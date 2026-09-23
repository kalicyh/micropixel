// SPDX-FileCopyrightText: 2024 kalicyh
// SPDX-License-Identifier: MIT
#include "platform/onewire/ds2484_reader.hpp"

#include <array>
#include <cinttypes>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "platform/onewire/ibutton_protocol.hpp"

namespace micropixel::platform::onewire {
namespace {
constexpr char kTag[] = "micropixel_ibutton";

// Per-call ownership: bus and pin leases are always released, including failures.
class Connection final : public Bus {
   public:
    explicit Connection(device::Gpio& gpio) : gpio_(gpio) {}
    ~Connection() override {
        if (device_ != nullptr) {
            (void)Configure(1);  // best effort: never leave strong pull-up enabled
            (void)i2c_master_bus_rm_device(device_);
        }
        if (bus_ != nullptr) (void)i2c_del_master_bus(bus_);
        if (scl_ != 0) gpio_.Close(scl_);
        if (sda_ != 0) gpio_.Close(sda_);
    }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    bool Open(micropixel_device_id_t sda, micropixel_device_id_t scl, uint16_t sda_line, uint16_t scl_line) {
        int32_t status = gpio_.Open(sda, MICROPIXEL_GPIO_MODE_INPUT, MICROPIXEL_GPIO_PULL_NONE,
                                    MICROPIXEL_GPIO_EDGE_NONE, 0, 0, nullptr, nullptr);
        if (status != MICROPIXEL_STATUS_OK) return false;
        sda_ = sda;
        status = gpio_.Open(scl, MICROPIXEL_GPIO_MODE_INPUT, MICROPIXEL_GPIO_PULL_NONE, MICROPIXEL_GPIO_EDGE_NONE, 0, 0,
                            nullptr, nullptr);
        if (status != MICROPIXEL_STATUS_OK) return false;
        scl_ = scl;
        i2c_master_bus_config_t config{};
        config.i2c_port = I2C_NUM_0;
        config.sda_io_num = static_cast<gpio_num_t>(sda_line);
        config.scl_io_num = static_cast<gpio_num_t>(scl_line);
        config.clk_source = I2C_CLK_SRC_DEFAULT;
        config.glitch_ignore_cnt = 7;
        config.flags.enable_internal_pullup = true;
        esp_err_t error = i2c_new_master_bus(&config, &bus_);
        if (error != ESP_OK) {
            ESP_LOGE(kTag, "I2C0 init failed: %s", esp_err_to_name(error));
            return false;
        }
        if (i2c_master_probe(bus_, 0x18, 5) != ESP_OK) return false;
        ESP_LOGI(kTag, "DS2484 found: SDA=GP%u SCL=GP%u address=0x18", sda_line, scl_line);
        i2c_device_config_t device_config{};
        device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device_config.device_address = 0x18;
        device_config.scl_speed_hz = 400000;
        error = i2c_master_bus_add_device(bus_, &device_config, &device_);
        if (error != ESP_OK) {
            ESP_LOGE(kTag, "DS2484 attach failed: %s", esp_err_to_name(error));
            return false;
        }
        uint8_t reset = 0xF0;
        uint8_t reset_status = 0;
        if (!Tx({&reset, 1}) || !Rx(reset_status)) {
            ESP_LOGE(kTag, "DS2484 reset failed");
            return false;
        }
        if (!Configure(1)) {
            ESP_LOGE(kTag, "DS2484 configuration failed");
            return false;
        }
        ESP_LOGI(kTag, "DS2484 ready, reset status=0x%02x", reset_status);
        return true;
    }
    bool Reset(bool& present) override {
        uint8_t command = 0xB4, status = 0;
        if (!Configure(1) || !Tx({&command, 1}) || !Wait(status)) return false;
        present = (status & 2U) != 0;
        return true;
    }
    bool Write(uint8_t byte, uint32_t strong_pullup_us = 0U) override {
        uint8_t command[]{0xA5, byte}, status = 0;
        if (!Configure(strong_pullup_us != 0U ? 5 : 1) || !Tx(command) || !Wait(status)) return false;
        if (strong_pullup_us != 0U) {
            esp_rom_delay_us(strong_pullup_us);
            return Configure(1);
        }
        return true;
    }
    bool Read(uint8_t& byte) override {
        uint8_t command = 0x96, status = 0;
        const uint8_t pointer[]{0xE1, 0xE1};
        return Configure(1) && Tx({&command, 1}) && Wait(status) && Tx(pointer) && Rx(byte);
    }
    bool Bit(bool output, bool& input) override {
        uint8_t command[]{0x87, static_cast<uint8_t>(output ? 0x80 : 0)}, status = 0;
        if (!Tx(command) || !Wait(status)) return false;
        input = (status & 0x20U) != 0;
        return true;
    }

   private:
    bool Tx(std::span<const uint8_t> bytes) {
        return i2c_master_transmit(device_, bytes.data(), bytes.size(), 20) == ESP_OK;
    }
    bool Rx(uint8_t& byte) { return i2c_master_receive(device_, &byte, 1, 20) == ESP_OK; }
    bool Configure(uint8_t config) {
        const uint8_t command[]{0xD2, static_cast<uint8_t>(config | ((~config & 0x0FU) << 4))};
        uint8_t readback = 0;
        return Tx(command) && Rx(readback) && (readback & 0x0FU) == config;
    }
    bool Wait(uint8_t& status) {
        const auto deadline = esp_timer_get_time() + 20000;
        do {
            if (!Rx(status)) return false;
            if ((status & 1U) == 0) return (status & 4U) == 0;
            esp_rom_delay_us(100);
        } while (esp_timer_get_time() < deadline);
        return false;
    }
    device::Gpio& gpio_;
    micropixel_device_id_t sda_{}, scl_{};
    i2c_master_bus_handle_t bus_{};
    i2c_master_dev_handle_t device_{};
};
}  // namespace
int32_t Ds2484Reader::Call(uint32_t method, const micropixel_ibutton_request_t& request,
                           micropixel_ibutton_response_t& response) {
    if (method != MICROPIXEL_IBUTTON_SCAN && method != MICROPIXEL_IBUTTON_READ &&
        method != MICROPIXEL_IBUTTON_WRITE) return MICROPIXEL_STATUS_UNSUPPORTED;
    if (method == MICROPIXEL_IBUTTON_READ && (request.length == 0 || request.length > 64))
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    if (method == MICROPIXEL_IBUTTON_WRITE &&
        (request.length != 64U || request.offset >= 4096U || (request.offset % 64U) != 0U))
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    struct Candidate final {
        micropixel_device_id_t device{};
        uint16_t line{};
    };
    Candidate sda{};
    Candidate scl{};
    bool has_sda = false;
    bool has_scl = false;
    for (uint32_t index = 0; index < devices_.Count(); ++index) {
        micropixel_device_info_t info{};
        micropixel_gpio_info_t line{};
        if (devices_.GetByIndex(index, info) != MICROPIXEL_STATUS_OK ||
            gpio_.GetInfo(info.device, line) != MICROPIXEL_STATUS_OK)
            continue;
        if (line.line_number == 17U) {
            sda = {info.device, line.line_number};
            has_sda = true;
        } else if (line.line_number == 15U) {
            scl = {info.device, line.line_number};
            has_scl = true;
        }
    }
    if (!has_sda || !has_scl) {
        response.operation_status = static_cast<uint32_t>(ReadStatus::kBusError);
        return MICROPIXEL_STATUS_OK;
    }
    Connection connection(gpio_);
    response.sda_line = 17U;
    response.scl_line = 15U;
    if (!connection.Open(sda.device, scl.device, sda.line, scl.line)) {
        response.operation_status = static_cast<uint32_t>(ReadStatus::kBusError);
        return MICROPIXEL_STATUS_OK;
    }
    ReadStatus status;
    if (method == MICROPIXEL_IBUTTON_SCAN) {
        status = Scan(connection, response.rom);
    } else {
        // Scan before each page so removal/replacement or a second device cannot
        // silently turn a stale selection into plausible data (especially DS1991).
        status = Scan(connection, response.rom);
        if (status == ReadStatus::kOk) {
            for (unsigned i = 0; i < 8; ++i) {
                if (response.rom[i] != request.rom[i]) status = ReadStatus::kNoDevice;
            }
        }
        if (status == ReadStatus::kOk) {
            if (method == MICROPIXEL_IBUTTON_READ) {
                status = ReadPage(connection, request.rom, request.offset, request.password,
                                  {response.data, request.length});
            } else {
                std::array<uint8_t, 64> data{};
                std::copy_n(request.data, data.size(), data.begin());
                status = WritePage(connection, request.rom, request.offset, request.password, data);
            }
        }
        if (status == ReadStatus::kOk)
            response.length = request.length;
        else
            for (auto& byte : response.data) byte = 0;
    }
    response.operation_status = static_cast<uint32_t>(status);
    return MICROPIXEL_STATUS_OK;
}
}  // namespace micropixel::platform::onewire

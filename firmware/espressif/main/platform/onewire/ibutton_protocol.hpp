// SPDX-FileCopyrightText: 2024 kalicyh
// SPDX-License-Identifier: MIT
// Read-only protocol adapted from esp-1wire/DS1977_1991.
#ifndef MICROPIXEL_PLATFORM_ONEWIRE_IBUTTON_PROTOCOL_HPP
#define MICROPIXEL_PLATFORM_ONEWIRE_IBUTTON_PROTOCOL_HPP

#include <cstdint>
#include <algorithm>
#include <array>
#include <span>

namespace micropixel::platform::onewire {

enum class ReadStatus : uint32_t {
    kOk,
    kNoDevice,
    kMultipleDevices,
    kUnsupported,
    kBusError,
    kCrcError,
    kInvalidRange
};

class Bus {
   public:
    virtual ~Bus() = default;
    virtual bool Reset(bool& present) = 0;
    virtual bool Write(uint8_t byte, uint32_t strong_pullup_us = 0U) = 0;
    virtual bool Read(uint8_t& byte) = 0;
    virtual bool Bit(bool output, bool& input) = 0;
};

inline uint16_t Crc16(std::span<const uint8_t> bytes) {
    uint16_t crc = 0;
    for (uint8_t byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1U) ? 0xA001U : 0U);
    }
    return crc;
}

inline uint8_t Crc8(std::span<const uint8_t> bytes) {
    uint8_t crc = 0;
    for (uint8_t byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1U) ? 0x8CU : 0U);
    }
    return crc;
}

// Reject a branching ROM search: the UI must never silently choose an iButton.
inline ReadStatus Scan(Bus& bus, std::span<uint8_t, 8> rom) {
    bool present = false;
    if (!bus.Reset(present)) return ReadStatus::kBusError;
    if (!present) return ReadStatus::kNoDevice;
    if (!bus.Write(0xF0)) return ReadStatus::kBusError;
    for (auto& byte : rom) byte = 0;
    for (unsigned bit = 0; bit < 64; ++bit) {
        bool value = false;
        bool complement = false;
        bool ignored = false;
        if (!bus.Bit(true, value) || !bus.Bit(true, complement)) return ReadStatus::kBusError;
        if (value && complement) return ReadStatus::kNoDevice;
        if (!value && !complement) return ReadStatus::kMultipleDevices;
        if (!bus.Bit(value, ignored)) return ReadStatus::kBusError;
        rom[bit / 8] |= static_cast<uint8_t>(value) << (bit % 8);
    }
    return Crc8(rom) == 0 ? ReadStatus::kOk : ReadStatus::kCrcError;
}

// One bounded page/subkey per call. No write-memory/password commands exist here.
inline ReadStatus ReadPage(Bus& bus, std::span<const uint8_t, 8> rom, uint16_t offset,
                           std::span<const uint8_t, 8> password, std::span<uint8_t> output) {
    const bool ds1977 = rom[0] == 0x37;
    if (!ds1977 && rom[0] != 0x02) return ReadStatus::kUnsupported;
    const unsigned stride = ds1977 ? 64 : 48;
    const unsigned limit = ds1977 ? 32768 : 144;
    if (output.empty() || offset >= limit || output.size() > limit - offset || output.size() > stride - offset % stride)
        return ReadStatus::kInvalidRange;
    if (Crc8(rom) != 0) return ReadStatus::kCrcError;
    bool present = false;
    if (!bus.Reset(present)) return ReadStatus::kBusError;
    if (!present) return ReadStatus::kNoDevice;
    if (!bus.Write(0x55)) return ReadStatus::kBusError;
    for (auto byte : rom)
        if (!bus.Write(byte)) return ReadStatus::kBusError;
    if (!ds1977) {
        const uint8_t address = (offset / 48) * 64 + 16 + offset % 48;
        if (!bus.Write(0x66) || !bus.Write(address) || !bus.Write(static_cast<uint8_t>(~address)))
            return ReadStatus::kBusError;
        uint8_t ignored = 0;
        for (unsigned i = 0; i < 8; ++i)
            if (!bus.Read(ignored)) return ReadStatus::kBusError;
        for (auto byte : password)
            if (!bus.Write(byte)) return ReadStatus::kBusError;
        for (auto& byte : output)
            if (!bus.Read(byte)) return ReadStatus::kBusError;
        // DS1991 has no data CRC; a wrong password can produce plausible pseudo-data.
        return ReadStatus::kOk;
    }
    uint8_t frame[67]{0x69, static_cast<uint8_t>(offset), static_cast<uint8_t>(offset >> 8)};
    for (unsigned i = 0; i < 3; ++i)
        if (!bus.Write(frame[i])) return ReadStatus::kBusError;
    for (unsigned i = 0; i < 8; ++i)
        if (!bus.Write(password[i], i == 7 ? 3000U : 0U)) return ReadStatus::kBusError;
    const unsigned remaining = 64 - offset % 64;
    for (unsigned i = 0; i < remaining; ++i)
        if (!bus.Read(frame[i + 3])) return ReadStatus::kBusError;
    uint8_t low = 0, high = 0;
    if (!bus.Read(low) || !bus.Read(high)) return ReadStatus::kBusError;
    const uint16_t expected = static_cast<uint16_t>(~Crc16({frame, remaining + 3}));
    if (low != static_cast<uint8_t>(expected) || high != static_cast<uint8_t>(expected >> 8))
        return ReadStatus::kCrcError;
    for (unsigned i = 0; i < output.size(); ++i) output[i] = frame[i + 3];
    return ReadStatus::kOk;
}

inline ReadStatus WritePage(Bus& bus, std::span<const uint8_t, 8> rom, uint16_t offset,
                            std::span<const uint8_t, 8> password,
                            std::span<const uint8_t, 64> data) {
    if (rom[0] != 0x37) return ReadStatus::kUnsupported;
    if ((offset & 63U) != 0U || offset >= 4096U || Crc8(rom) != 0U) return ReadStatus::kInvalidRange;
    auto select = [&]() {
        bool present = false;
        if (!bus.Reset(present)) return ReadStatus::kBusError;
        if (!present) return ReadStatus::kNoDevice;
        if (!bus.Write(0x55)) return ReadStatus::kBusError;
        for (auto byte : rom) if (!bus.Write(byte)) return ReadStatus::kBusError;
        return ReadStatus::kOk;
    };

    auto status = select();
    if (status != ReadStatus::kOk) return status;
    const uint8_t ta1 = static_cast<uint8_t>(offset);
    const uint8_t ta2 = static_cast<uint8_t>(offset >> 8U);
    if (!bus.Write(0x0F) || !bus.Write(ta1) || !bus.Write(ta2)) return ReadStatus::kBusError;
    for (auto byte : data) if (!bus.Write(byte)) return ReadStatus::kBusError;
    uint8_t write_crc[2]{};
    if (!bus.Read(write_crc[0]) || !bus.Read(write_crc[1])) return ReadStatus::kBusError;
    uint8_t write_frame[67]{0x0F, ta1, ta2};
    for (unsigned i = 0; i < data.size(); ++i) write_frame[3U + i] = data[i];
    const uint16_t expected_write_crc = static_cast<uint16_t>(~Crc16(write_frame));
    if (write_crc[0] != static_cast<uint8_t>(expected_write_crc) ||
        write_crc[1] != static_cast<uint8_t>(expected_write_crc >> 8U)) return ReadStatus::kCrcError;

    status = select();
    if (status != ReadStatus::kOk) return status;
    if (!bus.Write(0xAA)) return ReadStatus::kBusError;
    uint8_t scratch_header[3]{};
    if (!bus.Read(scratch_header[0]) || !bus.Read(scratch_header[1]) || !bus.Read(scratch_header[2]))
        return ReadStatus::kBusError;
    uint8_t scratch_data[64]{};
    for (auto& byte : scratch_data) if (!bus.Read(byte)) return ReadStatus::kBusError;
    uint8_t scratch_crc[2]{};
    if (!bus.Read(scratch_crc[0]) || !bus.Read(scratch_crc[1])) return ReadStatus::kBusError;
    uint8_t scratch_frame[68]{0xAA, scratch_header[0], scratch_header[1], scratch_header[2]};
    for (unsigned i = 0; i < sizeof(scratch_data); ++i) scratch_frame[4U + i] = scratch_data[i];
    const uint16_t expected_scratch_crc = static_cast<uint16_t>(~Crc16(scratch_frame));
    if (scratch_crc[0] != static_cast<uint8_t>(expected_scratch_crc) ||
        scratch_crc[1] != static_cast<uint8_t>(expected_scratch_crc >> 8U)) return ReadStatus::kCrcError;
    if (scratch_header[0] != ta1 || scratch_header[1] != ta2 || (scratch_header[2] & 0xC0U) != 0U ||
        !std::equal(data.begin(), data.end(), scratch_data)) return ReadStatus::kCrcError;

    status = select();
    if (status != ReadStatus::kOk) return status;
    if (!bus.Write(0x99) || !bus.Write(scratch_header[0]) || !bus.Write(scratch_header[1]) ||
        !bus.Write(scratch_header[2])) return ReadStatus::kBusError;
    for (unsigned i = 0; i < password.size(); ++i)
        if (!bus.Write(password[i], i == password.size() - 1U ? 25000U : 0U)) return ReadStatus::kBusError;
    uint8_t ack = 0xFF;
    if (!bus.Read(ack)) return ReadStatus::kBusError;
    if (ack != 0xAA) return ReadStatus::kCrcError;

    std::array<uint8_t, 64> verify{};
    status = ReadPage(bus, rom, offset, password, verify);
    if (status != ReadStatus::kOk) return status;
    return std::equal(data.begin(), data.end(), verify.begin()) ? ReadStatus::kOk : ReadStatus::kCrcError;
}

}  // namespace micropixel::platform::onewire
#endif

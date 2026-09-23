// SPDX-FileCopyrightText: 2024 kalicyh
// SPDX-License-Identifier: MIT
// Read-only protocol adapted from esp-1wire/DS1977_1991.
#ifndef MICROPIXEL_PLATFORM_ONEWIRE_IBUTTON_PROTOCOL_HPP
#define MICROPIXEL_PLATFORM_ONEWIRE_IBUTTON_PROTOCOL_HPP

#include <cstdint>
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
    virtual bool Write(uint8_t byte, bool strong_pullup = false) = 0;
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
        if (!bus.Write(password[i], i == 7)) return ReadStatus::kBusError;
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

}  // namespace micropixel::platform::onewire
#endif

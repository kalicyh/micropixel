#include <array>
#include <cassert>
#include <vector>

#include "abi/micropixel_ibutton.h"
#include "platform/onewire/ibutton_protocol.hpp"

static_assert(sizeof(micropixel_ibutton_request_t) == 24);
static_assert(sizeof(micropixel_ibutton_response_t) == 84);

using namespace micropixel::platform::onewire;
struct FakeBus final : Bus {
    bool present{true};
    bool fail{};
    std::vector<uint8_t> incoming;
    std::vector<uint8_t> written;
    std::vector<bool> bits;
    unsigned read_index{}, bit_index{}, strong_count{};
    bool Reset(bool& value) override {
        value = present;
        return !fail;
    }
    bool Write(uint8_t byte, bool strong = false) override {
        written.push_back(byte);
        strong_count += strong;
        return !fail;
    }
    bool Read(uint8_t& byte) override {
        if (fail || read_index == incoming.size()) return false;
        byte = incoming[read_index++];
        return true;
    }
    bool Bit(bool, bool& input) override {
        if (fail || bit_index == bits.size()) return false;
        input = bits[bit_index++];
        return true;
    }
};
std::array<uint8_t, 8> Rom(uint8_t family) {
    std::array<uint8_t, 8> rom{family, 1, 2, 3, 4, 5, 6, 0};
    rom[7] = Crc8({rom.data(), 7});
    return rom;
}
int main() {
    constexpr std::array<uint8_t, 9> check{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    assert(Crc8(check) == 0xA1 && Crc16(check) == 0xBB3D);
    const std::array<uint8_t, 8> password{0, 1, 2, 3, 4, 5, 6, 7};
    std::array<uint8_t, 64> output{};
    auto rom = Rom(0x37);
    FakeBus scan;
    for (auto byte : rom)
        for (unsigned bit = 0; bit < 8; ++bit) {
            bool value = (byte & (1U << bit)) != 0;
            scan.bits.insert(scan.bits.end(), {value, !value, value});
        }
    std::array<uint8_t, 8> found{};
    assert(Scan(scan, found) == ReadStatus::kOk && found == rom);
    FakeBus multi;
    multi.bits = {false, false};
    assert(Scan(multi, found) == ReadStatus::kMultipleDevices);
    FakeBus empty;
    empty.present = false;
    assert(Scan(empty, found) == ReadStatus::kNoDevice);
    FakeBus ds1977;
    std::array<uint8_t, 67> frame{0x69, 0x40, 0};
    for (unsigned i = 0; i < 64; ++i) frame[i + 3] = static_cast<uint8_t>(i);
    ds1977.incoming.assign(frame.begin() + 3, frame.end());
    uint16_t crc = static_cast<uint16_t>(~Crc16(frame));
    ds1977.incoming.push_back(static_cast<uint8_t>(crc));
    ds1977.incoming.push_back(static_cast<uint8_t>(crc >> 8));
    assert(ReadPage(ds1977, rom, 64, password, output) == ReadStatus::kOk);
    assert(output[63] == 63 && ds1977.strong_count == 1);
    assert(ds1977.written[9] == 0x69 && ds1977.written.back() == 7);
    ds1977.read_index = 0;
    ds1977.incoming.back() ^= 1;
    output.fill(0xAA);
    assert(ReadPage(ds1977, rom, 64, password, output) == ReadStatus::kCrcError);
    assert(output[0] == 0xAA);
    assert(ReadPage(ds1977, rom, 63, password, output) == ReadStatus::kInvalidRange);
    assert(ReadPage(ds1977, rom, 32768, password, output) == ReadStatus::kInvalidRange);
    rom = Rom(0x02);
    for (unsigned key = 0; key < 3; ++key) {
        FakeBus ds1991;
        ds1991.incoming.resize(56, static_cast<uint8_t>(key));
        assert(ReadPage(ds1991, rom, key * 48, password, {output.data(), 48}) == ReadStatus::kOk);
        assert(ds1991.written[9] == 0x66);
        assert(ds1991.written[10] == key * 64 + 16);
        assert(ds1991.written[11] == static_cast<uint8_t>(~(key * 64 + 16)));
        assert(ds1991.strong_count == 0 && output[47] == key);
    }
    FakeBus failed;
    failed.fail = true;
    assert(ReadPage(failed, rom, 0, password, {output.data(), 48}) == ReadStatus::kBusError);
    rom = Rom(0x01);
    assert(ReadPage(failed, rom, 0, password, output) == ReadStatus::kUnsupported);
}

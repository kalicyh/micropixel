#ifndef MICROPIXEL_IBUTTON_READER_KEY_ALGORITHMS_HPP
#define MICROPIXEL_IBUTTON_READER_KEY_ALGORITHMS_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace ibutton_reader {
using Password = std::array<uint8_t, 8>;
using Rom = std::array<uint8_t, 8>;
using Rule = int16_t;

constexpr Rule Ref(unsigned byte, unsigned bit, bool invert = false) {
    const auto value = static_cast<Rule>(10U + byte * 8U + bit);
    return invert ? -value : value;
}

inline constexpr std::array<Rule, 64> kAlpha{
    Ref(2, 6), 0, 1, 1, 0, Ref(2, 3), Ref(3, 4), 0,
    1, 1, 0, 1, 0, 1, 0, Ref(7, 6),
    0, Ref(1, 1), 1, Ref(3, 0), Ref(7, 3), 1, Ref(7, 4), 0,
    1, Ref(7, 0), Ref(7, 7), Ref(1, 3), 1, 0, 0, 0,
    Ref(3, 2), Ref(2, 1), 1, Ref(1, 0), 1, Ref(2, 5), 0, 1,
    Ref(7, 1), 0, Ref(1, 7), 1, 1, 1, 0, 1,
    1, Ref(3, 3), 1, Ref(1, 5), 0, 1, 1, 1,
    1, 0, 1, 1, 0, 1, Ref(3, 5), 0,
};

inline constexpr std::array<Rule, 64> kBeta{
    Ref(2, 6), 1, 1, 1, 1, Ref(2, 3), Ref(3, 4), 0,
    1, 1, 0, 1, 1, 1, 1, -Ref(7, 6),
    0, -Ref(1, 1), 1, -Ref(3, 0), Ref(7, 3), 1, Ref(7, 4), 0,
    1, Ref(7, 0), -Ref(7, 7), Ref(1, 3), 1, 1, 0, 0,
    -Ref(3, 2), -Ref(2, 1), 0, -Ref(1, 0), 0, Ref(2, 5), 0, 0,
    -Ref(7, 1), 1, -Ref(1, 7), 0, 0, 0, 0, 0,
    0, Ref(3, 3), 0, -Ref(1, 5), 0, 1, 1, 0,
    1, 0, 0, 1, 1, 0, -Ref(3, 5), 0,
};

constexpr unsigned GetBit(const Rom& rom, unsigned byte, unsigned bit) {
    return (rom[byte] >> bit) & 1U;
}

template <std::size_t N>
constexpr Password Derive(const Rom& rom, const std::array<Rule, N>& rules) {
    Password result{};
    for (unsigned i = 0; i < result.size(); ++i) {
        uint8_t value = 0;
        for (unsigned j = 0; j < 8; ++j) {
            const Rule rule = rules[i * 8U + j];
            unsigned bit = 0;
            if (rule == 1) bit = 1;
            else if (rule >= 10) {
                const unsigned source = static_cast<unsigned>(rule - 10);
                bit = GetBit(rom, source / 8U, source % 8U);
            } else if (rule <= -10) {
                const unsigned source = static_cast<unsigned>(-rule - 10);
                bit = GetBit(rom, source / 8U, source % 8U) ^ 1U;
            }
            value = static_cast<uint8_t>((value << 1U) | bit);
        }
        result[i] = value;
    }
    return result;
}

constexpr Password Alpha(const Rom& rom) { return Derive(rom, kAlpha); }
constexpr Password Beta(const Rom& rom) { return Derive(rom, kBeta); }
}  // namespace ibutton_reader

#endif

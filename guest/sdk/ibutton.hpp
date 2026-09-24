#ifndef MICROPIXEL_SDK_IBUTTON_HPP
#define MICROPIXEL_SDK_IBUTTON_HPP
#include <array>
#include <cstdint>

#include "sdk/result.hpp"
namespace micropixel {
class Application;
enum class IButtonStatus : uint32_t {
    kOk,
    kNoDevice,
    kMultipleDevices,
    kUnsupported,
    kBusError,
    kCrcError,
    kInvalidRange
};
struct IButtonPage final {
    IButtonStatus status{};
    std::array<uint8_t, 8> rom{};
    uint16_t length{};
    uint8_t sda_line{};
    uint8_t scl_line{};
    std::array<uint8_t, 64> data{};
};
class IButton final {
   public:
    [[nodiscard]] Result<IButtonPage> Scan() const;
    [[nodiscard]] Result<IButtonPage> Read(const std::array<uint8_t, 8>& rom, uint16_t offset, uint16_t length,
                                           const std::array<uint8_t, 8>& password) const;
    [[nodiscard]] Result<IButtonPage> Write(const std::array<uint8_t, 8>& rom, uint16_t offset,
                                            const std::array<uint8_t, 64>& data,
                                            const std::array<uint8_t, 8>& password,
                                            uint16_t length = 64U) const;

   private:
    struct CapabilityToken final {
       private:
        constexpr CapabilityToken() = default;
        friend class Application;
    };
    explicit constexpr IButton(CapabilityToken) noexcept {}
    friend class Application;
};
}  // namespace micropixel
#endif

#ifndef MICROPIXEL_PLATFORM_ONEWIRE_DS2484_READER_HPP
#define MICROPIXEL_PLATFORM_ONEWIRE_DS2484_READER_HPP
#include "device/contracts/devices.hpp"
#include "device/contracts/gpio.hpp"
#include "device/contracts/ibutton.hpp"
namespace micropixel::platform::onewire {
class Ds2484Reader final : public device::IButton {
   public:
    Ds2484Reader(device::DeviceCatalog& devices, device::Gpio& gpio) : devices_(devices), gpio_(gpio) {}
    int32_t Call(uint32_t method, const micropixel_ibutton_request_t& request,
                 micropixel_ibutton_response_t& response) override;

   private:
    device::DeviceCatalog& devices_;
    device::Gpio& gpio_;
};
}  // namespace micropixel::platform::onewire
#endif

#ifndef MICROPIXEL_DEVICE_IBUTTON_HPP
#define MICROPIXEL_DEVICE_IBUTTON_HPP
#include "abi/micropixel_ibutton.h"
namespace micropixel::device {
class IButton {
   public:
    virtual ~IButton() = default;
    virtual int32_t Call(uint32_t method, const micropixel_ibutton_request_t& request,
                         micropixel_ibutton_response_t& response) = 0;
};
}  // namespace micropixel::device
#endif

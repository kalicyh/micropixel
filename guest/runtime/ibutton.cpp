#include "sdk/ibutton.hpp"

#include "abi/micropixel_ibutton.h"
#include "runtime/service_binding.hpp"
namespace micropixel {
namespace {
runtime::ServiceCache ibutton_service;
Result<IButtonPage> Call(uint32_t method, const micropixel_ibutton_request_t& request) {
    auto status = runtime::OpenService(ibutton_service, MICROPIXEL_SERVICE_IBUTTON, 1, 0);
    if (status != MICROPIXEL_STATUS_OK) return unexpected(runtime::ErrorFromStatus(status));
    micropixel_ibutton_response_t response{};
    uint32_t size = 0;
    status =
        runtime::CallService(ibutton_service, method, &request, sizeof(request), &response, sizeof(response), size);
    if (status != MICROPIXEL_STATUS_OK) return unexpected(runtime::ErrorFromStatus(status));
    if (size != sizeof(response) || response.size != sizeof(response) || response.length > 64 ||
        response.operation_status > 6 || (response.operation_status == 0 && response.length != request.length))
        return unexpected(Error{ErrorCode::kInternal});
    IButtonPage result{};
    result.status = static_cast<IButtonStatus>(response.operation_status);
    result.length = response.length;
    result.sda_line = response.sda_line;
    result.scl_line = response.scl_line;
    runtime::CopyBytes(result.rom.data(), response.rom, 8);
    runtime::CopyBytes(result.data.data(), response.data, 64);
    return result;
}
}  // namespace
Result<IButtonPage> IButton::Scan() const {
    micropixel_ibutton_request_t request{};
    request.size = sizeof(request);
    return Call(MICROPIXEL_IBUTTON_SCAN, request);
}
Result<IButtonPage> IButton::Read(const std::array<uint8_t, 8>& rom, uint16_t offset, uint16_t length,
                                  const std::array<uint8_t, 8>& password) const {
    if (length == 0 || length > 64) return unexpected(Error{ErrorCode::kInvalidArgument});
    micropixel_ibutton_request_t request{};
    request.size = sizeof(request);
    request.offset = offset;
    request.length = length;
    runtime::CopyBytes(request.rom, rom.data(), 8);
    runtime::CopyBytes(request.password, password.data(), 8);
    return Call(MICROPIXEL_IBUTTON_READ, request);
}
Result<IButtonPage> IButton::Write(const std::array<uint8_t, 8>& rom, uint16_t offset,
                                   const std::array<uint8_t, 64>& data,
                                   const std::array<uint8_t, 8>& password) const {
    if ((offset % 64U) != 0U || offset >= 4096U) return unexpected(Error{ErrorCode::kInvalidArgument});
    micropixel_ibutton_request_t request{};
    request.size = sizeof(request);
    request.offset = offset;
    request.length = 64;
    runtime::CopyBytes(request.rom, rom.data(), 8);
    runtime::CopyBytes(request.password, password.data(), 8);
    runtime::CopyBytes(request.data, data.data(), data.size());
    return Call(MICROPIXEL_IBUTTON_WRITE, request);
}
}  // namespace micropixel

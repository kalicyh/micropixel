#ifndef MICROPIXEL_ABI_IBUTTON_H
#define MICROPIXEL_ABI_IBUTTON_H
#include <stdint.h>
// Experimental read-only service, interface 1.0. Fixed little-endian wire layout.
#define MICROPIXEL_SERVICE_IBUTTON 24U
#define MICROPIXEL_IBUTTON_SCAN 1U
#define MICROPIXEL_IBUTTON_READ 2U
// operation_status: 0 OK, 1 absent, 2 multiple, 3 unsupported family,
// 4 bus error, 5 CRC/password error, 6 invalid range.
typedef struct micropixel_ibutton_request {
    uint32_t size;
    uint16_t offset;
    uint16_t length;
    uint8_t rom[8];
    uint8_t password[8];
} micropixel_ibutton_request_t;
typedef struct micropixel_ibutton_response {
    uint32_t size;
    uint32_t operation_status;
    uint8_t rom[8];
    uint16_t length;
    uint8_t sda_line;
    uint8_t scl_line;
    uint8_t data[64];
} micropixel_ibutton_response_t;
#endif

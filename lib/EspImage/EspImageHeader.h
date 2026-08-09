#pragma once

#include <cstddef>
#include <cstdint>

// Parser for the 24-byte main header of an ESP32 firmware image
// (esp_image_header_t in ESP-IDF's esp_app_format.h). Pure byte work with no
// device dependencies -- safe to unit test on the host, which is why it lives
// here rather than inside FirmwareFlasher.
//
// The field that motivated pulling this out is chip_id. An image built for a
// different MCU is otherwise byte-for-byte well formed -- right magic, valid
// segment table, matching XOR checksum and SHA256 trailer -- so nothing else
// in an integrity check can tell it apart from an image that will actually
// boot on this silicon.
namespace EspImage {

// sizeof(esp_image_header_t). Fixed by the image format; ESP-IDF static-asserts
// it in esp_app_format.h.
inline constexpr size_t HEADER_SIZE = 24;

// ESP_IMAGE_HEADER_MAGIC.
inline constexpr uint8_t IMAGE_MAGIC = 0xE9;

// esp_chip_id_t values for the MCUs this firmware is built for. The full list
// lives in esp_app_format.h; these two are named here so host tests can refer
// to them without pulling in an ESP-IDF header.
inline constexpr uint16_t CHIP_ID_ESP32C3 = 0x0005;
inline constexpr uint16_t CHIP_ID_ESP32S3 = 0x0009;

struct Header {
  uint8_t magic = 0;          // offset 0
  uint8_t segmentCount = 0;   // offset 1
  uint16_t chipId = 0;        // offsets 12-13, little-endian
  bool hashAppended = false;  // offset 23; nonzero => a SHA256 digest of the
                              // whole image follows the checksum byte
};

// Fills `out` from the first HEADER_SIZE bytes of `bytes`. Returns false --
// leaving `out` untouched -- only when the buffer is null or shorter than the
// header. A foreign magic or an unexpected chip_id is reported through `out`
// rather than as a parse failure, so the caller can tell those cases apart and
// report each one to the user differently.
bool parseHeader(const uint8_t* bytes, size_t len, Header& out);

}  // namespace EspImage

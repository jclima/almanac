#include "EspImageHeader.h"

#include <cstring>

namespace EspImage {

namespace {
// Byte offsets within esp_image_header_t. The struct is packed and its layout
// is part of the on-flash format, so these are stable.
constexpr size_t OFF_MAGIC = 0;
constexpr size_t OFF_SEGMENT_COUNT = 1;
constexpr size_t OFF_CHIP_ID = 12;
constexpr size_t OFF_HASH_APPENDED = 23;
}  // namespace

bool parseHeader(const uint8_t* bytes, size_t len, Header& out) {
  if (!bytes || len < HEADER_SIZE) return false;

  out.magic = bytes[OFF_MAGIC];
  out.segmentCount = bytes[OFF_SEGMENT_COUNT];
  // memcpy, not a cast: `bytes` comes straight off a file read and carries no
  // alignment guarantee, and the ESP32-C3 is RISC-V, which faults on unaligned
  // multi-byte loads. The image format stores chip_id little-endian, which is
  // also the target's byte order, so no swap is needed.
  std::memcpy(&out.chipId, bytes + OFF_CHIP_ID, sizeof(out.chipId));
  out.hashAppended = bytes[OFF_HASH_APPENDED] != 0;
  return true;
}

}  // namespace EspImage

#include "TesseraeFrame.h"

namespace tesserae {

namespace {
bool pixelBelongsToPlane(const uint8_t level, const GrayPlane plane) {
  switch (plane) {
    case GrayPlane::Base:
      return level == 3;
    case GrayPlane::Lsb:
      return level == 1;
    case GrayPlane::Msb:
      return level == 1 || level == 2;
  }
  return false;
}
}  // namespace

uint8_t expandGray2Pair(const uint8_t first, const uint8_t second, const GrayPlane plane) {
  const uint8_t packed[2] = {first, second};
  uint8_t result = 0;
  for (uint8_t pixel = 0; pixel < 8; ++pixel) {
    const uint8_t source = packed[pixel / 4];
    const uint8_t shift = static_cast<uint8_t>(6 - (pixel % 4) * 2);
    const uint8_t level = static_cast<uint8_t>((source >> shift) & 0x03);
    if (pixelBelongsToPlane(level, plane)) {
      result |= static_cast<uint8_t>(1U << (7 - pixel));
    }
  }
  return result;
}

}  // namespace tesserae

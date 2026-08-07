#pragma once

#include <cstddef>
#include <cstdint>

namespace tesserae {

enum class FrameDepth : uint8_t { Mono1, Gray2 };

enum class GrayPlane : uint8_t {
  Base,
  Lsb,
  Msb,
};

// Tesserae's Xteink renderers emit one native-panel byte per eight pixels for
// monochrome frames and two bytes per eight pixels for four-level grayscale.
constexpr size_t encodedFrameSize(const size_t monoBufferSize, const FrameDepth depth) {
  return depth == FrameDepth::Gray2 ? monoBufferSize * 2 : monoBufferSize;
}

// Convert eight packed 2-bpp pixels (two input bytes, MSB-first) into one
// controller plane byte. Tesserae encodes 0=black, 1=dark gray,
// 2=light gray, 3=white. Almanac's Xteink grayscale driver expects:
//   base: white pixels only
//   LSB:  dark-gray pixels only
//   MSB:  dark- and light-gray pixels
uint8_t expandGray2Pair(uint8_t first, uint8_t second, GrayPlane plane);

}  // namespace tesserae

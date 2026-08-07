#include <gtest/gtest.h>

#include "Tesserae/TesseraeFrame.h"

namespace {

TEST(TesseraeFrame, ReportsEncodedSizes) {
  EXPECT_EQ(tesserae::encodedFrameSize(48000, tesserae::FrameDepth::Mono1), 48000U);
  EXPECT_EQ(tesserae::encodedFrameSize(48000, tesserae::FrameDepth::Gray2), 96000U);
}

TEST(TesseraeFrame, ExpandsEachGrayscalePlane) {
  // Levels: black, dark, light, white, white, light, dark, black.
  constexpr uint8_t first = 0x1B;   // 00 01 10 11
  constexpr uint8_t second = 0xE4;  // 11 10 01 00

  EXPECT_EQ(tesserae::expandGray2Pair(first, second, tesserae::GrayPlane::Base), 0x18);
  EXPECT_EQ(tesserae::expandGray2Pair(first, second, tesserae::GrayPlane::Lsb), 0x42);
  EXPECT_EQ(tesserae::expandGray2Pair(first, second, tesserae::GrayPlane::Msb), 0x66);
}

TEST(TesseraeFrame, KeepsSolidExtremesOutOfGrayPlanes) {
  EXPECT_EQ(tesserae::expandGray2Pair(0xFF, 0xFF, tesserae::GrayPlane::Base), 0xFF);
  EXPECT_EQ(tesserae::expandGray2Pair(0xFF, 0xFF, tesserae::GrayPlane::Lsb), 0x00);
  EXPECT_EQ(tesserae::expandGray2Pair(0xFF, 0xFF, tesserae::GrayPlane::Msb), 0x00);

  EXPECT_EQ(tesserae::expandGray2Pair(0x00, 0x00, tesserae::GrayPlane::Base), 0x00);
  EXPECT_EQ(tesserae::expandGray2Pair(0x00, 0x00, tesserae::GrayPlane::Lsb), 0x00);
  EXPECT_EQ(tesserae::expandGray2Pair(0x00, 0x00, tesserae::GrayPlane::Msb), 0x00);
}

}  // namespace

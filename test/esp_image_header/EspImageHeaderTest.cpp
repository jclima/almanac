#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "EspImageHeader.h"

namespace {

// The first 24 bytes of .pio/build/default/firmware.bin (Almanac 1.0.1,
// [env:default], ESP32-C3). Kept verbatim so the parser is pinned against a
// real image rather than only against synthetic bytes.
constexpr std::array<uint8_t, EspImage::HEADER_SIZE> c3ImageHeader = {
    0xE9, 0x07, 0x02, 0x4F, 0x4A, 0x09, 0x38, 0x40, 0xEE, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x01,
};

// Sentinels, so a parse that wrongly reports success while leaving the output
// alone is visible rather than passing by coincidence.
EspImage::Header sentinelHeader() {
  EspImage::Header h;
  h.magic = 0xAB;
  h.segmentCount = 0xCD;
  h.chipId = 0xBEEF;
  h.hashAppended = true;
  return h;
}

}  // namespace

TEST(EspImageHeader, ParsesARealEsp32C3ImageHeader) {
  EspImage::Header h = sentinelHeader();
  ASSERT_TRUE(EspImage::parseHeader(c3ImageHeader.data(), c3ImageHeader.size(), h));
  EXPECT_EQ(h.magic, EspImage::IMAGE_MAGIC);
  EXPECT_EQ(h.segmentCount, 7);
  EXPECT_EQ(h.chipId, EspImage::CHIP_ID_ESP32C3);
  EXPECT_TRUE(h.hashAppended);
}

// Pins every field to its byte offset in esp_image_header_t: byte i holds the
// value i, so any off-by-one in the field offsets shifts a value visibly.
// chip_id spans bytes 12-13, hence 0x0D0C little-endian.
TEST(EspImageHeader, ReadsEachFieldFromItsOwnOffset) {
  std::array<uint8_t, EspImage::HEADER_SIZE> bytes{};
  for (size_t i = 0; i < bytes.size(); i++) bytes[i] = static_cast<uint8_t>(i);

  EspImage::Header h = sentinelHeader();
  ASSERT_TRUE(EspImage::parseHeader(bytes.data(), bytes.size(), h));
  EXPECT_EQ(h.magic, 0x00);         // offset 0
  EXPECT_EQ(h.segmentCount, 0x01);  // offset 1
  EXPECT_EQ(h.chipId, 0x0D0C);      // offsets 12-13, little-endian
  EXPECT_TRUE(h.hashAppended);      // offset 23, nonzero
}

// chip_id is a little-endian uint16. Reading it big-endian would turn the
// ESP32-S3's 0x0009 into 0x0900 and, worse, the C3's 0x0005 into 0x0500 --
// a wrong-chip image would then be indistinguishable from a right-chip one
// only by luck of which side of the comparison also got byte-swapped.
TEST(EspImageHeader, ReadsChipIdAsLittleEndian) {
  std::array<uint8_t, EspImage::HEADER_SIZE> bytes{};
  bytes[0] = EspImage::IMAGE_MAGIC;
  bytes[12] = 0x00;
  bytes[13] = 0x05;

  EspImage::Header h = sentinelHeader();
  ASSERT_TRUE(EspImage::parseHeader(bytes.data(), bytes.size(), h));
  EXPECT_EQ(h.chipId, 0x0500);
}

// The neighbours of chip_id (spi_pin_drv[2] at 11, min_chip_rev at 14) are
// distinct and nonzero, so a one-byte slide in either direction is caught.
TEST(EspImageHeader, IgnoresTheBytesAdjacentToChipId) {
  std::array<uint8_t, EspImage::HEADER_SIZE> bytes{};
  bytes[0] = EspImage::IMAGE_MAGIC;
  bytes[11] = 0xAA;
  bytes[12] = 0x09;
  bytes[13] = 0x00;
  bytes[14] = 0xBB;

  EspImage::Header h = sentinelHeader();
  ASSERT_TRUE(EspImage::parseHeader(bytes.data(), bytes.size(), h));
  EXPECT_EQ(h.chipId, EspImage::CHIP_ID_ESP32S3);
}

TEST(EspImageHeader, ReportsHashAppendedFalseWhenTheLastHeaderByteIsZero) {
  std::array<uint8_t, EspImage::HEADER_SIZE> bytes{};
  bytes[0] = EspImage::IMAGE_MAGIC;
  bytes[23] = 0x00;

  EspImage::Header h = sentinelHeader();
  ASSERT_TRUE(EspImage::parseHeader(bytes.data(), bytes.size(), h));
  EXPECT_FALSE(h.hashAppended);
}

// A non-0xE9 magic is reported, not rejected: the caller distinguishes "not an
// ESP image" from "short read", and they map to different errors on screen.
TEST(EspImageHeader, ReportsAForeignMagicRatherThanFailing) {
  std::array<uint8_t, EspImage::HEADER_SIZE> bytes{};
  bytes[0] = 0x50;  // 'P', e.g. the user picked a zip

  EspImage::Header h = sentinelHeader();
  ASSERT_TRUE(EspImage::parseHeader(bytes.data(), bytes.size(), h));
  EXPECT_EQ(h.magic, 0x50);
  EXPECT_NE(h.magic, EspImage::IMAGE_MAGIC);
}

TEST(EspImageHeader, RejectsABufferShorterThanTheHeader) {
  std::array<uint8_t, EspImage::HEADER_SIZE> bytes = c3ImageHeader;

  EspImage::Header h = sentinelHeader();
  EXPECT_FALSE(EspImage::parseHeader(bytes.data(), EspImage::HEADER_SIZE - 1, h));
  // Output untouched, so a caller that ignores the return value cannot act on
  // half-filled fields.
  EXPECT_EQ(h.magic, 0xAB);
  EXPECT_EQ(h.segmentCount, 0xCD);
  EXPECT_EQ(h.chipId, 0xBEEF);
}

TEST(EspImageHeader, RejectsANullBuffer) {
  EspImage::Header h = sentinelHeader();
  EXPECT_FALSE(EspImage::parseHeader(nullptr, EspImage::HEADER_SIZE, h));
}

// The ESP32-C3 is RISC-V and faults on unaligned multi-byte loads, so chip_id
// must be memcpy'd rather than read through a reinterpret_cast. This exercises
// a header sitting at an odd address; it cannot fault on x86, but it fails
// loudly under -fsanitize=alignment and documents the constraint.
TEST(EspImageHeader, ParsesFromAnUnalignedAddress) {
  std::array<uint8_t, EspImage::HEADER_SIZE + 1> raw{};
  for (size_t i = 0; i < c3ImageHeader.size(); i++) raw[i + 1] = c3ImageHeader[i];

  EspImage::Header h = sentinelHeader();
  ASSERT_TRUE(EspImage::parseHeader(raw.data() + 1, EspImage::HEADER_SIZE, h));
  EXPECT_EQ(h.chipId, EspImage::CHIP_ID_ESP32C3);
  EXPECT_EQ(h.magic, EspImage::IMAGE_MAGIC);
}

#include <gtest/gtest.h>

#include "SemVer.h"

namespace {

struct Parsed {
  bool ok;
  int major;
  int minor;
  int patch;
};

Parsed parse(const char* text) {
  // Sentinels, so a parse that wrongly reports success while leaving the
  // outputs alone is visible rather than passing by coincidence.
  Parsed p{false, -1, -1, -1};
  p.ok = SemVer::parseSemVer(text, p.major, p.minor, p.patch);
  return p;
}

}  // namespace

TEST(SemVerParse, ParsesBareTriple) {
  const Parsed p = parse("1.2.3");
  EXPECT_TRUE(p.ok);
  EXPECT_EQ(p.major, 1);
  EXPECT_EQ(p.minor, 2);
  EXPECT_EQ(p.patch, 3);
}

// The regression this file exists for: GitHub returns the raw tag_name, and
// Almanac's release tags are prefixed because the bare numbers 0.4.0-1.5.0 are
// already taken by inherited CrossPoint tags.
TEST(SemVerParse, SkipsReleaseTagPrefix) {
  const Parsed p = parse("almanac-v1.0.1");
  EXPECT_TRUE(p.ok);
  EXPECT_EQ(p.major, 1);
  EXPECT_EQ(p.minor, 0);
  EXPECT_EQ(p.patch, 1);
}

TEST(SemVerParse, IgnoresBuildSuffixes) {
  for (const char* text : {"1.0.1-slim", "1.0.1-sticky", "1.0.1-rc+a1b2c3d", "1.0.1+build.7"}) {
    const Parsed p = parse(text);
    EXPECT_TRUE(p.ok) << text;
    EXPECT_EQ(p.major, 1) << text;
    EXPECT_EQ(p.minor, 0) << text;
    EXPECT_EQ(p.patch, 1) << text;
  }
}

TEST(SemVerParse, ParsesMultiDigitFields) {
  const Parsed p = parse("almanac-v12.34.56");
  EXPECT_TRUE(p.ok);
  EXPECT_EQ(p.major, 12);
  EXPECT_EQ(p.minor, 34);
  EXPECT_EQ(p.patch, 56);
}

TEST(SemVerParse, RejectsIncompleteOrMalformed) {
  for (const char* text : {"", "almanac", "1", "1.2", "1.2.", "v1.2-3", "almanac-v1-0-1", "1..3", ".1.2"}) {
    const Parsed p = parse(text);
    EXPECT_FALSE(p.ok) << text;
  }
}

TEST(SemVerParse, RejectsNullptr) {
  int major = -1, minor = -1, patch = -1;
  EXPECT_FALSE(SemVer::parseSemVer(nullptr, major, minor, patch));
}

// A failed parse must not touch the caller's variables -- the original bug was
// sscanf leaving them indeterminate and the comparison reading them anyway.
TEST(SemVerParse, LeavesOutputsUntouchedOnFailure) {
  int major = 7, minor = 8, patch = 9;
  EXPECT_FALSE(SemVer::parseSemVer("not-a-version", major, minor, patch));
  EXPECT_EQ(major, 7);
  EXPECT_EQ(minor, 8);
  EXPECT_EQ(patch, 9);
}

TEST(SemVerParse, RejectsAbsurdlyLongField) {
  const Parsed p = parse("1.2.12345678901234567890");
  EXPECT_FALSE(p.ok);
}

TEST(SemVerIsNewer, PrefixedTagBeatsOlderBuild) {
  EXPECT_TRUE(SemVer::isNewer("almanac-v1.0.1", "1.0.0"));
  EXPECT_TRUE(SemVer::isNewer("almanac-v1.1.0", "1.0.9"));
  EXPECT_TRUE(SemVer::isNewer("almanac-v2.0.0", "1.9.9"));
}

TEST(SemVerIsNewer, SameVersionIsNotNewer) {
  EXPECT_FALSE(SemVer::isNewer("almanac-v1.0.0", "1.0.0"));
  EXPECT_FALSE(SemVer::isNewer("almanac-v1.0.0", "1.0.0-slim"));
  EXPECT_FALSE(SemVer::isNewer("almanac-v1.0.0", "1.0.0-sticky"));
}

TEST(SemVerIsNewer, OlderTagIsNotNewer) {
  EXPECT_FALSE(SemVer::isNewer("almanac-v1.0.0", "1.0.1"));
  EXPECT_FALSE(SemVer::isNewer("almanac-v0.9.9", "1.0.0"));
  EXPECT_FALSE(SemVer::isNewer("almanac-v1.0.0", "2.0.0"));
}

TEST(SemVerIsNewer, ReleaseSupersedesItsOwnReleaseCandidate) {
  EXPECT_TRUE(SemVer::isNewer("almanac-v1.0.1", "1.0.1-rc+a1b2c3d"));
  // ...but an RC does not make an older release look newer.
  EXPECT_FALSE(SemVer::isNewer("almanac-v1.0.0", "1.0.1-rc+a1b2c3d"));
}

// An unparseable side must read as "no update" rather than as an update built
// from indeterminate numbers.
TEST(SemVerIsNewer, UnparseableSideIsNotNewer) {
  EXPECT_FALSE(SemVer::isNewer("nightly", "1.0.0"));
  EXPECT_FALSE(SemVer::isNewer("almanac-v1.0.1", "dev-simulator"));
  EXPECT_FALSE(SemVer::isNewer("", "1.0.0"));
  EXPECT_FALSE(SemVer::isNewer(nullptr, "1.0.0"));
  EXPECT_FALSE(SemVer::isNewer("almanac-v1.0.1", nullptr));
}

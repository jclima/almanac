#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "ZipGeocodeParser.h"

namespace {
// Captured verbatim via `curl https://api.zippopotam.us/us/90210` on 2026-08-07.
constexpr char SUCCESS_JSON[] =
    R"({"country": "United States", "country abbreviation": "US", "post code": "90210", )"
    R"("places": [{"place name": "Beverly Hills", "longitude": "-118.4065", "latitude": "34.0901", )"
    R"("state": "California", "state abbreviation": "CA"}]})";

// Captured verbatim via `curl https://api.zippopotam.us/us/00000` on 2026-08-07 (HTTP 404).
constexpr char NOT_FOUND_JSON[] = "{}";

void feedAll(ZipGeocodeParser& p, const char* json) { p.feed(json, strlen(json)); }
}  // namespace

TEST(ZipGeocodeParserTest, ParsesSuccessResponse) {
  ZipGeocodeParser p;
  feedAll(p, SUCCESS_JSON);
  ASSERT_FALSE(p.hasError());
  const auto& r = p.geocode();
  EXPECT_TRUE(r.found);
  EXPECT_DOUBLE_EQ(r.latitude, 34.0901);
  EXPECT_DOUBLE_EQ(r.longitude, -118.4065);
  EXPECT_STREQ(r.placeName, "Beverly Hills");
  EXPECT_STREQ(r.stateAbbrev, "CA");
}

TEST(ZipGeocodeParserTest, EmptyObjectIsNotFoundNotError) {
  ZipGeocodeParser p;
  feedAll(p, NOT_FOUND_JSON);
  EXPECT_FALSE(p.hasError());
  EXPECT_FALSE(p.geocode().found);
}

TEST(ZipGeocodeParserTest, IgnoresIrrelevantTopLevelAndPlaceFields) {
  ZipGeocodeParser p;
  // "country", "country abbreviation", "post code" at top level and "state"
  // (the full name, not the abbreviation) inside the place object are all
  // present in the real payload but never captured -- this just confirms
  // their presence doesn't break parsing of the fields that matter.
  feedAll(p, SUCCESS_JSON);
  ASSERT_FALSE(p.hasError());
  EXPECT_TRUE(p.geocode().found);
}

TEST(ZipGeocodeParserTest, MissingOptionalFieldsNoError) {
  ZipGeocodeParser p;
  constexpr char json[] = R"({"places": [{"latitude": "34.0901", "longitude": "-118.4065"}]})";
  feedAll(p, json);
  ASSERT_FALSE(p.hasError());
  const auto& r = p.geocode();
  EXPECT_TRUE(r.found);
  EXPECT_DOUBLE_EQ(r.latitude, 34.0901);
  EXPECT_DOUBLE_EQ(r.longitude, -118.4065);
  EXPECT_STREQ(r.placeName, "");
  EXPECT_STREQ(r.stateAbbrev, "");
}

TEST(ZipGeocodeParserTest, MissingCoordinatesNotFound) {
  ZipGeocodeParser p;
  // A place object that closes cleanly but never carried lat/lon must not
  // present as found -- guards against writing 0,0 (Null Island) into the
  // home location.
  constexpr char json[] = R"({"places": [{"place name": "X"}]})";
  feedAll(p, json);
  ASSERT_FALSE(p.hasError());
  EXPECT_FALSE(p.geocode().found);
}

TEST(ZipGeocodeParserTest, TruncatedBodyNeverPresentsAsFound) {
  ZipGeocodeParser p;
  // Cuts off mid-object -- places[0] never closes.
  constexpr char json[] = R"({"places": [{"place name": "Beverly Hills", "latitude": "34.0901")";
  feedAll(p, json);
  EXPECT_FALSE(p.geocode().found);
}

TEST(ZipGeocodeParserTest, OverlongValueTruncatesWithoutOverrun) {
  ZipGeocodeParser p;
  const std::string longName(100, 'A');
  const std::string json = R"({"places": [{"place name": ")" + longName + R"(", "latitude": "1", "longitude": "2"}]})";
  feedAll(p, json.c_str());
  ASSERT_FALSE(p.hasError());
  const auto& r = p.geocode();
  EXPECT_TRUE(r.found);
  EXPECT_EQ(strlen(r.placeName), sizeof(r.placeName) - 1);
}

TEST(ZipGeocodeParserTest, ChunkedFeedProducesSameResult) {
  ZipGeocodeParser p;
  const size_t len = strlen(SUCCESS_JSON);
  for (size_t i = 0; i < len; i += 7) {
    const size_t chunk = std::min<size_t>(7, len - i);
    p.feed(SUCCESS_JSON + i, chunk);
  }
  ASSERT_FALSE(p.hasError());
  const auto& r = p.geocode();
  EXPECT_TRUE(r.found);
  EXPECT_DOUBLE_EQ(r.latitude, 34.0901);
  EXPECT_DOUBLE_EQ(r.longitude, -118.4065);
  EXPECT_STREQ(r.placeName, "Beverly Hills");
}

TEST(ZipGeocodeParserTest, ResetClearsPreviousResult) {
  ZipGeocodeParser p;
  feedAll(p, SUCCESS_JSON);
  ASSERT_TRUE(p.geocode().found);
  p.reset();
  EXPECT_FALSE(p.geocode().found);
  EXPECT_DOUBLE_EQ(p.geocode().latitude, 0);
  EXPECT_STREQ(p.geocode().placeName, "");
}

TEST(ZipGeocodeParserTest, SecondPlaceEntryIgnored) {
  ZipGeocodeParser p;
  // A second places[] entry exercises the sawFirstPlace-already-true branch
  // of sOnObjectStart's IN_PLACES case (and its matching decrement in
  // sOnObjectEnd) -- it must be skipped, with the array still closing
  // cleanly, and must not clobber the data already captured from places[0].
  constexpr char json[] = R"({"places": [{"latitude": "34.0901", "longitude": "-118.4065"}, )"
                          R"({"latitude": "40.0", "longitude": "-70.0"}]})";
  feedAll(p, json);
  ASSERT_FALSE(p.hasError());
  const auto& r = p.geocode();
  EXPECT_TRUE(r.found);
  EXPECT_DOUBLE_EQ(r.latitude, 34.0901);
  EXPECT_DOUBLE_EQ(r.longitude, -118.4065);
}

TEST(ZipGeocodeParserTest, NestedContainerInsidePlaceDoesNotCorruptParsing) {
  ZipGeocodeParser p;
  // A nested object inside places[0] exercises the IN_PLACE case's depth++
  // path. The nested string value ("bar") must not be mistaken for one of
  // the tracked fields, and the surrounding lat/lon must still come through
  // correctly once the nested container closes and depth returns to 0.
  constexpr char json[] =
      R"({"places": [{"latitude": "34.0901", "longitude": "-118.4065", "nested": {"foo": "bar"}}]})";
  feedAll(p, json);
  ASSERT_FALSE(p.hasError());
  const auto& r = p.geocode();
  EXPECT_TRUE(r.found);
  EXPECT_DOUBLE_EQ(r.latitude, 34.0901);
  EXPECT_DOUBLE_EQ(r.longitude, -118.4065);
}

TEST(ZipGeocodeParserTest, NonNumericCoordinateNotFound) {
  ZipGeocodeParser p;
  // A non-numeric latitude means strtod() can't fully consume the value;
  // sawLatitude must stay false rather than silently accepting strtod()'s
  // 0.0 fallback as a real coordinate.
  constexpr char json[] = R"({"places": [{"latitude": "abc", "longitude": "2.0"}]})";
  feedAll(p, json);
  ASSERT_FALSE(p.hasError());
  EXPECT_FALSE(p.geocode().found);
}

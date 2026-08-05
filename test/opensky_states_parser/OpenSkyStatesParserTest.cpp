#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "lib/JsonParser/OpenSkyStatesParser.h"

namespace {
// Home location: SFO. Radius covers the nearby match but excludes JFK.
constexpr double HOME_LAT = 37.6213;
constexpr double HOME_LON = -122.3790;
constexpr double RADIUS_MILES = 50.0;

void feedAll(OpenSkyStatesParser& parser, const std::string& json) {
  parser.feed(json.c_str(), json.size());
}
}  // namespace

TEST(OpenSkyStatesParser, DecodesAMatchingAirborneAircraft) {
  const std::string json = R"({
    "time": 1700000000,
    "states": [
      ["a835af","UAL123  ","United States",1699999999,1699999999,-122.2211,37.7213,1000.0,false,90.0,45.0,2.5,null,1050.0,"1200",false,0,0]
    ]
  })";

  OpenSkyStatesParser parser;
  parser.reset(HOME_LAT, HOME_LON, RADIUS_MILES);
  feedAll(parser, json);

  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(parser.matchCount(), 1u);

  const auto& m = parser.matchAt(0);
  EXPECT_STREQ(m.icao24, "a835af");
  EXPECT_STREQ(m.callsign, "UAL123");  // trailing spaces trimmed
  EXPECT_STREQ(m.originCountry, "United States");
  ASSERT_TRUE(m.hasAltitudeFeet);
  EXPECT_NEAR(m.altitudeFeet, 1050.0 * 3.28084, 1.0);  // prefers geo_altitude
  ASSERT_TRUE(m.hasSpeedMph);
  EXPECT_NEAR(m.speedMph, 90.0 * 2.23694, 1.0);
  ASSERT_TRUE(m.hasHeading);
  EXPECT_EQ(m.headingDeg, 45);
  ASSERT_TRUE(m.hasVerticalRate);
  EXPECT_NEAR(m.verticalRateMs, 2.5, 0.01);
  EXPECT_GT(m.distanceMiles, 0.0);
  EXPECT_LT(m.distanceMiles, RADIUS_MILES);
}

TEST(OpenSkyStatesParser, SkipsOnGroundAndNullCallsignDoesNotDesyncFields) {
  const std::string json = R"({
    "time": 1700000000,
    "states": [
      ["b123cd",null,"United States",1699999999,1699999999,-122.3890,37.6150,50.0,true,0.0,0.0,0.0,null,60.0,"1201",false,0,0]
    ]
  })";

  OpenSkyStatesParser parser;
  parser.reset(HOME_LAT, HOME_LON, RADIUS_MILES);
  feedAll(parser, json);

  ASSERT_FALSE(parser.hasError());
  EXPECT_EQ(parser.matchCount(), 0u);  // on_ground=true -> excluded
}

TEST(OpenSkyStatesParser, SkipsAircraftWithNullPosition) {
  const std::string json = R"({
    "time": 1700000000,
    "states": [
      ["c456ef","NOPOS   ","United States",null,1699999999,null,null,null,false,120.0,180.0,0.0,null,null,null,false,0,0]
    ]
  })";

  OpenSkyStatesParser parser;
  parser.reset(HOME_LAT, HOME_LON, RADIUS_MILES);
  feedAll(parser, json);

  ASSERT_FALSE(parser.hasError());
  EXPECT_EQ(parser.matchCount(), 0u);  // no lat/lon -> excluded
}

TEST(OpenSkyStatesParser, SkipsAircraftOutsideRadius) {
  const std::string json = R"({
    "time": 1700000000,
    "states": [
      ["d789gh","JBU456  ","United States",1699999999,1699999999,-73.7781,40.6413,3000.0,false,200.0,270.0,-1.0,null,3100.0,"1202",false,0,0]
    ]
  })";

  OpenSkyStatesParser parser;
  parser.reset(HOME_LAT, HOME_LON, RADIUS_MILES);  // JFK is ~2570mi from SFO
  feedAll(parser, json);

  ASSERT_FALSE(parser.hasError());
  EXPECT_EQ(parser.matchCount(), 0u);
}

TEST(OpenSkyStatesParser, MixedRowsOnlyKeepsTheValidMatch) {
  const std::string json = R"({
    "time": 1700000000,
    "states": [
      ["a835af","UAL123  ","United States",1699999999,1699999999,-122.2211,37.7213,1000.0,false,90.0,45.0,2.5,null,1050.0,"1200",false,0,0],
      ["b123cd",null,"United States",1699999999,1699999999,-122.3890,37.6150,50.0,true,0.0,0.0,0.0,null,60.0,"1201",false,0,0],
      ["c456ef","NOPOS   ","United States",null,1699999999,null,null,null,false,120.0,180.0,0.0,null,null,null,false,0,0],
      ["d789gh","JBU456  ","United States",1699999999,1699999999,-73.7781,40.6413,3000.0,false,200.0,270.0,-1.0,null,3100.0,"1202",false,0,0]
    ]
  })";

  OpenSkyStatesParser parser;
  parser.reset(HOME_LAT, HOME_LON, RADIUS_MILES);
  feedAll(parser, json);

  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(parser.matchCount(), 1u);
  EXPECT_STREQ(parser.matchAt(0).icao24, "a835af");
}

TEST(OpenSkyStatesParser, FeedInMultipleChunksProducesSameResult) {
  const std::string json = R"({
    "time": 1700000000,
    "states": [
      ["a835af","UAL123  ","United States",1699999999,1699999999,-122.2211,37.7213,1000.0,false,90.0,45.0,2.5,null,1050.0,"1200",false,0,0]
    ]
  })";

  OpenSkyStatesParser parser;
  parser.reset(HOME_LAT, HOME_LON, RADIUS_MILES);
  // Split mid-token to make sure chunk boundaries don't lose data.
  const size_t mid = json.size() / 2;
  parser.feed(json.c_str(), mid);
  parser.feed(json.c_str() + mid, json.size() - mid);

  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(parser.matchCount(), 1u);
  EXPECT_STREQ(parser.matchAt(0).icao24, "a835af");
}

TEST(OpenSkyStatesParser, KeepsOnlyClosestMaxMatchesSorted) {
  // Build MAX_MATCHES + 5 aircraft along the same meridian, spaced 1 degree
  // (~69mi) apart, well within a huge radius -- all should be candidates,
  // but only the MAX_MATCHES closest survive, sorted ascending by distance.
  std::string json = R"({"time": 1700000000, "states": [)";
  const int totalAircraft = static_cast<int>(OpenSkyStatesParser::MAX_MATCHES) + 5;
  for (int i = 0; i < totalAircraft; ++i) {
    char row[256];
    const double lat = HOME_LAT + 0.01 * (i + 1);  // each ~0.69mi further than the last
    snprintf(row, sizeof(row),
             "[\"%06x\",\"FLT%03d  \",\"United States\",1699999999,1699999999,%.4f,%.4f,1000.0,false,90.0,0.0,0.0,"
             "null,1050.0,\"1200\",false,0,0]",
             i, i, HOME_LON, lat);
    json += row;
    if (i + 1 < totalAircraft) json += ",";
  }
  json += "]}";

  OpenSkyStatesParser parser;
  parser.reset(HOME_LAT, HOME_LON, 1000.0);  // huge radius: every aircraft is a candidate
  feedAll(parser, json);

  ASSERT_FALSE(parser.hasError());
  EXPECT_EQ(parser.matchCount(), OpenSkyStatesParser::MAX_MATCHES);

  for (size_t i = 1; i < parser.matchCount(); ++i) {
    EXPECT_LE(parser.matchAt(i - 1).distanceMiles, parser.matchAt(i).distanceMiles);
  }
  // The closest aircraft (index 0, lat offset 0.01) must be kept.
  EXPECT_STREQ(parser.matchAt(0).icao24, "000000");
}

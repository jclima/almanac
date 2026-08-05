#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "lib/JsonParser/AircraftInfoParser.h"

namespace {
// Captured verbatim from api.adsbdb.com/v0/aircraft/a25b3f on 2026-08-05.
const char* const REAL_SUCCESS = R"({"response":{"aircraft":{
"type":"737NG 990ER/W","icao_type":"B739","manufacturer":"Boeing",
"mode_s":"A25B3F","registration":"N251AK","registered_owner_country_iso_name":"US",
"registered_owner_country_name":"United States","registered_owner_operator_flag_code":"ASA",
"registered_owner":"Alaska Airlines",
"url_photo":"https://airport-data.com/images/aircraft/001/419/001419608.jpg",
"url_photo_thumbnail":"https://airport-data.com/images/aircraft/thumbnails/001/419/001419608.jpg"}}})";

// The not-found shape: `response` is a STRING here, not an object.
const char* const REAL_NOT_FOUND = R"({"response":"unknown aircraft"})";

void feedAll(AircraftInfoParser& p, const char* json) { p.feed(json, strlen(json)); }
}  // namespace

TEST(AircraftInfoParser, ParsesRealSuccessPayload) {
  AircraftInfoParser p;
  p.reset();
  feedAll(p, REAL_SUCCESS);

  ASSERT_FALSE(p.hasError());
  ASSERT_TRUE(p.info().found);
  EXPECT_STREQ(p.info().manufacturer, "Boeing");
  EXPECT_STREQ(p.info().icaoType, "B739");
  EXPECT_STREQ(p.info().registration, "N251AK");
}

TEST(AircraftInfoParser, UnknownAircraftStringIsNotFoundButNotAnError) {
  AircraftInfoParser p;
  p.reset();
  feedAll(p, REAL_NOT_FOUND);

  EXPECT_FALSE(p.hasError());  // a missing record is a normal outcome
  EXPECT_FALSE(p.info().found);
  EXPECT_STREQ(p.info().manufacturer, "");
  EXPECT_STREQ(p.info().icaoType, "");
}

TEST(AircraftInfoParser, IgnoresTheVerboseTypeFieldAndUsesIcaoType) {
  // "type" is verbose and inconsistent ("737NG 990ER/W"); icao_type is the
  // clean 4-char designator. Only the latter should land in icaoType.
  AircraftInfoParser p;
  p.reset();
  feedAll(p, REAL_SUCCESS);

  ASSERT_TRUE(p.info().found);
  EXPECT_STREQ(p.info().icaoType, "B739");
}

TEST(AircraftInfoParser, MissingFieldsLeaveEmptyStringsWithoutError) {
  AircraftInfoParser p;
  p.reset();
  feedAll(p, R"({"response":{"aircraft":{"icao_type":"A320"}}})");

  EXPECT_FALSE(p.hasError());
  EXPECT_TRUE(p.info().found);
  EXPECT_STREQ(p.info().icaoType, "A320");
  EXPECT_STREQ(p.info().manufacturer, "");
  EXPECT_STREQ(p.info().registration, "");
}

TEST(AircraftInfoParser, TruncatedBodyDoesNotReportFound) {
  AircraftInfoParser p;
  p.reset();
  feedAll(p, R"({"response":{"aircraft":{"manufacturer":"Boei)");

  // Whether the streaming parser flags an error here is its business; what
  // matters is that a half-delivered record is never presented as complete.
  EXPECT_FALSE(p.info().found);
}

TEST(AircraftInfoParser, OverlongValuesTruncateWithoutOverrun) {
  AircraftInfoParser p;
  p.reset();
  std::string json = R"({"response":{"aircraft":{"manufacturer":")";
  json += std::string(200, 'X');
  json += R"(","icao_type":"B739"}}})";
  feedAll(p, json.c_str());

  EXPECT_FALSE(p.hasError());
  // Fits the fixed buffer and stays NUL-terminated.
  EXPECT_LT(strlen(p.info().manufacturer), sizeof(AircraftInfo::manufacturer));
  EXPECT_EQ(p.info().manufacturer[sizeof(AircraftInfo::manufacturer) - 1], '\0');
}

TEST(AircraftInfoParser, FeedInChunksProducesSameResult) {
  AircraftInfoParser p;
  p.reset();
  const std::string json(REAL_SUCCESS);
  const size_t mid = json.size() / 2;
  p.feed(json.c_str(), mid);
  p.feed(json.c_str() + mid, json.size() - mid);

  ASSERT_FALSE(p.hasError());
  ASSERT_TRUE(p.info().found);
  EXPECT_STREQ(p.info().icaoType, "B739");
}

TEST(AircraftInfoParser, ResetClearsPreviousResult) {
  AircraftInfoParser p;
  p.reset();
  feedAll(p, REAL_SUCCESS);
  ASSERT_TRUE(p.info().found);

  p.reset();
  EXPECT_FALSE(p.info().found);
  EXPECT_STREQ(p.info().icaoType, "");

  feedAll(p, REAL_NOT_FOUND);
  EXPECT_FALSE(p.info().found);
}

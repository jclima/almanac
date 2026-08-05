#pragma once

#include <cstddef>
#include <cstdint>

#include "StreamingJsonParser.h"

// One matched aircraft: within the configured radius, airborne, position known.
struct FlightMatch {
  char icao24[7] = {0};
  char callsign[9] = {0};  // trimmed; may be empty if OpenSky has no callsign
  char originCountry[32] = {0};
  double latitude = 0;
  double longitude = 0;
  bool hasAltitudeFeet = false;
  int32_t altitudeFeet = 0;  // prefers geo_altitude, falls back to baro_altitude
  bool hasSpeedMph = false;
  int32_t speedMph = 0;
  bool hasHeading = false;
  int32_t headingDeg = 0;  // 0-359, true track
  bool hasVerticalRate = false;
  float verticalRateMs = 0;  // raw m/s; sign gives climb/level/descend
  double distanceMiles = 0;
  double bearingDeg = 0;  // initial bearing from home location, 0-359
};

// Streams OpenSky's /api/states/all response and keeps the MAX_MATCHES closest
// airborne aircraft within a radius of a configured home location, sorted by
// distance ascending. Memory use is fixed regardless of response size: one
// scratch row plus a fixed MAX_MATCHES-entry result array -- important on a
// device with ~380KB RAM and no PSRAM, where a busy airspace could otherwise
// mean an unbounded aircraft list.
class OpenSkyStatesParser {
 public:
  static constexpr size_t MAX_MATCHES = 20;

  OpenSkyStatesParser();

  OpenSkyStatesParser(const OpenSkyStatesParser&) = delete;
  OpenSkyStatesParser& operator=(const OpenSkyStatesParser&) = delete;

  // Resets all state and configures the search. Call before each fetch.
  void reset(double homeLatitude, double homeLongitude, double radiusMiles);

  // Feeds a chunk of the HTTP response body. Safe to call repeatedly as data streams in.
  void feed(const char* data, size_t len);

  bool hasError() const { return parser.hasError(); }

  size_t matchCount() const { return matchCount_; }
  const FlightMatch& matchAt(size_t index) const { return matches[index]; }

 private:
  struct RowScratch {
    char icao24[7] = {0};
    char callsign[9] = {0};
    char originCountry[32] = {0};
    bool hasLat = false;
    bool hasLon = false;
    double lat = 0;
    double lon = 0;
    bool onGround = false;
    bool hasBaroAlt = false;
    float baroAltM = 0;
    bool hasGeoAlt = false;
    float geoAltM = 0;
    bool hasVelocity = false;
    float velocityMs = 0;
    bool hasTrueTrack = false;
    float trueTrackDeg = 0;
    bool hasVerticalRate = false;
    float verticalRateMs = 0;
  };

  enum class Position : uint8_t { AWAITING_STATES, IN_STATES_ARRAY, IN_ROW };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  void commitRow();
  void insertSorted(const FlightMatch& match);

  StreamingJsonParser parser;

  Position position = Position::AWAITING_STATES;
  bool expectStatesArray = false;
  uint8_t fieldIndex = 0;
  uint8_t nestedArrayDepth = 0;
  RowScratch scratch;

  double homeLat = 0;
  double homeLon = 0;
  double radiusMiles = 0;

  FlightMatch matches[MAX_MATCHES];
  size_t matchCount_ = 0;
};

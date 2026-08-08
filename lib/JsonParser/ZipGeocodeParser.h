#pragma once

#include <cstddef>
#include <cstdint>

#include "StreamingJsonParser.h"

// One zip code's geocoded location, as returned by api.zippopotam.us.
struct ZipGeocodeResult {
  bool found = false;
  double latitude = 0;
  double longitude = 0;
  char placeName[32] = {0};   // e.g. "Beverly Hills" -- for the confirmation message
  char stateAbbrev[4] = {0};  // e.g. "CA"
};

// Streams api.zippopotam.us's /us/<zip> response.
//
// The endpoint returns {"places":[{...}], ...} on success; an unknown zip
// answers HTTP 404 with an empty `{}` body, which this parser also handles
// cleanly (found stays false, no error). Only malformed JSON sets hasError().
//
// Load-bearing detail: `latitude`/`longitude` are JSON *strings*
// ("latitude": "34.0901"), not numbers -- consumed via the string callback
// and converted with strtod, not the number callback. Object keys in this
// API also contain spaces ("place name", "state abbreviation") -- ordinary,
// valid JSON keys, matched verbatim.
//
// Fixed-size buffers throughout: no allocation. Same shape as
// AircraftInfoParser -- dominated by the embedded StreamingJsonParser (a
// 512-byte token buffer plus a 32-byte nesting stack); this class's own
// fields (ZipGeocodeResult + a few bytes of parse-position state) add well
// under 100 bytes on top of that.
class ZipGeocodeParser {
 public:
  ZipGeocodeParser();

  ZipGeocodeParser(const ZipGeocodeParser&) = delete;
  ZipGeocodeParser& operator=(const ZipGeocodeParser&) = delete;

  // Clears all state. Call before each lookup.
  void reset();

  // Feeds a chunk of the HTTP response body.
  void feed(const char* data, size_t len);

  bool hasError() const { return parser.hasError(); }

  const ZipGeocodeResult& geocode() const { return result; }

 private:
  enum class Position : uint8_t { TOP_LEVEL, IN_PLACES, IN_PLACE };
  enum class LastKey : uint8_t { NONE, PLACES, PLACE_NAME, LONGITUDE, LATITUDE, STATE_ABBREV };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  StreamingJsonParser parser;

  Position position = Position::TOP_LEVEL;
  LastKey lastKey = LastKey::NONE;
  uint8_t depth = 0;
  bool sawFirstPlace = false;  // true once places[0] has been entered -- later array entries are ignored
  bool sawLatitude = false;    // both required for `found` -- see sOnObjectEnd
  bool sawLongitude = false;

  ZipGeocodeResult result;
};

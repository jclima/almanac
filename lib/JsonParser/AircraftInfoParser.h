#pragma once

#include <cstddef>
#include <cstdint>

#include "StreamingJsonParser.h"

// One aircraft's registry record, as returned by adsbdb.com.
struct AircraftInfo {
  char icao24[7] = {0};
  char manufacturer[24] = {0};
  char icaoType[8] = {0};       // clean 4-char ICAO designator, e.g. "B739"
  char registration[12] = {0};  // tail number, e.g. "N251AK"
  bool found = false;
};

// Streams adsbdb.com's /v0/aircraft/<icao24> response.
//
// The endpoint returns {"response":{"aircraft":{...}}} on success but
// {"response":"unknown aircraft"} when it has no record -- `response` changes
// from an object to a string. A missing record sets found=false and is NOT an
// error; only malformed JSON sets hasError().
//
// Fixed-size buffers throughout: no allocation, ~54 bytes of state.
class AircraftInfoParser {
 public:
  AircraftInfoParser();

  AircraftInfoParser(const AircraftInfoParser&) = delete;
  AircraftInfoParser& operator=(const AircraftInfoParser&) = delete;

  // Clears all state. Call before each lookup.
  void reset();

  // Feeds a chunk of the HTTP response body.
  void feed(const char* data, size_t len);

  bool hasError() const { return parser.hasError(); }

  const AircraftInfo& info() const { return result; }

 private:
  enum class Position : uint8_t { TOP_LEVEL, IN_RESPONSE, IN_AIRCRAFT };
  enum class LastKey : uint8_t { NONE, RESPONSE, AIRCRAFT, MANUFACTURER, ICAO_TYPE, REGISTRATION };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);

  StreamingJsonParser parser;

  Position position = Position::TOP_LEVEL;
  LastKey lastKey = LastKey::NONE;
  uint8_t depth = 0;
  bool sawAircraftObject = false;

  AircraftInfo result;
};

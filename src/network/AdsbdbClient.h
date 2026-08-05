#pragma once

#include "AircraftInfoParser.h"

// Looks up one aircraft's registry record (manufacturer, ICAO type,
// registration) from adsbdb.com's free, keyless API.
//
// Deliberately one aircraft at a time, called only when a detail screen opens:
// looking up a whole 20-match list would mean 20 sequential TLS handshakes,
// each peaking ~36KB transient heap (measured on device).
class AdsbdbClient {
 public:
  // parser must already be reset() before calling. Returns false on
  // HTTP/transport failure; check parser.hasError() for a JSON parse failure
  // and parser.info().found for "no record" (a normal outcome, not an error).
  static bool fetchAircraftInfo(const char* icao24, AircraftInfoParser& parser);
};

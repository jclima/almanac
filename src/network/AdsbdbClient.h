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
  enum class Result {
    // HTTP 200. Check parser.hasError() for a JSON parse failure and
    // parser.info().found for a "found" record with no useful fields (both
    // normal outcomes, not errors).
    Ok,
    // adsbdb answered HTTP 404 ({"response":"unknown aircraft"}): it has no
    // record for this icao24. A normal, expected outcome on every ordinary
    // miss -- not a transport or parse failure. parser is left in its
    // reset() state (the 404 body is never fed to it).
    NotFound,
    // Transport/HTTP failure: DNS, TLS, timeout, or any status other than
    // 200/404. parser is left in its reset() state.
    Error,
  };

  // parser must already be reset() before calling.
  static Result fetchAircraftInfo(const char* icao24, AircraftInfoParser& parser);
};

#pragma once

#include "ZipGeocodeParser.h"

// Geocodes a 5-digit US zip code to a lat/lon via api.zippopotam.us's free,
// keyless API.
class ZipGeocodeClient {
 public:
  enum class Result {
    // HTTP 200. Check parser.hasError() for a JSON parse failure and
    // parser.geocode().found for a "found" record (should always be true on
    // a 200 for this API, but the parser makes no such assumption).
    Ok,
    // Zippopotam answered HTTP 404 (an empty `{}` body): no record for this
    // zip. A normal, expected outcome for a well-formed but nonexistent zip
    // -- not a transport or parse failure. parser is left in its reset()
    // state (the 404 body is never fed to it).
    NotFound,
    // Transport/HTTP failure: DNS, TLS, timeout, or any status other than
    // 200/404. parser is left in its reset() state -- except when a
    // malformed-JSON byte mid-stream aborts an otherwise-200 response; then
    // parser holds its partial data with hasError()==true instead. Callers
    // that need to tell the two apart (e.g.
    // FlightTrackerSettingsActivity::performZipLookup()) check
    // parser.hasError().
    Error,
  };

  // zip must be exactly 5 ASCII digits (validated by the caller). parser
  // must already be reset() before calling.
  static Result geocode(const char* zip, ZipGeocodeParser& parser);
};

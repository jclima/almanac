#pragma once

#include "OpenSkyStatesParser.h"

// Fetches aircraft near a home location from OpenSky Network's free,
// anonymous /api/states/all endpoint and streams them into a parser.
class OpenSkyClient {
 public:
  // parser must already be reset() with the same home location/radius before
  // calling. Returns false on HTTP/transport failure; check parser.hasError()
  // separately for JSON parse failures (a false return does not distinguish
  // the two -- see HttpDownloader::fetchUrl's DataCallback overload).
  static bool fetchNearby(double homeLat, double homeLon, double radiusMiles, OpenSkyStatesParser& parser);
};

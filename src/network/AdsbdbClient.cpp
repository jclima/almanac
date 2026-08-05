#include "AdsbdbClient.h"

#include <Logging.h>

#include <cstdio>
#include <string>

#include "network/HttpDownloader.h"

AdsbdbClient::Result AdsbdbClient::fetchAircraftInfo(const char* icao24, AircraftInfoParser& parser) {
  if (!icao24 || icao24[0] == '\0') {
    LOG_ERR("ADSBDB", "fetchAircraftInfo called with an empty icao24");
    return Result::Error;
  }

  char url[80];
  snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/aircraft/%s", icao24);

  LOG_DBG("ADSBDB", "Fetching: %s", url);

  int status = 0;
  const bool ok = HttpDownloader::fetchUrl(
      std::string(url),
      [&parser](const uint8_t* data, size_t len) {
        parser.feed(reinterpret_cast<const char*>(data), len);
        return !parser.hasError();
      },
      "", "", &status);

  if (ok) return Result::Ok;

  if (status == 404) {
    // adsbdb's documented "no record" response -- a normal outcome that
    // happens on every ordinary miss, not an error.
    LOG_DBG("ADSBDB", "No record for %s (HTTP 404)", icao24);
    return Result::NotFound;
  }
  return Result::Error;
}

#include "AdsbdbClient.h"

#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "network/HttpDownloader.h"

bool AdsbdbClient::fetchAircraftInfo(const char* icao24, AircraftInfoParser& parser) {
  if (!icao24 || icao24[0] == '\0') {
    LOG_ERR("ADSBDB", "fetchAircraftInfo called with an empty icao24");
    return false;
  }

  char url[80];
  snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/aircraft/%s", icao24);

  LOG_DBG("ADSBDB", "Fetching: %s", url);

  return HttpDownloader::fetchUrl(std::string(url), [&parser](const uint8_t* data, size_t len) {
    parser.feed(reinterpret_cast<const char*>(data), len);
    return !parser.hasError();
  });
}

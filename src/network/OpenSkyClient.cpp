#include "OpenSkyClient.h"

#include <Logging.h>

#include <cstdio>
#include <string>

#include "GeoMath.h"
#include "network/HttpDownloader.h"

bool OpenSkyClient::fetchNearby(const double homeLat, const double homeLon, const double radiusMiles,
                                OpenSkyStatesParser& parser) {
  const auto box = GeoMath::computeBoundingBox(homeLat, homeLon, radiusMiles);

  char url[192];
  snprintf(url, sizeof(url), "https://opensky-network.org/api/states/all?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f",
           box.latMin, box.lonMin, box.latMax, box.lonMax);

  LOG_DBG("OPENSKY", "Fetching: %s", url);

  return HttpDownloader::fetchUrl(std::string(url), [&parser](const uint8_t* data, size_t len) {
    parser.feed(reinterpret_cast<const char*>(data), len);
    return !parser.hasError();
  });
}

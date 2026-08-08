#include "ZipGeocodeClient.h"

#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "network/HttpDownloader.h"

ZipGeocodeClient::Result ZipGeocodeClient::geocode(const char* zip, ZipGeocodeParser& parser) {
  if (!zip || strlen(zip) != 5) {
    LOG_ERR("ZIPGEO", "geocode called with a malformed zip");
    return Result::Error;
  }

  char url[64];
  snprintf(url, sizeof(url), "https://api.zippopotam.us/us/%s", zip);

  LOG_DBG("ZIPGEO", "Fetching: %s", url);

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
    // Zippopotam's documented "no record" response -- a normal outcome for
    // a well-formed but nonexistent zip, not an error.
    LOG_DBG("ZIPGEO", "No record for zip %s (HTTP 404)", zip);
    return Result::NotFound;
  }
  return Result::Error;
}

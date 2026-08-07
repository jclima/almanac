#include "TesseraeClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <WiFi.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include "AlmanacSettings.h"
#include "WifiCredentialStore.h"
#include "network/HttpDownloader.h"

namespace {
constexpr uint32_t HTTP_TIMEOUT_MS = 15000;

struct HttpResponse {
  int status = -1;
  std::string body;
};

void copySetting(char* destination, const size_t capacity, const std::string& value) {
  if (capacity == 0) return;
  const size_t count = std::min(capacity - 1, value.size());
  memcpy(destination, value.data(), count);
  destination[count] = '\0';
}

std::string serverUrl() {
  std::string normalized;
  return TesseraeClient::normalizeServerUrl(SETTINGS.tesseraeServerUrl, normalized) ? normalized : "";
}

std::string apiUrl(const std::string& path) {
  const std::string base = serverUrl();
  return base.empty() ? "" : base + path;
}

std::string macAddress() {
  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  char value[18];
  snprintf(value, sizeof(value), "%02x:%02x:%02x:%02x:%02x:%02x", static_cast<unsigned>(mac[0]),
           static_cast<unsigned>(mac[1]), static_cast<unsigned>(mac[2]), static_cast<unsigned>(mac[3]),
           static_cast<unsigned>(mac[4]), static_cast<unsigned>(mac[5]));
  return value;
}

std::string identityBody(const uint16_t panelWidth, const uint16_t panelHeight) {
  JsonDocument doc;
  doc["device_id"] = TesseraeClient::resolvedDeviceId();
  std::string kind = gpio.deviceIsX3() ? "xteink_x3" : "xteink_x4";
  if (SETTINGS.tesseraeGrayscale) kind += "_gray";
  doc["kind"] = kind;
  doc["panel_w"] = panelWidth;
  doc["panel_h"] = panelHeight;
  doc["fw_version"] = ALMANAC_VERSION;
  doc["mac"] = macAddress();
  std::string body;
  serializeJson(doc, body);
  return body;
}

HttpResponse request(const char* method, const std::string& url, const std::string& body = "",
                     const std::string& bearerToken = "", const std::string& pairingCode = "") {
  HttpResponse response;
  freeink::SecureHttpClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  // SecureNet does not yet have the platform CA bundle wired into wolfSSL.
  // This matches the firmware's existing HTTPS clients; plain HTTP remains
  // useful for Tesserae servers on a trusted local network.
  http.setInsecure();
  http.setUserAgent("Almanac-ESP32-" ALMANAC_VERSION);
  if (!http.begin(url)) {
    LOG_ERR("TESS", "Invalid URL: %s", url.c_str());
    return response;
  }
  http.addHeader("Accept", "application/json");
  if (!body.empty()) http.addHeader("Content-Type", "application/json");
  if (!bearerToken.empty()) http.addHeader("Authorization", "Bearer " + bearerToken);
  if (!pairingCode.empty()) http.addHeader("X-Pairing-Code", pairingCode);

  LOG_DBG("TESS", "%s %s", method, url.c_str());
  response.status = body.empty() && strcmp(method, "GET") == 0 ? http.GET() : http.sendRequest(method, body);
  if (response.status > 0 && http.responseComplete()) {
    response.body = http.getString();
  } else if (response.status > 0) {
    LOG_ERR("TESS", "Incomplete HTTP response: %d", response.status);
    response.status = -1;
  }
  http.end();
  return response;
}

TesseraeClient::Result mapFailure(const int status) {
  if (status == 401 || status == 403) return TesseraeClient::Result::Unauthorized;
  return status <= 0 ? TesseraeClient::Result::NetworkError : TesseraeClient::Result::InvalidResponse;
}

bool storeRegistration(JsonVariantConst response) {
  const char* token = response["device_token"].is<const char*>() ? response["device_token"].as<const char*>() : "";
  const char* id = response["device_id"].is<const char*>() ? response["device_id"].as<const char*>() : "";
  if (!token[0]) return false;
  if (strlen(token) >= sizeof(SETTINGS.tesseraeDeviceToken)) return false;
  if (id[0] && strlen(id) >= sizeof(SETTINGS.tesseraeDeviceId)) return false;

  copySetting(SETTINGS.tesseraeDeviceToken, sizeof(SETTINGS.tesseraeDeviceToken), token);
  if (id[0]) copySetting(SETTINGS.tesseraeDeviceId, sizeof(SETTINGS.tesseraeDeviceId), id);
  SETTINGS.saveToFile();
  return true;
}
}  // namespace

bool TesseraeClient::normalizeServerUrl(const std::string& input, std::string& normalized) {
  size_t first = 0;
  while (first < input.size() && std::isspace(static_cast<unsigned char>(input[first]))) ++first;
  size_t last = input.size();
  while (last > first && std::isspace(static_cast<unsigned char>(input[last - 1]))) --last;
  normalized = input.substr(first, last - first);

  const bool isHttp = normalized.rfind("http://", 0) == 0;
  const bool isHttps = normalized.rfind("https://", 0) == 0;
  if (!isHttp && !isHttps) {
    normalized.clear();
    return false;
  }
  const size_t authorityStart = isHttps ? 8 : 7;
  if (normalized.size() <= authorityStart || normalized[authorityStart] == '/') {
    normalized.clear();
    return false;
  }
  if (normalized.find('#', authorityStart) != std::string::npos ||
      normalized.find('?', authorityStart) != std::string::npos) {
    normalized.clear();
    return false;
  }
  while (normalized.size() > authorityStart && normalized.back() == '/') normalized.pop_back();
  return true;
}

bool TesseraeClient::isConfigured() { return !serverUrl().empty(); }

bool TesseraeClient::isRegistered() { return SETTINGS.tesseraeDeviceToken[0] != '\0'; }

std::string TesseraeClient::resolvedDeviceId() {
  if (SETTINGS.tesseraeDeviceId[0]) return SETTINGS.tesseraeDeviceId;

  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  char generated[33];
  snprintf(generated, sizeof(generated), "almanac-%s-%02x%02x%02x%02x%02x%02x", gpio.deviceIsX3() ? "x3" : "x4",
           static_cast<unsigned>(mac[0]), static_cast<unsigned>(mac[1]), static_cast<unsigned>(mac[2]),
           static_cast<unsigned>(mac[3]), static_cast<unsigned>(mac[4]), static_cast<unsigned>(mac[5]));
  return generated;
}

TesseraeClient::Result TesseraeClient::connectSavedWifi(const uint32_t timeoutMs) {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) return Result::Ok;

  const WifiCredential* credential = nullptr;
  const std::string& lastSsid = WIFI_STORE.getLastConnectedSsid();
  if (!lastSsid.empty()) credential = WIFI_STORE.findCredential(lastSsid);
  if (!credential && WIFI_STORE.getCredentials().size() == 1) credential = &WIFI_STORE.getCredentials().front();
  if (!credential) {
    LOG_DBG("TESS", "No saved Wi-Fi network available");
    return Result::NoWifi;
  }

  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(50);
  if (credential->password.empty()) {
    WiFi.begin(credential->ssid.c_str());
  } else {
    WiFi.begin(credential->ssid.c_str(), credential->password.c_str());
  }

  const unsigned long startedAt = millis();
  while (millis() - startedAt < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
      WIFI_STORE.setLastConnectedSsid(credential->ssid);
      return Result::Ok;
    }
    delay(100);
  }
  WiFi.disconnect(false);
  LOG_ERR("TESS", "Saved Wi-Fi connection timed out");
  return Result::NoWifi;
}

TesseraeClient::Result TesseraeClient::discover(const uint16_t panelWidth, const uint16_t panelHeight) {
  const std::string url = apiUrl("/api/v1/device/discover");
  if (url.empty()) return Result::NotConfigured;
  if (WiFi.status() != WL_CONNECTED) return Result::NoWifi;

  const HttpResponse response = request("POST", url, identityBody(panelWidth, panelHeight));
  if (response.status != 200) return mapFailure(response.status);

  JsonDocument doc;
  if (deserializeJson(doc, response.body)) return Result::InvalidResponse;
  if (!(doc["registered"] | false)) return Result::WaitingApproval;
  return storeRegistration(doc.as<JsonVariantConst>()) ? Result::Ok : Result::InvalidResponse;
}

TesseraeClient::Result TesseraeClient::registerWithCode(const std::string& pairingCode, const uint16_t panelWidth,
                                                        const uint16_t panelHeight) {
  const std::string url = apiUrl("/api/v1/device/register");
  if (url.empty()) return Result::NotConfigured;
  if (WiFi.status() != WL_CONNECTED) return Result::NoWifi;

  const HttpResponse response = request("POST", url, identityBody(panelWidth, panelHeight), "", pairingCode);
  if (response.status != 200 && response.status != 201) return mapFailure(response.status);

  JsonDocument doc;
  if (deserializeJson(doc, response.body)) return Result::InvalidResponse;
  return storeRegistration(doc.as<JsonVariantConst>()) ? Result::Ok : Result::InvalidResponse;
}

TesseraeClient::Result TesseraeClient::fetchFrame(FrameInfo& frame) {
  if (!isRegistered()) return Result::Unauthorized;
  const std::string url = apiUrl("/api/v1/device/" + resolvedDeviceId() + "/frame");
  if (url.empty()) return Result::NotConfigured;
  if (WiFi.status() != WL_CONNECTED) return Result::NoWifi;

  const HttpResponse response = request("GET", url, "", SETTINGS.tesseraeDeviceToken);
  if (response.status == 204 || response.status == 304) return Result::NoFrame;
  if (response.status == 401 || response.status == 403) {
    clearRegistration();
    return Result::Unauthorized;
  }
  if (response.status != 200) return mapFailure(response.status);

  JsonDocument doc;
  if (deserializeJson(doc, response.body)) return Result::InvalidResponse;
  const char* rawUrl = doc["url"].is<const char*>() ? doc["url"].as<const char*>() : "";
  const char* format = doc["format"].is<const char*>() ? doc["format"].as<const char*>() : "";
  if (!rawUrl[0] || strcmp(format, "bin") != 0) return Result::InvalidResponse;

  std::string resolved;
  if (!freeink::SecureHttpClient::resolveUrl(serverUrl() + "/", rawUrl, resolved)) return Result::InvalidResponse;
  frame.url = std::move(resolved);
  frame.panelWidth = doc["panel_w"] | 0;
  frame.panelHeight = doc["panel_h"] | 0;
  return Result::Ok;
}

TesseraeClient::Result TesseraeClient::downloadFrame(const FrameInfo& frame, const std::string& destination,
                                                     const size_t expectedBytes) {
  if (Storage.exists(destination.c_str())) Storage.remove(destination.c_str());
  HalFile file;
  if (!Storage.openFileForWrite("TESS", destination, file)) return Result::DownloadFailed;

  size_t received = 0;
  int status = 0;
  const bool ok = HttpDownloader::fetchUrl(
      frame.url,
      [&file, &received, expectedBytes](const uint8_t* data, const size_t length) {
        if (length > expectedBytes - received) return false;
        if (file.write(data, length) != length) return false;
        received += length;
        return true;
      },
      "", "", &status);
  file.close();
  if (!ok || status != 200 || received != expectedBytes) {
    Storage.remove(destination.c_str());
    LOG_ERR("TESS", "Frame body mismatch: HTTP %d, got %zu of %zu bytes", status, received, expectedBytes);
    return Result::DownloadFailed;
  }
  return Result::Ok;
}

TesseraeClient::Result TesseraeClient::postStatus(const uint16_t panelWidth, const uint16_t panelHeight) {
  if (!isRegistered()) return Result::Unauthorized;
  const std::string url = apiUrl("/api/v1/device/" + resolvedDeviceId() + "/status");
  if (url.empty()) return Result::NotConfigured;
  if (WiFi.status() != WL_CONNECTED) return Result::NoWifi;

  JsonDocument doc;
  doc["battery_pct"] = powerManager.getBatteryPercentage();
  doc["rssi"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();
  doc["fw_version"] = ALMANAC_VERSION;
  doc["panel_w"] = panelWidth;
  doc["panel_h"] = panelHeight;
  std::string body;
  serializeJson(doc, body);

  const HttpResponse response = request("POST", url, body, SETTINGS.tesseraeDeviceToken);
  if (response.status == 401 || response.status == 403) {
    clearRegistration();
    return Result::Unauthorized;
  }
  return response.status >= 200 && response.status < 300 ? Result::Ok : mapFailure(response.status);
}

void TesseraeClient::clearRegistration() {
  if (!SETTINGS.tesseraeDeviceToken[0]) return;
  SETTINGS.tesseraeDeviceToken[0] = '\0';
  SETTINGS.saveToFile();
}

const char* TesseraeClient::resultName(const Result result) {
  switch (result) {
    case Result::Ok:
      return "ok";
    case Result::WaitingApproval:
      return "waiting approval";
    case Result::NotConfigured:
      return "not configured";
    case Result::NoWifi:
      return "no Wi-Fi";
    case Result::NetworkError:
      return "network error";
    case Result::Unauthorized:
      return "unauthorized";
    case Result::NoFrame:
      return "no frame";
    case Result::InvalidResponse:
      return "invalid response";
    case Result::DownloadFailed:
      return "download failed";
  }
  return "unknown";
}

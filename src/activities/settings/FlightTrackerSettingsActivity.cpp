#include "FlightTrackerSettingsActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "AlmanacSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "network/ZipGeocodeClient.h"

namespace {
bool parseCoordinate(const std::string& text, double minValue, double maxValue, double& outValue) {
  if (text.empty()) return false;
  char* end = nullptr;
  const double value = strtod(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0') return false;
  // strtod() accepts "nan"/"-nan" as a valid parse with a NaN result; every
  // comparison against a NaN is false, so the range check below would let it
  // slip through as "in range". Reject it explicitly.
  if (std::isnan(value)) return false;
  if (value < minValue || value > maxValue) return false;
  outValue = value;
  return true;
}

bool isValidZip(const std::string& text) {
  if (text.size() != 5) return false;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}
}  // namespace

void FlightTrackerSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  errorMessage.clear();
  requestUpdate();
}

void FlightTrackerSettingsActivity::onExit() {
  Activity::onExit();
  // Unlike NearbyFlightsActivity (a top-level Home entry, where landing back
  // on Home after a reboot is exactly where Back would go anyway), this
  // activity is reached via Settings -> Flight Tracker. silentRestart() only
  // knows how to land on Home or the reader -- rebooting there on every exit
  // would strand a user who only edited Lat/Lon/Radius by hand, bouncing
  // them out of the settings hierarchy they were navigating. So the teardown
  // is gated on wifiUsedThisSession (set only inside performZipLookup(), the
  // one place this screen actually makes an HTTP/TLS request) rather than on
  // raw WiFi state -- matching SilentRestart.h's stated purpose (clearing
  // fragmentation from a WiFi *session*, not merely "WiFi is on"). A zip
  // lookup still lands the user on Home rather than back in Settings; that
  // residual is a real tradeoff, not a bug, given the destinations
  // silentRestart() offers.
  if (wifiUsedThisSession && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void FlightTrackerSettingsActivity::loop() {
  // CHECK_WIFI and LOADING run to completion synchronously inside the call
  // that set them (checkAndConnectWifi()/performZipLookup() are blocking,
  // same as NearbyFlightsActivity's fetchFlights()); WIFI_SELECTION hands
  // the screen to a pushed WifiSelectionActivity. None of the three should
  // process row navigation or Back/Confirm here.
  if (zipLookupState != ZipLookupState::IDLE) return;

  if (!errorMessage.empty() && millis() - errorShownAt >= ERROR_MESSAGE_DURATION_MS) {
    errorMessage.clear();
    requestUpdate();
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  switch (handleListTouch(selectedIndex, ITEM_COUNT, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      handleSelection();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, ITEM_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, ITEM_COUNT);
    requestUpdate();
  });
}

void FlightTrackerSettingsActivity::handleSelection() {
  if (selectedIndex == 0) {
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        if (isValidZip(kb.text)) {
          strncpy(pendingZip, kb.text.c_str(), sizeof(pendingZip) - 1);
          pendingZip[sizeof(pendingZip) - 1] = '\0';
          zipLookupState = ZipLookupState::CHECK_WIFI;
          requestUpdate();
          checkAndConnectWifi();
        } else {
          rejectZip(kb.text);
        }
      }
      requestUpdate();
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_FLIGHT_TRACKER_HOME_ZIP),
                                                std::string(SETTINGS.flightTrackerHomeZip), 5, InputType::Text),
        handler);
    return;
  }

  if (selectedIndex == 1) {
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        double parsed = 0;
        if (parseCoordinate(kb.text, -90.0, 90.0, parsed)) {
          strncpy(SETTINGS.flightTrackerHomeLat, kb.text.c_str(), sizeof(SETTINGS.flightTrackerHomeLat) - 1);
          SETTINGS.flightTrackerHomeLat[sizeof(SETTINGS.flightTrackerHomeLat) - 1] = '\0';
          SETTINGS.saveToFile();
        } else {
          rejectCoordinate(kb.text);
        }
      }
      requestUpdate();
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_FLIGHT_TRACKER_HOME_LAT),
                                                std::string(SETTINGS.flightTrackerHomeLat), 15, InputType::Text),
        handler);
    return;
  }

  if (selectedIndex == 2) {
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        double parsed = 0;
        if (parseCoordinate(kb.text, -180.0, 180.0, parsed)) {
          strncpy(SETTINGS.flightTrackerHomeLon, kb.text.c_str(), sizeof(SETTINGS.flightTrackerHomeLon) - 1);
          SETTINGS.flightTrackerHomeLon[sizeof(SETTINGS.flightTrackerHomeLon) - 1] = '\0';
          SETTINGS.saveToFile();
        } else {
          rejectCoordinate(kb.text);
        }
      }
      requestUpdate();
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_FLIGHT_TRACKER_HOME_LON),
                                                std::string(SETTINGS.flightTrackerHomeLon), 15, InputType::Text),
        handler);
    return;
  }

  // Search radius: tap cycles through the allowed range. `next` is int (not
  // uint8_t) purely as a defensive habit, not because it's load-bearing here:
  // AlmanacSettings::fromJson() already clamps every SettingType::VALUE
  // field -- including this one -- to [min,max] on load (see
  // AlmanacSettings.cpp:159-164), so flightTrackerRadiusMiles can never
  // actually reach this point above MAX. The int widening is harmless and
  // guards against that invariant changing later.
  const int next = static_cast<int>(SETTINGS.flightTrackerRadiusMiles) + AlmanacSettings::FLIGHT_TRACKER_RADIUS_STEP;
  SETTINGS.flightTrackerRadiusMiles = next > AlmanacSettings::FLIGHT_TRACKER_RADIUS_MAX
                                          ? AlmanacSettings::FLIGHT_TRACKER_RADIUS_MIN
                                          : static_cast<uint8_t>(next);
  SETTINGS.saveToFile();
  requestUpdate();
}

void FlightTrackerSettingsActivity::rejectCoordinate(const std::string& text) {
  errorMessage = tr(STR_INVALID_COORDINATE);
  errorShownAt = millis();
  LOG_ERR("FTS", "Rejected coordinate input: %s", text.c_str());
}

void FlightTrackerSettingsActivity::rejectZip(const std::string& text) {
  errorMessage = tr(STR_INVALID_ZIP_CODE);
  errorShownAt = millis();
  LOG_ERR("FTS", "Rejected zip input: %s", text.c_str());
}

void FlightTrackerSettingsActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    performZipLookup();
    return;
  }
  launchWifiSelection();
}

void FlightTrackerSettingsActivity::launchWifiSelection() {
  zipLookupState = ZipLookupState::WIFI_SELECTION;
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void FlightTrackerSettingsActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    performZipLookup();
    return;
  }
  // Leave WiFi up; onExit's silent reboot handles teardown without fragmenting.
  zipLookupState = ZipLookupState::IDLE;
  errorMessage = tr(STR_WIFI_CONN_FAILED);
  errorShownAt = millis();
  requestUpdate();
}

void FlightTrackerSettingsActivity::performZipLookup() {
  zipLookupState = ZipLookupState::LOADING;
  wifiUsedThisSession = true;
  requestUpdate();

  zipParser.reset();
  const ZipGeocodeClient::Result result = ZipGeocodeClient::geocode(pendingZip, zipParser);

  switch (result) {
    case ZipGeocodeClient::Result::Ok: {
      if (zipParser.hasError() || !zipParser.geocode().found) {
        LOG_ERR("FTS", "Zip lookup for %s returned a malformed response", pendingZip);
        errorMessage = tr(STR_ZIP_LOOKUP_ERROR);
        break;
      }
      const auto& geo = zipParser.geocode();
      snprintf(SETTINGS.flightTrackerHomeLat, sizeof(SETTINGS.flightTrackerHomeLat), "%.4f", geo.latitude);
      snprintf(SETTINGS.flightTrackerHomeLon, sizeof(SETTINGS.flightTrackerHomeLon), "%.4f", geo.longitude);
      strncpy(SETTINGS.flightTrackerHomeZip, pendingZip, sizeof(SETTINGS.flightTrackerHomeZip) - 1);
      SETTINGS.flightTrackerHomeZip[sizeof(SETTINGS.flightTrackerHomeZip) - 1] = '\0';
      SETTINGS.saveToFile();
      errorMessage.clear();
      break;
    }
    case ZipGeocodeClient::Result::NotFound:
      LOG_ERR("FTS", "No record for zip %s (HTTP 404)", pendingZip);
      errorMessage = tr(STR_ZIP_NOT_FOUND);
      break;
    case ZipGeocodeClient::Result::Error:
      LOG_ERR("FTS", "Zip lookup transport/parse failure for %s", pendingZip);
      errorMessage = tr(STR_ZIP_LOOKUP_ERROR);
      break;
  }

  zipLookupState = ZipLookupState::IDLE;
  errorShownAt = millis();
  requestUpdate();
}

void FlightTrackerSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  std::string subtitle;
  if (zipLookupState == ZipLookupState::LOADING) {
    char buf[48];
    snprintf(buf, sizeof(buf), tr(STR_ZIP_LOOKUP_LOADING_FORMAT), pendingZip);
    subtitle = buf;
  } else if (!errorMessage.empty()) {
    subtitle = errorMessage;
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FLIGHT_TRACKER),
                 subtitle.empty() ? nullptr : subtitle.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, ITEM_COUNT, selectedIndex,
      [](int index) -> std::string {
        switch (index) {
          case 0:
            return I18n::getInstance().get(StrId::STR_FLIGHT_TRACKER_HOME_ZIP);
          case 1:
            return I18n::getInstance().get(StrId::STR_FLIGHT_TRACKER_HOME_LAT);
          case 2:
            return I18n::getInstance().get(StrId::STR_FLIGHT_TRACKER_HOME_LON);
          default:
            return I18n::getInstance().get(StrId::STR_FLIGHT_TRACKER_RADIUS);
        }
      },
      nullptr, nullptr,
      [](int index) -> std::string {
        switch (index) {
          case 0:
            return SETTINGS.flightTrackerHomeZip[0] ? std::string(SETTINGS.flightTrackerHomeZip)
                                                    : std::string(I18n::getInstance().get(StrId::STR_NOT_SET));
          case 1:
            return SETTINGS.flightTrackerHomeLat[0] ? std::string(SETTINGS.flightTrackerHomeLat)
                                                    : std::string(I18n::getInstance().get(StrId::STR_NOT_SET));
          case 2:
            return SETTINGS.flightTrackerHomeLon[0] ? std::string(SETTINGS.flightTrackerHomeLon)
                                                    : std::string(I18n::getInstance().get(StrId::STR_NOT_SET));
          default: {
            char buf[8];
            snprintf(buf, sizeof(buf), "%u", SETTINGS.flightTrackerRadiusMiles);
            return std::string(buf);
          }
        }
      });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

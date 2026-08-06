#include "NearbyFlightsActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "AlmanacSettings.h"
#include "GeoMath.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/AdsbdbClient.h"
#include "network/OpenSkyClient.h"

bool NearbyFlightsActivity::parseHomeLocation(double& lat, double& lon) const {
  if (SETTINGS.flightTrackerHomeLat[0] == '\0' || SETTINGS.flightTrackerHomeLon[0] == '\0') return false;
  char* latEnd = nullptr;
  char* lonEnd = nullptr;
  const double parsedLat = strtod(SETTINGS.flightTrackerHomeLat, &latEnd);
  const double parsedLon = strtod(SETTINGS.flightTrackerHomeLon, &lonEnd);
  if (latEnd == SETTINGS.flightTrackerHomeLat || *latEnd != '\0') return false;
  if (lonEnd == SETTINGS.flightTrackerHomeLon || *lonEnd != '\0') return false;
  // strtod() accepts "nan"/"-nan" as a valid parse with a NaN result; every comparison against a
  // NaN is false, so the range check below would let it slip through as "in range". Reject it
  // explicitly -- same fix as FlightTrackerSettingsActivity::parseCoordinate.
  if (std::isnan(parsedLat) || std::isnan(parsedLon)) return false;
  if (parsedLat < -90.0 || parsedLat > 90.0 || parsedLon < -180.0 || parsedLon > 180.0) return false;
  lat = parsedLat;
  lon = parsedLon;
  return true;
}

void NearbyFlightsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  errorMessage.clear();

  double lat = 0;
  double lon = 0;
  if (!parseHomeLocation(lat, lon)) {
    state = FlightsState::NO_LOCATION;
    requestUpdate();
    return;
  }

  // Cold start: no view has been shown yet, so a successful fetch should land
  // on LIST, same as it always has.
  fetchReturnState = FlightsState::LIST;
  state = FlightsState::CHECK_WIFI;
  requestUpdate();
  checkAndConnectWifi();
}

void NearbyFlightsActivity::onExit() {
  Activity::onExit();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    // Disconnect, then silently reboot to tear WiFi down without fragmenting
    // the heap -- same pattern OpdsBookBrowserActivity uses.
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void NearbyFlightsActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    fetchFlights();
    return;
  }
  launchWifiSelection();
}

void NearbyFlightsActivity::launchWifiSelection() {
  state = FlightsState::WIFI_SELECTION;
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void NearbyFlightsActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    fetchFlights();
  } else {
    // Leave WiFi up; onExit's silent reboot handles teardown without fragmenting.
    state = FlightsState::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}

void NearbyFlightsActivity::fetchFlights() {
  state = FlightsState::LOADING;
  requestUpdate(true);

  double lat = 0;
  double lon = 0;
  if (!parseHomeLocation(lat, lon)) {
    state = FlightsState::NO_LOCATION;
    requestUpdate();
    return;
  }

  const double radius = SETTINGS.flightTrackerRadiusMiles;
  parser.reset(lat, lon, radius);

  // Checked and logged separately (rather than one combined condition) so the
  // console can tell a transport failure (DNS/TLS/HTTP) apart from a JSON
  // parse failure -- otherwise both collapse into the same silent blank spot.
  const bool fetchOk = OpenSkyClient::fetchNearby(lat, lon, radius, parser);
  if (!fetchOk) {
    LOG_ERR("FLIGHTS", "OpenSkyClient::fetchNearby transport failure");
  }
  if (parser.hasError()) {
    LOG_ERR("FLIGHTS", "OpenSkyStatesParser reported a JSON parse error");
  }
  if (!fetchOk || parser.hasError()) {
    state = FlightsState::ERROR;
    errorMessage = tr(STR_FETCH_FLIGHTS_FAILED);
    requestUpdate();
    return;
  }

  fetchCompletedMs = millis();
  selectedIndex = 0;
  state = fetchReturnState;  // whichever of LIST/RADAR asked for this fetch
  LOG_DBG("FLIGHTS", "Fetched %u matching aircraft", static_cast<unsigned>(parser.matchCount()));
  requestUpdate();
}

bool NearbyFlightsActivity::handleConfirmPressOrRefresh(const bool hasMatches) {
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmHeld = true;
    confirmLongHandled = false;
  }

  if (confirmHeld && !confirmLongHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    confirmLongHandled = true;
    confirmHeld = false;
    fetchReturnState = state;  // remember LIST vs RADAR so a successful refetch returns here
    checkAndConnectWifi();     // "Refresh"
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const bool wasShortPress = confirmHeld && !confirmLongHandled;
    confirmHeld = false;
    confirmLongHandled = false;
    if (wasShortPress && hasMatches) {
      detailReturnState = state;  // remember LIST vs RADAR so Back returns here
      state = FlightsState::DETAIL;
      // Paint the (possibly pending) detail screen before the blocking
      // lookup below, so the prior LIST/RADAR frame doesn't stay frozen on
      // screen for the whole fetch -- immediate notify, same pattern as
      // fetchFlights()'s LOADING screen.
      requestUpdate(true);
      ensureAircraftInfo();
      requestUpdate();
    }
    return true;
  }

  return false;
}

void NearbyFlightsActivity::loop() {
  // checkAndConnectWifi() above can change `state` synchronously (e.g. LIST
  // -> ERROR on a failed refetch) while Confirm is still physically held
  // down. The eventual release then arrives after state has already moved
  // on; without this guard it falls through to the new state's own Confirm
  // handler (e.g. ERROR's tap-to-retry) and fires an unwanted second fetch.
  // confirmLongHandled is only ever true right after that happens, so this
  // check is a no-op on every ordinary short press.
  if (confirmLongHandled && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    confirmHeld = false;
    confirmLongHandled = false;
    return;
  }

  switch (state) {
    case FlightsState::WIFI_SELECTION:
      return;  // WifiSelectionActivity owns input while pushed

    case FlightsState::NO_LOCATION:
      if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
          mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        onGoHome(HomeMenuItem::NEARBY_FLIGHTS);
      }
      return;

    case FlightsState::CHECK_WIFI:
    case FlightsState::LOADING:
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        onGoHome(HomeMenuItem::NEARBY_FLIGHTS);
      }
      return;

    case FlightsState::ERROR: {
      int tx = 0;
      int ty = 0;
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(tx, ty)) {
        checkAndConnectWifi();
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        onGoHome(HomeMenuItem::NEARBY_FLIGHTS);
      }
      return;
    }

    case FlightsState::DETAIL:
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        state = detailReturnState;  // back to whichever of LIST/RADAR opened this
        requestUpdate();
      }
      return;

    case FlightsState::LIST: {
      const auto matchCount = static_cast<int>(parser.matchCount());

      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        onGoHome(HomeMenuItem::NEARBY_FLIGHTS);
        return;
      }
      if (handleConfirmPressOrRefresh(matchCount > 0)) {
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
        state = FlightsState::RADAR;  // toggle to radar
        requestUpdate();
        return;
      }

      if (matchCount > 0) {
        const auto& metrics = UITheme::getInstance().getMetrics();
        const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
        const int contentHeight =
            renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
        switch (handleListTouch(selectedIndex, matchCount, contentTop, contentHeight, true)) {
          case ListTouchResult::Activated:
            detailReturnState = FlightsState::LIST;  // touch activation only exists in LIST
            state = FlightsState::DETAIL;
            // See the matching comment in handleConfirmPressOrRefresh: paint
            // the pending detail screen before the blocking lookup.
            requestUpdate(true);
            ensureAircraftInfo();
            requestUpdate();
            return;
          case ListTouchResult::Consumed:
            return;
          case ListTouchResult::None:
            break;
        }

        buttonNavigator.onNextRelease([this, matchCount] {
          selectedIndex = ButtonNavigator::nextIndex(selectedIndex, matchCount);
          requestUpdate();
        });
        buttonNavigator.onPreviousRelease([this, matchCount] {
          selectedIndex = ButtonNavigator::previousIndex(selectedIndex, matchCount);
          requestUpdate();
        });
      }
      return;
    }

    case FlightsState::RADAR: {
      const auto matchCount = static_cast<int>(parser.matchCount());

      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        onGoHome(HomeMenuItem::NEARBY_FLIGHTS);
        return;
      }
      if (handleConfirmPressOrRefresh(matchCount > 0)) {
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
        state = FlightsState::LIST;  // toggle back to the list
        requestUpdate();
        return;
      }

      if (matchCount > 0) {
        buttonNavigator.onNextRelease([this, matchCount] {
          selectedIndex = ButtonNavigator::nextIndex(selectedIndex, matchCount);
          requestUpdate();
        });
        buttonNavigator.onPreviousRelease([this, matchCount] {
          selectedIndex = ButtonNavigator::previousIndex(selectedIndex, matchCount);
          requestUpdate();
        });
      }
      return;
    }
  }
}

void NearbyFlightsActivity::ensureAircraftInfo() {
  if (parser.matchCount() == 0) return;
  const auto& m = parser.matchAt(static_cast<size_t>(selectedIndex));

  // Already cached for this aircraft AND that earlier lookup didn't fail.
  // A failed lookup must be retried on the next visit rather than pinning
  // "unavailable" for the rest of the activity's lifetime, so it deliberately
  // does NOT short-circuit here -- see the aircraftLookupFailed comment on
  // the member. A NotFound (no adsbdb record) is not a failure and DOES
  // short-circuit, same as a successful lookup.
  //
  // An empty icao24 is its own cache key: there is nothing to look up, but
  // the reset below still must happen so a match with no icao24 doesn't
  // inherit a *different* aircraft's stale result left over in
  // aircraftParser from a previous DETAIL visit.
  if (!aircraftLookupFailed && strcmp(aircraftInfoIcao24, m.icao24) == 0) return;

  aircraftParser.reset();
  // Cleared immediately (not just left stale) so this also covers the
  // same-aircraft-retry-after-failure case: without this, the guard above
  // falls through with aircraftInfoIcao24 still equal to m.icao24 (it was
  // written unconditionally at the end of the previous, failed attempt --
  // see the strncpy below), so a render landing after this point but before
  // aircraftLookupFailed is reset to false would see a "matching" key and a
  // "found == false" (just-reset) parser and render the definitive-sounding
  // "Type: unknown" for a retry that hasn't even started.
  aircraftInfoIcao24[0] = '\0';
  aircraftLookupFailed = false;

  if (m.icao24[0] == '\0') {
    aircraftInfoIcao24[0] = '\0';  // nothing to look up; renders as "Type: unknown"
    return;
  }

  // aircraftInfoIcao24 was just cleared above and is not rewritten until the
  // attempt below completes, so renderDetail()'s infoPending check
  // (aircraftInfoIcao24 != m.icao24) stays true for the whole blocking call,
  // no matter when the render task happens to run relative to this one
  // (ActivityManagerRender and the Arduino loop task share the same
  // FreeRTOS priority, so there's no ordering guarantee between the
  // requestUpdate(true) at the DETAIL-entry call sites and this function
  // actually running). Writing the key here first would let a render task
  // that runs between this line and the fetch returning see a "matching"
  // key pointing at a freshly-reset (empty) aircraftParser, i.e. render
  // "Type: unknown" instead of "Type: checking..." for a lookup that hasn't
  // even started yet.
  //
  // Logged separately so the console distinguishes a transport failure from a
  // JSON parse failure -- both otherwise render the same on screen.
  const AdsbdbClient::Result result = AdsbdbClient::fetchAircraftInfo(m.icao24, aircraftParser);
  if (result == AdsbdbClient::Result::Error) {
    LOG_ERR("FLIGHTS", "AdsbdbClient::fetchAircraftInfo transport failure for %s", m.icao24);
  }
  if (aircraftParser.hasError()) {
    LOG_ERR("FLIGHTS", "AircraftInfoParser reported a JSON parse error for %s", m.icao24);
  }
  // A 404 (Result::NotFound) is adsbdb's normal "no record" answer, not a
  // failure -- it renders as STR_AIRCRAFT_TYPE_UNKNOWN via the same
  // acInfo.found == false path as a JSON-level miss, and (per the guard
  // above) stays cached like a success. Only a transport failure or a JSON
  // parse error counts as aircraftLookupFailed.
  aircraftLookupFailed = result == AdsbdbClient::Result::Error || aircraftParser.hasError();
  if (!aircraftLookupFailed) {
    LOG_DBG("FLIGHTS", "Aircraft %s: found=%d type=%s", m.icao24, aircraftParser.info().found ? 1 : 0,
            aircraftParser.info().icaoType);
  }
  // Recorded only now that the attempt has completed (success, NotFound, or
  // Error -- see the M3 retry-on-failure note on the guard above and on the
  // aircraftLookupFailed member).
  strncpy(aircraftInfoIcao24, m.icao24, sizeof(aircraftInfoIcao24) - 1);
  aircraftInfoIcao24[sizeof(aircraftInfoIcao24) - 1] = '\0';
}

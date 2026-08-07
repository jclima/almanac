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

#include "CrossPointSettings.h"
#include "GeoMath.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/AdsbdbClient.h"
#include "network/OpenSkyClient.h"

namespace {
// GfxRenderer has no circle primitive; drawArc renders a single quadrant
// selected by the xDir/yDir signs. Four calls make one full circle. The axis
// row/column is drawn twice (once by each adjacent quadrant), which is
// harmless because fillRect sets pixels rather than toggling them.
void drawCircle(const GfxRenderer& renderer, int radius, int cx, int cy, int lineWidth) {
  if (radius <= 0) return;
  renderer.drawArc(radius, cx, cy, -1, -1, lineWidth, true);
  renderer.drawArc(radius, cx, cy, 1, -1, lineWidth, true);
  renderer.drawArc(radius, cx, cy, 1, 1, lineWidth, true);
  renderer.drawArc(radius, cx, cy, -1, 1, lineWidth, true);
}
}  // namespace

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

void NearbyFlightsActivity::render(RenderLock&&) {
  switch (state) {
    case FlightsState::NO_LOCATION:
      renderNoLocation();
      return;
    case FlightsState::CHECK_WIFI:
    case FlightsState::LOADING:
      renderCheckWifiOrLoading();
      return;
    case FlightsState::WIFI_SELECTION:
      return;  // WifiSelectionActivity owns the screen while pushed
    case FlightsState::ERROR:
      renderError();
      return;
    case FlightsState::LIST:
      renderList();
      return;
    case FlightsState::RADAR:
      renderRadar();
      return;
    case FlightsState::DETAIL:
      renderDetail();
      return;
  }
}

void NearbyFlightsActivity::renderNoLocation() const {
  renderer.clearScreen();
  const auto pageHeight = renderer.getScreenHeight();
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_SET_HOME_LOCATION_FIRST));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void NearbyFlightsActivity::renderCheckWifiOrLoading() const {
  renderer.clearScreen();
  const auto pageHeight = renderer.getScreenHeight();
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_LOADING));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void NearbyFlightsActivity::renderError() const {
  renderer.clearScreen();
  const auto pageHeight = renderer.getScreenHeight();
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_ERROR_MSG));
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
  if (mappedInput.hasTouch()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 40, tr(STR_TAP_TO_RETRY));
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void NearbyFlightsActivity::renderList() {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_NEARBY_FLIGHTS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const auto matchCount = static_cast<int>(parser.matchCount());

  if (matchCount == 0) {
    char message[48];
    snprintf(message, sizeof(message), tr(STR_NO_FLIGHTS_FORMAT), static_cast<int>(SETTINGS.flightTrackerRadiusMiles));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, message);
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, matchCount, selectedIndex,
        [this](int index) -> std::string {
          const auto& m = parser.matchAt(static_cast<size_t>(index));
          return m.callsign[0] ? std::string(m.callsign) : std::string(tr(STR_UNKNOWN_CALLSIGN));
        },
        [this](int index) -> std::string {
          const auto& m = parser.matchAt(static_cast<size_t>(index));
          char buf[32];
          snprintf(buf, sizeof(buf), tr(STR_FLIGHT_DISTANCE_FORMAT), m.distanceMiles,
                   GeoMath::compassPoint(m.bearingDeg));
          return std::string(buf);
        },
        nullptr,
        [this](int index) -> std::string {
          const auto& m = parser.matchAt(static_cast<size_t>(index));
          if (!m.hasAltitudeFeet) return std::string();
          char buf[16];
          snprintf(buf, sizeof(buf), tr(STR_FLIGHT_ALTITUDE_SHORT_FORMAT), static_cast<long>(m.altitudeFeet));
          return std::string(buf);
        });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_RADAR), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void NearbyFlightsActivity::renderRadar() {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_NEARBY_FLIGHTS));

  const auto matchCount = static_cast<int>(parser.matchCount());
  const double maxRange = SETTINGS.flightTrackerRadiusMiles;

  // Reserve four rows at the bottom for the selected-aircraft readout, above
  // the button hints. Everything derives from theme metrics -- headerHeight
  // alone varies 45..84 across the shipped themes.
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int stripHeight = metrics.listRowHeight * 3;
  const int plotBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing * 2 - stripHeight;
  const int plotHeight = plotBottom - contentTop;

  if (matchCount == 0) {
    char message[48];
    snprintf(message, sizeof(message), tr(STR_NO_FLIGHTS_FORMAT), static_cast<int>(SETTINGS.flightTrackerRadiusMiles));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, message);
    const auto emptyLabels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_LIST_VIEW), "");
    GUI.drawButtonHints(renderer, emptyLabels.btn1, emptyLabels.btn2, emptyLabels.btn3, emptyLabels.btn4);
    renderer.displayBuffer();
    return;
  }

  // Aircraft glyph sizes -- used both for the markers drawn below and here,
  // to budget the plot margin so the selected-aircraft ring (radius
  // SELECTED_RING_RADIUS, drawn around the marker) never clips against the
  // content edge. contentSidePadding (20) alone is smaller than
  // SELECTED_RING_RADIUS (22): a selected aircraft plotted at full range due
  // E/W would otherwise clip its ring by a couple of pixels in portrait.
  static constexpr int MARK_SIZE = 11;
  static constexpr int SELECTED_MARK_SIZE = 15;
  static constexpr int SELECTED_RING_RADIUS = SELECTED_MARK_SIZE + 7;

  const int cx = pageWidth / 2;
  const int cy = contentTop + plotHeight / 2;
  // Fit the circle to whichever axis is tighter, so landscape orientations
  // shrink the plot instead of clipping it.
  const int plotMargin = std::max(metrics.contentSidePadding, SELECTED_RING_RADIUS);
  const int radiusPx = std::min(pageWidth / 2, plotHeight / 2) - plotMargin;

  // Range rings. drawArc renders ONE QUADRANT per call, selected by the
  // xDir/yDir signs: (-1,-1) top-left, (1,-1) top-right, (1,1) bottom-right,
  // (-1,1) bottom-left. Four calls = one full circle. lineWidth must stay
  // well below the radius or the ring fills into a solid disc.
  static constexpr int RING_COUNT = 3;
  for (int ring = 1; ring <= RING_COUNT; ++ring) {
    const int r = radiusPx * ring / RING_COUNT;
    const int lineWidth = (ring == RING_COUNT) ? 2 : 1;
    drawCircle(renderer, r, cx, cy, lineWidth);

    char ringLabel[12];
    if (ring == RING_COUNT) {
      snprintf(ringLabel, sizeof(ringLabel), tr(STR_RADAR_RANGE_OUTER_FORMAT), static_cast<int>(maxRange));
    } else {
      snprintf(ringLabel, sizeof(ringLabel), "%d", static_cast<int>(maxRange * ring / RING_COUNT));
    }
    renderer.drawText(UI_10_FONT_ID, cx + 6, cy - r - 2, ringLabel, true);
  }

  // Crosshair and compass letters.
  renderer.drawLine(cx, cy - radiusPx, cx, cy + radiusPx, true);
  renderer.drawLine(cx - radiusPx, cy, cx + radiusPx, cy, true);
  renderer.drawText(UI_10_FONT_ID, cx - 4, cy - radiusPx - 22, "N", true);
  renderer.drawText(UI_10_FONT_ID, cx - 4, cy + radiusPx + 6, "S", true);
  renderer.drawText(UI_10_FONT_ID, cx + radiusPx + 6, cy - 8, "E", true);
  renderer.drawText(UI_10_FONT_ID, cx - radiusPx - 14, cy - 8, "W", true);

  // Home location.
  renderer.fillRect(cx - 2, cy - 2, 5, 5, true);

  // Aircraft. fillPolygon malloc()s a small node buffer per call, so this is
  // up to MAX_MATCHES(20) 16-byte alloc/free pairs per frame. They are tiny,
  // immediately freed, and identically sized, so the allocator reuses the same
  // block rather than fragmenting -- and radar frames only render on user
  // input, never continuously. Accepted deliberately; revisit if heap
  // instrumentation shows otherwise.
  for (int i = 0; i < matchCount; ++i) {
    const auto& m = parser.matchAt(static_cast<size_t>(i));
    const auto p = GeoMath::polarToScreen(m.distanceMiles, m.bearingDeg, maxRange, cx, cy, radiusPx);
    const bool isSelected = (i == selectedIndex);
    // Aircraft with no reported track draw nose-up rather than vanishing.
    const double heading = m.hasHeading ? static_cast<double>(m.headingDeg) : 0.0;

    int xs[4];
    int ys[4];
    GeoMath::headingTriangle(p.x, p.y, heading, isSelected ? SELECTED_MARK_SIZE : MARK_SIZE, xs, ys);
    renderer.fillPolygon(xs, ys, 4, true);

    if (isSelected) {
      drawCircle(renderer, SELECTED_RING_RADIUS, p.x, p.y, 1);
    }
  }

  // Selected-aircraft readout.
  const auto& sel = parser.matchAt(static_cast<size_t>(selectedIndex));
  const int textX = metrics.contentSidePadding;
  int textY = plotBottom + metrics.verticalSpacing;
  char line[64];

  renderer.drawText(UI_10_FONT_ID, textX, textY, sel.callsign[0] ? sel.callsign : tr(STR_UNKNOWN_CALLSIGN), true);
  snprintf(line, sizeof(line), tr(STR_FLIGHT_DISTANCE_FORMAT), sel.distanceMiles,
           GeoMath::compassPoint(sel.bearingDeg));
  renderer.drawText(UI_10_FONT_ID, textX, textY + metrics.listRowHeight, line, true);

  if (sel.hasAltitudeFeet) {
    snprintf(line, sizeof(line), tr(STR_FLIGHT_ALTITUDE_FORMAT), static_cast<long>(sel.altitudeFeet));
    renderer.drawText(UI_10_FONT_ID, textX, textY + metrics.listRowHeight * 2, line, true);
  }

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), tr(STR_FLIGHT_DETAIL), tr(STR_LIST_VIEW), tr(STR_NEXT_AIRCRAFT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
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

void NearbyFlightsActivity::renderDetail() const {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto& m = parser.matchAt(static_cast<size_t>(selectedIndex));

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 m.callsign[0] ? m.callsign : tr(STR_UNKNOWN_CALLSIGN));

  const int x = metrics.contentSidePadding;
  const int firstLineY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + metrics.listRowHeight;

  // Highest y at which a full row still clears the button-hints strip
  // reserved at the bottom of the screen. Without this guard, the tightest
  // theme+orientation combo (Lyra, landscape -- getScreenHeight() returns
  // the 480px panelHeight there, see GfxRenderer.cpp's Landscape cases) can
  // push a fully-populated detail screen (up to 10 lines) tens of pixels
  // past the physical bottom. That is worse than merely invisible text:
  // GfxRenderer::drawPixel bounds-checks and logs LOG_ERR("GFX", "!!
  // Outside range ...") once PER out-of-range pixel, so an off-screen text
  // row floods the serial log rather than silently failing to draw.
  const int maxY = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.listRowHeight;

  // DETAIL is now painted once *before* ensureAircraftInfo()'s blocking
  // lookup runs (see the two DETAIL-entry call sites), so this render can
  // land on the frame where the lookup for THIS aircraft hasn't happened
  // yet. aircraftInfoIcao24 is the cache key aircraftParser/
  // aircraftLookupFailed actually describe (see ensureAircraftInfo); if it
  // doesn't match the currently-selected aircraft, that data belongs to
  // whatever aircraft was cached before (or is empty) and must not be shown
  // as this aircraft's result.
  const bool infoPending = strcmp(aircraftInfoIcao24, m.icao24) != 0;
  const auto& acInfo = aircraftParser.info();
  // A "found" record with every field empty (adsbdb has the aircraft object
  // but no useful data in it) is informationally the same as no record --
  // render it the same way rather than silently skipping the block.
  const bool acInfoAllEmpty = !acInfo.manufacturer[0] && !acInfo.icaoType[0] && !acInfo.registration[0];
  const bool showStatusLine = infoPending || aircraftLookupFailed || !acInfo.found || acInfoAllEmpty;
  const bool showTypeLine = !showStatusLine && (acInfo.manufacturer[0] || acInfo.icaoType[0]);
  const bool showRegLine = !showStatusLine && acInfo.registration[0];

  // Fields the screen could show, keyed by identity rather than draw order,
  // so priority (below) and visual layout (further below) can differ.
  enum DetailField : uint8_t {
    kAltitude,
    kType,
    kDistance,
    kHeading,
    kRegistration,
    kSpeed,
    kOrigin,
    kIcao24,
    kVerticalRate,
    kDataAge,
    kFieldCount
  };

  bool present[kFieldCount] = {};
  present[kAltitude] = m.hasAltitudeFeet;
  present[kType] = showStatusLine || showTypeLine;  // the status message occupies this slot when there's no type text
  present[kDistance] = true;
  present[kHeading] = m.hasHeading;
  present[kRegistration] = showRegLine;
  present[kSpeed] = m.hasSpeedMph;
  present[kOrigin] = m.originCountry[0] != '\0';
  present[kIcao24] = true;
  present[kVerticalRate] = m.hasVerticalRate;
  present[kDataAge] = true;

  // Which present fields survive if the screen can't fit them all, highest
  // priority first. Altitude and aircraft type/registration are this
  // screen's reason for existing -- kept longest. Data age is a pure
  // freshness indicator and the least harmful to drop, so it goes last,
  // just after the technical ICAO24 identifier and the vertical-rate
  // refinement of altitude. Distance/heading/speed/origin sit in between,
  // roughly in order of how much they help identify and place the aircraft.
  static constexpr DetailField PRIORITY_ORDER[kFieldCount] = {
      kAltitude, kType, kDistance, kHeading, kRegistration, kSpeed, kOrigin, kIcao24, kVerticalRate, kDataAge,
  };

  // Pass 1: decide which present fields fit, in priority order. Every line
  // is the same height, so this depends only on how many higher-priority
  // present fields came before it, not on any field's actual text --
  // nothing needs formatting yet.
  bool included[kFieldCount] = {};
  {
    int simulatedY = firstLineY;
    for (const DetailField f : PRIORITY_ORDER) {
      if (!present[f]) continue;
      if (simulatedY > maxY) break;  // this and every lower-priority field after it won't fit either
      included[f] = true;
      simulatedY += metrics.listRowHeight;
    }
  }

  // Pass 2: draw whatever was included, in the screen's normal reading
  // order, formatting into one reused buffer -- same footprint as before
  // truncation existed. In the overwhelming majority of cases (any theme in
  // portrait, or landscape without every optional field populated at once)
  // every field is included and this renders exactly as it always has.
  int y = firstLineY;
  char line[64];

  if (included[kAltitude]) {
    snprintf(line, sizeof(line), tr(STR_FLIGHT_ALTITUDE_FORMAT), static_cast<long>(m.altitudeFeet));
    renderer.drawText(UI_10_FONT_ID, x, y, line, true);
    y += metrics.listRowHeight;
  }
  if (included[kSpeed]) {
    snprintf(line, sizeof(line), tr(STR_FLIGHT_SPEED_FORMAT), static_cast<long>(m.speedMph));
    renderer.drawText(UI_10_FONT_ID, x, y, line, true);
    y += metrics.listRowHeight;
  }
  if (included[kHeading]) {
    snprintf(line, sizeof(line), tr(STR_FLIGHT_HEADING_FORMAT), static_cast<long>(m.headingDeg),
             GeoMath::compassPoint(m.headingDeg));
    renderer.drawText(UI_10_FONT_ID, x, y, line, true);
    y += metrics.listRowHeight;
  }
  if (included[kVerticalRate]) {
    const char* rateLabel = m.verticalRateMs > 0.5f    ? tr(STR_FLIGHT_CLIMBING)
                            : m.verticalRateMs < -0.5f ? tr(STR_FLIGHT_DESCENDING)
                                                       : tr(STR_FLIGHT_LEVEL);
    renderer.drawText(UI_10_FONT_ID, x, y, rateLabel, true);
    y += metrics.listRowHeight;
  }
  if (included[kDistance]) {
    snprintf(line, sizeof(line), tr(STR_FLIGHT_DISTANCE_FORMAT), m.distanceMiles, GeoMath::compassPoint(m.bearingDeg));
    renderer.drawText(UI_10_FONT_ID, x, y, line, true);
    y += metrics.listRowHeight;
  }
  if (included[kOrigin]) {
    snprintf(line, sizeof(line), tr(STR_FLIGHT_ORIGIN_FORMAT), m.originCountry);
    renderer.drawText(UI_10_FONT_ID, x, y, line, true);
    y += metrics.listRowHeight;
  }
  if (included[kIcao24]) {
    snprintf(line, sizeof(line), tr(STR_FLIGHT_ICAO24_FORMAT), m.icao24);
    renderer.drawText(UI_10_FONT_ID, x, y, line, true);
    y += metrics.listRowHeight;
  }
  if (included[kType]) {
    if (showStatusLine) {
      // infoPending checked first: the lookup for THIS aircraft hasn't run
      // yet on this frame (see the comment above showStatusLine), so
      // aircraftLookupFailed still reflects whatever aircraft was looked up
      // previously and must not be shown as this one's result.
      const char* statusText;
      if (infoPending) {
        statusText = tr(STR_AIRCRAFT_TYPE_LOADING);
      } else if (aircraftLookupFailed) {
        statusText = tr(STR_AIRCRAFT_TYPE_UNAVAILABLE);
      } else {
        statusText = tr(STR_AIRCRAFT_TYPE_UNKNOWN);
      }
      renderer.drawText(UI_10_FONT_ID, x, y, statusText, true);
    } else {
      snprintf(line, sizeof(line), tr(STR_AIRCRAFT_TYPE_FORMAT), acInfo.manufacturer, acInfo.icaoType);
      renderer.drawText(UI_10_FONT_ID, x, y, line, true);
    }
    y += metrics.listRowHeight;
  }
  if (included[kRegistration]) {
    snprintf(line, sizeof(line), tr(STR_AIRCRAFT_REG_FORMAT), acInfo.registration);
    renderer.drawText(UI_10_FONT_ID, x, y, line, true);
    y += metrics.listRowHeight;
  }
  if (included[kDataAge]) {
    const unsigned long ageSeconds = (millis() - fetchCompletedMs) / 1000;
    snprintf(line, sizeof(line), tr(STR_FLIGHT_DATA_AGE_FORMAT), ageSeconds);
    renderer.drawText(UI_10_FONT_ID, x, y, line, true);
    y += metrics.listRowHeight;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

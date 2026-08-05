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

#include "CrossPointSettings.h"
#include "GeoMath.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
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
  state = FlightsState::LIST;
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
    checkAndConnectWifi();  // "Refresh"
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const bool wasShortPress = confirmHeld && !confirmLongHandled;
    confirmHeld = false;
    confirmLongHandled = false;
    if (wasShortPress && hasMatches) {
      state = FlightsState::DETAIL;
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
        state = FlightsState::LIST;
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
            state = FlightsState::DETAIL;
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

  const int cx = pageWidth / 2;
  const int cy = contentTop + plotHeight / 2;
  // Fit the circle to whichever axis is tighter, so landscape orientations
  // shrink the plot instead of clipping it.
  const int radiusPx = std::min(pageWidth / 2, plotHeight / 2) - metrics.contentSidePadding;

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
  static constexpr int MARK_SIZE = 11;
  static constexpr int SELECTED_MARK_SIZE = 15;
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
      drawCircle(renderer, SELECTED_MARK_SIZE + 7, p.x, p.y, 1);
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

void NearbyFlightsActivity::renderDetail() const {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto& m = parser.matchAt(static_cast<size_t>(selectedIndex));

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 m.callsign[0] ? m.callsign : tr(STR_UNKNOWN_CALLSIGN));

  const int x = metrics.contentSidePadding;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + metrics.listRowHeight;
  char line[64];

  if (m.hasAltitudeFeet) {
    snprintf(line, sizeof(line), tr(STR_FLIGHT_ALTITUDE_FORMAT), static_cast<long>(m.altitudeFeet));
    renderer.drawText(UI_10_FONT_ID, x, y, line, true);
    y += metrics.listRowHeight;
  }
  if (m.hasSpeedMph) {
    snprintf(line, sizeof(line), tr(STR_FLIGHT_SPEED_FORMAT), static_cast<long>(m.speedMph));
    renderer.drawText(UI_10_FONT_ID, x, y, line, true);
    y += metrics.listRowHeight;
  }
  if (m.hasHeading) {
    snprintf(line, sizeof(line), tr(STR_FLIGHT_HEADING_FORMAT), static_cast<long>(m.headingDeg),
             GeoMath::compassPoint(m.headingDeg));
    renderer.drawText(UI_10_FONT_ID, x, y, line, true);
    y += metrics.listRowHeight;
  }
  if (m.hasVerticalRate) {
    const char* rateLabel = m.verticalRateMs > 0.5f    ? tr(STR_FLIGHT_CLIMBING)
                            : m.verticalRateMs < -0.5f ? tr(STR_FLIGHT_DESCENDING)
                                                        : tr(STR_FLIGHT_LEVEL);
    renderer.drawText(UI_10_FONT_ID, x, y, rateLabel, true);
    y += metrics.listRowHeight;
  }

  snprintf(line, sizeof(line), tr(STR_FLIGHT_DISTANCE_FORMAT), m.distanceMiles, GeoMath::compassPoint(m.bearingDeg));
  renderer.drawText(UI_10_FONT_ID, x, y, line, true);
  y += metrics.listRowHeight;

  if (m.originCountry[0]) {
    snprintf(line, sizeof(line), tr(STR_FLIGHT_ORIGIN_FORMAT), m.originCountry);
    renderer.drawText(UI_10_FONT_ID, x, y, line, true);
    y += metrics.listRowHeight;
  }

  snprintf(line, sizeof(line), tr(STR_FLIGHT_ICAO24_FORMAT), m.icao24);
  renderer.drawText(UI_10_FONT_ID, x, y, line, true);
  y += metrics.listRowHeight;

  const unsigned long ageSeconds = (millis() - fetchCompletedMs) / 1000;
  snprintf(line, sizeof(line), tr(STR_FLIGHT_DATA_AGE_FORMAT), ageSeconds);
  renderer.drawText(UI_10_FONT_ID, x, y, line, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

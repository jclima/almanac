// Screen painters for NearbyFlightsActivity.
//
// Split from NearbyFlightsActivity.cpp so that one file does not carry the
// state machine, input dispatch, network orchestration AND four screen
// renderers at once. These are still members of the same class -- one class,
// two translation units -- so the split changes no interface and no behaviour.

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "AlmanacSettings.h"
#include "GeoMath.h"
#include "MappedInputManager.h"
#include "NearbyFlightsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

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

  // With no aircraft there is nothing to select and nothing to scroll, so
  // advertising Select and Down would be hints that lie. Long-press-Confirm
  // refreshes in both states (handleConfirmPressOrRefresh gates only the
  // short press on hasMatches), and this empty slot is the one place the
  // gesture can be named without dropping a hint that does work.
  const auto labels = matchCount == 0
                          ? mappedInput.mapLabels(tr(STR_BACK), tr(STR_REFRESH), tr(STR_RADAR), "")
                          : mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_RADAR), tr(STR_DIR_DOWN));
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
    // Same reasoning as the empty list above: name the refresh in the slot
    // that would otherwise sit blank.
    const auto emptyLabels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_REFRESH), tr(STR_LIST_VIEW), "");
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

  // A theme with a tall enough header, combined with the three-row readout
  // strip, can drive plotHeight (and so radiusPx) non-positive. drawCircle
  // guards itself, but the crosshair, the compass letters and every aircraft
  // position do not: they would draw far off-screen, and
  // GfxRenderer::drawPixel logs "!! Outside range" once PER out-of-range
  // pixel, flooding the serial log rather than failing quietly.
  //
  // No shipped theme reaches this today -- the tightest is Lyra in landscape
  // at radiusPx 69 -- so this is a guard against a future theme, not a live
  // defect. When it does trip, drop the plot and keep the readout, which is
  // the part that still carries usable information.
  const bool plotFits = radiusPx > 0;
  if (!plotFits) {
    LOG_ERR("FLIGHTS", "Radar plot area too small for this theme (radiusPx=%d, plotHeight=%d); showing readout only",
            radiusPx, plotHeight);
  }

  if (plotFits) {
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
  }

  // Selected-aircraft readout. When the plot was skipped it moves up to where
  // the plot would have started, so the screen still reads as a readout rather
  // than leaving a gap.
  const auto& sel = parser.matchAt(static_cast<size_t>(selectedIndex));
  const int textX = metrics.contentSidePadding;
  int textY = plotFits ? plotBottom + metrics.verticalSpacing : contentTop;
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
    // Dead on the last field, and deliberately kept: every block advances y, so
    // a field added after this one inherits the correct position instead of
    // drawing on top of the data-age line.
    // cppcheck-suppress unreadVariable
    y += metrics.listRowHeight;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

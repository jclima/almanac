#include "FlightTrackerSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

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
}  // namespace

void FlightTrackerSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void FlightTrackerSettingsActivity::onExit() { Activity::onExit(); }

void FlightTrackerSettingsActivity::loop() {
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
        double parsed = 0;
        if (parseCoordinate(kb.text, -90.0, 90.0, parsed)) {
          strncpy(SETTINGS.flightTrackerHomeLat, kb.text.c_str(), sizeof(SETTINGS.flightTrackerHomeLat) - 1);
          SETTINGS.flightTrackerHomeLat[sizeof(SETTINGS.flightTrackerHomeLat) - 1] = '\0';
          SETTINGS.saveToFile();
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

  if (selectedIndex == 1) {
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        double parsed = 0;
        if (parseCoordinate(kb.text, -180.0, 180.0, parsed)) {
          strncpy(SETTINGS.flightTrackerHomeLon, kb.text.c_str(), sizeof(SETTINGS.flightTrackerHomeLon) - 1);
          SETTINGS.flightTrackerHomeLon[sizeof(SETTINGS.flightTrackerHomeLon) - 1] = '\0';
          SETTINGS.saveToFile();
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
  // uint8_t) so a persisted value already above MAX (e.g. from a hand-edited
  // settings file) can't wrap through a narrowing add before the MAX check.
  const int next = static_cast<int>(SETTINGS.flightTrackerRadiusMiles) + CrossPointSettings::FLIGHT_TRACKER_RADIUS_STEP;
  SETTINGS.flightTrackerRadiusMiles = next > CrossPointSettings::FLIGHT_TRACKER_RADIUS_MAX
                                          ? CrossPointSettings::FLIGHT_TRACKER_RADIUS_MIN
                                          : static_cast<uint8_t>(next);
  SETTINGS.saveToFile();
  requestUpdate();
}

void FlightTrackerSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FLIGHT_TRACKER));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, ITEM_COUNT, selectedIndex,
      [](int index) -> std::string {
        switch (index) {
          case 0:
            return I18n::getInstance().get(StrId::STR_FLIGHT_TRACKER_HOME_LAT);
          case 1:
            return I18n::getInstance().get(StrId::STR_FLIGHT_TRACKER_HOME_LON);
          default:
            return I18n::getInstance().get(StrId::STR_FLIGHT_TRACKER_RADIUS);
        }
      },
      nullptr, nullptr,
      [](int index) -> std::string {
        switch (index) {
          case 0:
            return SETTINGS.flightTrackerHomeLat[0] ? std::string(SETTINGS.flightTrackerHomeLat)
                                                    : std::string(I18n::getInstance().get(StrId::STR_NOT_SET));
          case 1:
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

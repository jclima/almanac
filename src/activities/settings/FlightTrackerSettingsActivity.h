#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Three-row settings screen: home latitude, home longitude (both free-text
// decimal degrees via the keyboard), and search radius (tap-to-cycle).
class FlightTrackerSettingsActivity final : public Activity {
 public:
  explicit FlightTrackerSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlightTrackerSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int ITEM_COUNT = 3;

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  void handleSelection();
};

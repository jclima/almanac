#pragma once

#include "OpenSkyStatesParser.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class NearbyFlightsActivity final : public Activity {
 public:
  enum class FlightsState { NO_LOCATION, CHECK_WIFI, WIFI_SELECTION, LOADING, LIST, RADAR, DETAIL, ERROR };

  explicit NearbyFlightsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("NearbyFlights", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  bool preventAutoSleep() override { return true; }

  ButtonNavigator buttonNavigator;
  FlightsState state = FlightsState::NO_LOCATION;
  OpenSkyStatesParser parser;
  int selectedIndex = 0;
  std::string errorMessage;
  unsigned long fetchCompletedMs = 0;

  // Long-press Confirm = Refresh, short press = select/detail. getHeldTime()
  // is global rather than per-button, so confirmHeld is what attributes the
  // elapsed time to Confirm -- dropping it makes the check misfire on any
  // other button. Same pattern as KeyboardEntryActivity.
  static constexpr uint16_t LONG_PRESS_MS = 500;
  bool confirmHeld = false;
  bool confirmLongHandled = false;

  bool parseHomeLocation(double& lat, double& lon) const;
  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void fetchFlights();

  void renderNoLocation() const;
  void renderCheckWifiOrLoading() const;
  void renderError() const;
  void renderList();
  void renderRadar();
  // Shared by LIST and RADAR: short press acts, long press refreshes.
  // Returns true when the caller should stop processing input this frame.
  bool handleConfirmPressOrRefresh(bool hasMatches);
  void renderDetail() const;
};

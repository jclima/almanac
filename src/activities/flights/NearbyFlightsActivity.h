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

  // DETAIL is a state within this same Activity, not a pushed child, so there
  // is no back-stack to return through -- whichever code sends `state` to
  // DETAIL must also record where Back should land. Every assignment of
  // FlightsState::DETAIL must set this immediately beforehand. Defaults to
  // LIST so any path that somehow reaches DETAIL without doing so still
  // behaves as it always has.
  FlightsState detailReturnState = FlightsState::LIST;

  // Same problem, for fetchFlights(): a successful fetch used to always land
  // on LIST because LIST was the only view that could trigger one. RADAR's
  // long-press Refresh now triggers the same fetch, so fetchFlights() must
  // land back on whichever view asked for it. Only ever set to LIST or RADAR,
  // and only at the two points where the user was actually looking at one of
  // those views (the long-press handler, and onEnter's cold start). It is
  // deliberately left untouched by checkAndConnectWifi(),
  // launchWifiSelection(), onWifiSelectionComplete(), and the ERROR-state
  // retry -- those are all just legs of the same fetch attempt, and must
  // preserve whichever view originally asked for it. Defaults to LIST so the
  // first-ever fetch (before any view has been shown) lands there.
  FlightsState fetchReturnState = FlightsState::LIST;

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

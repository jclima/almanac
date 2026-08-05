#pragma once

#include "OpenSkyStatesParser.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class NearbyFlightsActivity final : public Activity {
 public:
  enum class FlightsState { NO_LOCATION, CHECK_WIFI, WIFI_SELECTION, LOADING, LIST, DETAIL, ERROR };

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

  bool parseHomeLocation(double& lat, double& lon) const;
  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void fetchFlights();

  void renderNoLocation() const;
  void renderCheckWifiOrLoading() const;
  void renderError() const;
  void renderList();
  void renderDetail() const;
};

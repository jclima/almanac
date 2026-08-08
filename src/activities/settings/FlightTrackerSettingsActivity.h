#pragma once

#include <string>

#include "ZipGeocodeParser.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Four-row settings screen: home zip code (geocoded via Zippopotam on
// submit), home latitude, home longitude (both free-text decimal degrees
// via the keyboard, and the fallback if the zip lookup fails or the user is
// outside the US), and search radius (tap-to-cycle).
class FlightTrackerSettingsActivity final : public Activity {
 public:
  explicit FlightTrackerSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlightTrackerSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int ITEM_COUNT = 4;
  // How long a rejected-input or lookup-result message stays on screen
  // before loop() clears it.
  static constexpr unsigned long ERROR_MESSAGE_DURATION_MS = 3000;

  enum class ZipLookupState { IDLE, CHECK_WIFI, WIFI_SELECTION, LOADING };

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  std::string errorMessage;
  unsigned long errorShownAt = 0;

  ZipLookupState zipLookupState = ZipLookupState::IDLE;
  char pendingZip[6] = {0};  // zip currently being looked up, set on commit
  ZipGeocodeParser zipParser;
  // Set true only inside performZipLookup(), i.e. only once this screen has
  // actually made an HTTP/TLS request. Gates onExit()'s teardown -- see its
  // comment for why WiFi.getMode() alone is the wrong condition here.
  bool wifiUsedThisSession = false;

  void handleSelection();
  void rejectCoordinate(const std::string& text);
  void rejectZip(const std::string& text);
  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void performZipLookup();
};

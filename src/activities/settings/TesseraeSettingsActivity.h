#pragma once

#include <string>

#include "activities/Activity.h"
#include "network/TesseraeClient.h"
#include "util/ButtonNavigator.h"

class TesseraeSettingsActivity final : public Activity {
 public:
  explicit TesseraeSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TesseraeSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::Connecting; }

 private:
  enum class State { Menu, Connecting, Success, WaitingApproval, Failed };

  static constexpr int ITEM_COUNT = 5;

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  State state = State::Menu;
  std::string pairingCode;
  std::string message;
  std::string menuError;

  void handleSelection();
  void beginConnection(std::string code);
  void onWifiSelectionComplete(bool connected);
  void performConnection();
  void showResult(TesseraeClient::Result result);
};

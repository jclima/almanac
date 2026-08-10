#pragma once

#include "Game2048.h"
#include "activities/Activity.h"

class Game2048Activity final : public Activity {
 public:
  explicit Game2048Activity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Game2048", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Applies a move and, when the board actually changed, schedules a repaint.
  // A no-op move must not repaint: an e-ink frame costs hundreds of ms.
  void applyMove(Game2048::Direction d);
  void startNewGame();

  Game2048 game;
  // Counts down to the next de-ghosting pass. FAST_REFRESH is a differential
  // waveform, so consecutive fast paints accumulate residue.
  uint8_t movesUntilDeghost = MOVES_PER_DEGHOST;
  bool dirty = false;

  static constexpr uint8_t MOVES_PER_DEGHOST = 15;
};

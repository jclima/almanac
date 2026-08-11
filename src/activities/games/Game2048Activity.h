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
  // waveform, so consecutive fast paints accumulate residue. Starts at 1, not
  // MOVES_PER_DEGHOST: onEnter() only calls startNewGame() (which also forces
  // this to 1) on the no-save, corrupt-save, and game-over paths. A board
  // that restores successfully -- the normal case on every entry after the
  // first game -- skips startNewGame() entirely, so this initialiser is what
  // forces that first paint to de-ghost over whatever Home left on the panel.
  uint8_t movesUntilDeghost = 1;
  bool dirty = false;

  static constexpr uint8_t MOVES_PER_DEGHOST = 15;
};

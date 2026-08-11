#pragma once

#include <stdint.h>

// 2048's board and rules, with no hardware dependency of any kind so the merge
// logic — the only genuinely error-prone part of the game — can be tested on
// the host. Randomness is injected rather than called directly for the same
// reason: tests need a deterministic sequence.
class Game2048 {
 public:
  static constexpr uint8_t SIZE = 4;
  static constexpr uint8_t CELLS = SIZE * SIZE;

  // Cells store the exponent, not the value: n means a tile of 2^n, 0 means
  // empty. The whole board is therefore 16 bytes. 2^17 is far past any
  // reachable tile, so uint8_t is never near overflowing.
  static constexpr uint8_t MAX_EXPONENT = 17;
  static constexpr uint8_t WIN_EXPONENT = 11;  // 2^11 == 2048

  enum class Direction : uint8_t { Left, Right, Up, Down };
  enum class Status : uint8_t { Playing, GameOver };

  // Returns a uniformly distributed value. esp_random on device, a fixed
  // sequence in tests.
  using RandomFn = uint32_t (*)(void* ctx);

  // Clears the board, zeroes the score, and spawns the two opening tiles.
  void reset(RandomFn rng, void* ctx);

  // Applies a move. Returns false when nothing shifted or merged, in which
  // case no tile is spawned and the caller must NOT refresh the display —
  // otherwise every blocked swipe would cost an e-ink frame.
  bool move(Direction d, RandomFn rng, void* ctx);

  // Reinstates a saved board. Returns false and leaves this object untouched
  // when the data is not a legal board, so a corrupt save cannot come back as
  // a half-valid game.
  bool restore(const uint8_t (&savedCells)[CELLS], uint32_t savedScore);

  uint8_t exponentAt(uint8_t row, uint8_t col) const { return cells[row * SIZE + col]; }
  uint32_t score() const { return currentScore; }
  Status status() const { return currentStatus; }

  // True once 2048 has been reached. Winning is deliberately not terminal —
  // only GameOver is — so play continues afterwards.
  bool hasWon() const { return won; }

  // True while any move would change the board.
  bool canMove() const;

  // Raw board access for the save store.
  const uint8_t* rawCells() const { return cells; }

 private:
  // Shifts and merges without spawning or updating status. Returns true when
  // the board changed.
  bool slide(Direction d);
  void spawnTile(RandomFn rng, void* ctx);

  uint8_t cells[CELLS] = {};
  uint32_t currentScore = 0;
  Status currentStatus = Status::Playing;
  bool won = false;
};

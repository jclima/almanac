#include "Game2048.h"

namespace {

// Flat index of the cell at position `pos` along `line`, walking in the
// direction of travel. This is what lets one slide() handle all four
// directions instead of four near-identical copies.
constexpr uint8_t indexFor(Game2048::Direction d, uint8_t line, uint8_t pos) {
  switch (d) {
    case Game2048::Direction::Left:
      return static_cast<uint8_t>(line * Game2048::SIZE + pos);
    case Game2048::Direction::Right:
      return static_cast<uint8_t>(line * Game2048::SIZE + (Game2048::SIZE - 1 - pos));
    case Game2048::Direction::Up:
      return static_cast<uint8_t>(pos * Game2048::SIZE + line);
    case Game2048::Direction::Down:
      return static_cast<uint8_t>((Game2048::SIZE - 1 - pos) * Game2048::SIZE + line);
  }
  return 0;
}

}  // namespace

bool Game2048::slide(const Direction d) {
  bool moved = false;

  for (uint8_t line = 0; line < SIZE; ++line) {
    // Compact the lane, dropping gaps.
    uint8_t lane[SIZE] = {};
    uint8_t n = 0;
    for (uint8_t p = 0; p < SIZE; ++p) {
      const uint8_t v = cells[indexFor(d, line, p)];
      if (v != 0) lane[n++] = v;
    }

    // Merge equal neighbours once each, scanning from the leading edge. The
    // shift-down after a merge is what stops a freshly merged tile from
    // merging again this move.
    for (uint8_t p = 0; p + 1 < n; ++p) {
      if (lane[p] != lane[p + 1]) continue;
      lane[p] = static_cast<uint8_t>(lane[p] + 1);
      currentScore += (1u << lane[p]);
      if (lane[p] >= WIN_EXPONENT) won = true;
      for (uint8_t q = static_cast<uint8_t>(p + 1); q + 1 < n; ++q) lane[q] = lane[q + 1];
      lane[--n] = 0;
    }

    // Write the lane back, noting whether anything actually changed.
    for (uint8_t p = 0; p < SIZE; ++p) {
      const uint8_t v = (p < n) ? lane[p] : 0;
      const uint8_t idx = indexFor(d, line, p);
      if (cells[idx] != v) {
        cells[idx] = v;
        moved = true;
      }
    }
  }

  return moved;
}

void Game2048::spawnTile(const RandomFn rng, void* ctx) {
  uint8_t empties[CELLS];
  uint8_t n = 0;
  for (uint8_t i = 0; i < CELLS; ++i) {
    if (cells[i] == 0) empties[n++] = i;
  }
  if (n == 0) return;

  const uint8_t slot = empties[rng(ctx) % n];
  // One in ten spawns is a 4, matching the original game.
  cells[slot] = (rng(ctx) % 10 == 0) ? 2 : 1;
}

void Game2048::reset(const RandomFn rng, void* ctx) {
  for (uint8_t i = 0; i < CELLS; ++i) cells[i] = 0;
  currentScore = 0;
  currentStatus = Status::Playing;
  won = false;
  spawnTile(rng, ctx);
  spawnTile(rng, ctx);
}

bool Game2048::move(const Direction d, const RandomFn rng, void* ctx) {
  if (currentStatus == Status::GameOver) return false;
  if (!slide(d)) return false;

  spawnTile(rng, ctx);
  if (!canMove()) currentStatus = Status::GameOver;
  return true;
}

bool Game2048::canMove() const {
  for (uint8_t i = 0; i < CELLS; ++i) {
    if (cells[i] == 0) return true;
  }
  for (uint8_t r = 0; r < SIZE; ++r) {
    for (uint8_t c = 0; c < SIZE; ++c) {
      const uint8_t v = cells[r * SIZE + c];
      if (c + 1 < SIZE && cells[r * SIZE + c + 1] == v) return true;
      if (r + 1 < SIZE && cells[(r + 1) * SIZE + c] == v) return true;
    }
  }
  return false;
}

bool Game2048::restore(const uint8_t (&savedCells)[CELLS], const uint32_t savedScore) {
  // Validate before mutating: a partial restore would leave a board that is
  // neither the save nor a fresh game.
  for (uint8_t i = 0; i < CELLS; ++i) {
    if (savedCells[i] > MAX_EXPONENT) return false;
  }

  won = false;
  for (uint8_t i = 0; i < CELLS; ++i) {
    cells[i] = savedCells[i];
    if (cells[i] >= WIN_EXPONENT) won = true;
  }
  currentScore = savedScore;
  currentStatus = canMove() ? Status::Playing : Status::GameOver;
  return true;
}

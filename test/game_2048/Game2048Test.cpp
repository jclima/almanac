#include <gtest/gtest.h>

#include "Game2048.h"

namespace {

// Deterministic RNG: returns a fixed sequence so spawn positions are
// reproducible. A real random source would make every assertion below flaky.
struct SeqRng {
  const uint32_t* values;
  size_t count;
  size_t pos = 0;
};

uint32_t seqNext(void* ctx) {
  auto* r = static_cast<SeqRng*>(ctx);
  return r->values[r->pos++ % r->count];
}

// Build a board directly, bypassing play, so each rule can be tested in
// isolation. Values are exponents: 0 empty, 1 = tile "2", 2 = tile "4".
Game2048 boardOf(const uint8_t (&cells)[Game2048::CELLS], uint32_t score = 0) {
  Game2048 g;
  EXPECT_TRUE(g.restore(cells, score));
  return g;
}

uint32_t zeroRng(void*) { return 0; }

}  // namespace

TEST(Game2048Move, MergesOnlyTheLeadingPair) {
  // The classic wrong answer here is [8]. Two 2s merge, the 4 does not join in.
  const uint8_t cells[Game2048::CELLS] = {1, 1, 2, 0,  //
                                          0, 0, 0, 0,  //
                                          0, 0, 0, 0,  //
                                          0, 0, 0, 0};
  Game2048 g = boardOf(cells);
  EXPECT_TRUE(g.move(Game2048::Direction::Left, zeroRng, nullptr));
  EXPECT_EQ(g.exponentAt(0, 0), 2);  // 4
  EXPECT_EQ(g.exponentAt(0, 1), 2);  // 4
}

TEST(Game2048Move, MergedTileCannotMergeAgainInSameMove) {
  // [2,2,4,4] -> [4,8], never [16].
  const uint8_t cells[Game2048::CELLS] = {1, 1, 2, 2,  //
                                          0, 0, 0, 0,  //
                                          0, 0, 0, 0,  //
                                          0, 0, 0, 0};
  Game2048 g = boardOf(cells);
  EXPECT_TRUE(g.move(Game2048::Direction::Left, zeroRng, nullptr));
  EXPECT_EQ(g.exponentAt(0, 0), 2);  // 4
  EXPECT_EQ(g.exponentAt(0, 1), 3);  // 8
}

TEST(Game2048Move, NoOpMoveReturnsFalseAndSpawnsNothing) {
  // Column already packed left with no equal neighbours: moving left changes
  // nothing, so no tile may appear and no refresh should be triggered.
  const uint8_t cells[Game2048::CELLS] = {1, 2, 3, 4,  //
                                          0, 0, 0, 0,  //
                                          0, 0, 0, 0,  //
                                          0, 0, 0, 0};
  Game2048 g = boardOf(cells);
  EXPECT_FALSE(g.move(Game2048::Direction::Left, zeroRng, nullptr));
  EXPECT_EQ(g.exponentAt(0, 0), 1);
  EXPECT_EQ(g.exponentAt(1, 0), 0);
}

TEST(Game2048Move, ScoreIncreasesByMergedTileValue) {
  const uint8_t cells[Game2048::CELLS] = {2, 2, 0, 0,  //
                                          0, 0, 0, 0,  //
                                          0, 0, 0, 0,  //
                                          0, 0, 0, 0};
  Game2048 g = boardOf(cells);
  EXPECT_TRUE(g.move(Game2048::Direction::Left, zeroRng, nullptr));
  EXPECT_EQ(g.score(), 8u);  // two 4s merged into an 8
}

TEST(Game2048Move, MovesUpAndCompactsColumns) {
  const uint8_t cells[Game2048::CELLS] = {0, 0, 0, 0,  //
                                          1, 0, 0, 0,  //
                                          0, 0, 0, 0,  //
                                          1, 0, 0, 0};
  Game2048 g = boardOf(cells);
  EXPECT_TRUE(g.move(Game2048::Direction::Up, zeroRng, nullptr));
  EXPECT_EQ(g.exponentAt(0, 0), 2);  // merged into a 4 at the top
}

TEST(Game2048Status, FullBoardWithAdjacentPairIsStillPlayable) {
  // Every cell occupied, but the two 2s at the end are adjacent.
  const uint8_t cells[Game2048::CELLS] = {1, 2, 3, 4,  //
                                          2, 3, 4, 5,  //
                                          3, 4, 5, 6,  //
                                          4, 5, 1, 1};
  Game2048 g = boardOf(cells);
  EXPECT_TRUE(g.canMove());
  EXPECT_EQ(g.status(), Game2048::Status::Playing);
}

TEST(Game2048Status, FullBoardWithNoEqualNeighboursIsGameOver) {
  const uint8_t cells[Game2048::CELLS] = {1, 2, 1, 2,  //
                                          2, 1, 2, 1,  //
                                          1, 2, 1, 2,  //
                                          2, 1, 2, 1};
  Game2048 g = boardOf(cells);
  EXPECT_FALSE(g.canMove());
}

TEST(Game2048Status, WinIsNotTerminal) {
  // Merging two 1024s makes 2048; the game must keep accepting moves.
  const uint8_t cells[Game2048::CELLS] = {10, 10, 0, 0,  //
                                          0,  0,  0, 0,  //
                                          0,  0,  0, 0,  //
                                          0,  0,  0, 0};
  Game2048 g = boardOf(cells);
  EXPECT_FALSE(g.hasWon());
  EXPECT_TRUE(g.move(Game2048::Direction::Left, zeroRng, nullptr));
  EXPECT_TRUE(g.hasWon());
  EXPECT_EQ(g.status(), Game2048::Status::Playing);
}

TEST(Game2048Reset, StartsWithExactlyTwoTiles) {
  const uint32_t seq[] = {0, 1, 3, 1};
  SeqRng rng{seq, 4};
  Game2048 g;
  g.reset(seqNext, &rng);

  int occupied = 0;
  for (uint8_t r = 0; r < Game2048::SIZE; ++r) {
    for (uint8_t c = 0; c < Game2048::SIZE; ++c) {
      const uint8_t e = g.exponentAt(r, c);
      if (e != 0) {
        ++occupied;
        EXPECT_TRUE(e == 1 || e == 2) << "spawned tile must be 2 or 4";
      }
    }
  }
  EXPECT_EQ(occupied, 2);
  EXPECT_EQ(g.score(), 0u);
}

TEST(Game2048Restore, RejectsOutOfRangeExponent) {
  uint8_t cells[Game2048::CELLS] = {};
  cells[0] = Game2048::MAX_EXPONENT + 1;
  Game2048 g;
  EXPECT_FALSE(g.restore(cells, 0));
  // Board must be untouched by a rejected restore.
  EXPECT_EQ(g.exponentAt(0, 0), 0);
}

TEST(Game2048Restore, ClearsStaleWinFlagFromReusedInstance) {
  // A long-lived instance (the natural pattern: one Game2048 member, reused
  // across restore() calls) must not carry a win flag from a previous board
  // into a restore() of a board that never reached WIN_EXPONENT (11).
  const uint8_t wonCells[Game2048::CELLS] = {11, 0, 0, 0,  //
                                             0,  0, 0, 0,  //
                                             0,  0, 0, 0,  //
                                             0,  0, 0, 0};
  Game2048 g = boardOf(wonCells);
  ASSERT_TRUE(g.hasWon());

  const uint8_t freshCells[Game2048::CELLS] = {1, 0, 0, 0,  //
                                               0, 0, 0, 0,  //
                                               0, 0, 0, 0,  //
                                               0, 0, 0, 0};
  EXPECT_TRUE(g.restore(freshCells, 0));
  EXPECT_FALSE(g.hasWon());
}

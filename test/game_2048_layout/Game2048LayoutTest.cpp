#include <gtest/gtest.h>

#include "components/themes/Game2048Layout.h"

namespace {

// The four orientations produce these content bands on the two panels. Width
// and height swap between portrait and landscape; the insets stand in for a
// header above and a button-hints bar below.
struct Band {
  const char* name;
  Rect bounds;
};

const Band kBands[] = {
    {"800x480 landscape", Rect{0, 60, 800, 360}},
    {"480x800 portrait", Rect{0, 60, 480, 680}},
    {"800x480 landscape inverted", Rect{0, 60, 800, 360}},
    {"480x800 portrait inverted", Rect{0, 60, 480, 680}},
};

}  // namespace

TEST(Game2048Layout, BoardFitsInsideBoundsInEveryOrientation) {
  for (const Band& band : kBands) {
    const auto g = Game2048Layout::boardGeometry(band.bounds);
    for (int r = 0; r < 4; ++r) {
      for (int c = 0; c < 4; ++c) {
        const Rect t = Game2048Layout::tileRect(g, r, c);
        EXPECT_GE(t.x, band.bounds.x) << band.name;
        EXPECT_GE(t.y, band.bounds.y) << band.name;
        EXPECT_LE(t.x + t.width, band.bounds.x + band.bounds.width) << band.name;
        EXPECT_LE(t.y + t.height, band.bounds.y + band.bounds.height) << band.name;
        EXPECT_GT(t.width, 0) << band.name;
        EXPECT_GT(t.height, 0) << band.name;
      }
    }
  }
}

TEST(Game2048Layout, TilesDoNotOverlap) {
  const auto g = Game2048Layout::boardGeometry(Rect{0, 60, 800, 360});
  // Adjacent tiles are separated by exactly one gap.
  const Rect a = Game2048Layout::tileRect(g, 0, 0);
  const Rect b = Game2048Layout::tileRect(g, 0, 1);
  EXPECT_EQ(b.x - (a.x + a.width), Game2048Layout::GAP);

  const Rect below = Game2048Layout::tileRect(g, 1, 0);
  EXPECT_EQ(below.y - (a.y + a.height), Game2048Layout::GAP);
}

TEST(Game2048Layout, BoardIsSquareAndCentred) {
  const Rect bounds{0, 60, 800, 360};
  const auto g = Game2048Layout::boardGeometry(bounds);
  const Rect topLeft = Game2048Layout::tileRect(g, 0, 0);
  const Rect bottomRight = Game2048Layout::tileRect(g, 3, 3);

  const int spanX = (bottomRight.x + bottomRight.width) - topLeft.x;
  const int spanY = (bottomRight.y + bottomRight.height) - topLeft.y;
  EXPECT_EQ(spanX, spanY) << "board must be square";

  const int leftMargin = topLeft.x - bounds.x;
  const int rightMargin = (bounds.x + bounds.width) - (bottomRight.x + bottomRight.width);
  EXPECT_LE(leftMargin - rightMargin, 2 * Game2048Layout::GAP);
}

TEST(Game2048Layout, GeometryIsCompileTimeConstant) {
  // constexpr placement keeps the arithmetic in flash rather than costing DRAM.
  constexpr auto g = Game2048Layout::boardGeometry(Rect{0, 60, 800, 360});
  static_assert(g.tileSize > 0, "board geometry must be constexpr-evaluable");
  EXPECT_GT(g.tileSize, 0);
}

#pragma once

#include "components/themes/BaseTheme.h"

// Board geometry for the 2048 activity.
//
// Kept here — pure, constexpr, flash-resident — for the same reason as
// MenuLayout: the numbers are checkable on the host (see
// test/game_2048_layout/) instead of being buried in render() where an
// orientation bug would only show up on a device.
namespace Game2048Layout {

// Space between tiles, and between the board edge and the outer tiles.
constexpr int GAP = 6;
constexpr int GRID = 4;

struct BoardGeometry {
  int originX;
  int originY;
  int tilePitch;  // tile size plus one gap
  int tileSize;
};

// The largest square board that fits inside `bounds`, centred within it.
constexpr BoardGeometry boardGeometry(const Rect& bounds) {
  const int shorter = (bounds.width < bounds.height) ? bounds.width : bounds.height;
  const int pitch = (shorter - GAP) / GRID;
  const int side = pitch * GRID + GAP;
  return BoardGeometry{
      bounds.x + (bounds.width - side) / 2,
      bounds.y + (bounds.height - side) / 2,
      pitch,
      pitch - GAP,
  };
}

constexpr Rect tileRect(const BoardGeometry& g, int row, int col) {
  return Rect{g.originX + GAP + col * g.tilePitch, g.originY + GAP + row * g.tilePitch, g.tileSize, g.tileSize};
}

}  // namespace Game2048Layout

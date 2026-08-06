#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "components/themes/MenuLayout.h"
#include "components/themes/almanac/AlmanacTheme.h"
#include "components/themes/lyra/Lyra3CoversTheme.h"
#include "components/themes/lyra/LyraTheme.h"
#include "components/themes/roundedraff/RoundedRaffTheme.h"

// The home menu must never draw a row into the button-hints bar. These tests
// pin the arithmetic in MenuLayout.h against the real metrics of every shipped
// theme, at both menu sizes the firmware can produce.
//
// Portrait is the only orientation these need to cover: HomeActivity never
// changes the renderer orientation, and the only activities that do (the
// readers, via ReaderUtils::applyOrientation) restore Portrait on exit.
//
// Known limitation: these model the drawing geometry rather than calling
// drawButtonMenu, which needs a GfxRenderer. They pin the shared arithmetic in
// MenuLayout.h and the metrics each theme feeds it; they would not catch a
// change made inside a theme's drawButtonMenu body.

namespace {

// X4 portrait logical dimensions (GfxRenderer::getScreenHeight() returns
// panelWidth in Portrait). Height is what the menu layout depends on.
constexpr int kPortraitHeight = 800;

// HomeActivity builds the menu from 5 fixed entries, plus "Continue Reading"
// when the theme hosts it in the menu and a recent book exists, plus the OPDS
// browser when at least one OPDS server is configured.
constexpr int menuItemCount(const ThemeMetrics& metrics, const bool hasRecentBook, const bool hasOpdsServers) {
  return 5 + ((metrics.homeContinueReadingInMenu && hasRecentBook) ? 1 : 0) + (hasOpdsServers ? 1 : 0);
}

// Geometry of the last row as BaseTheme and LyraTheme draw it: uniform rows at
// a fitted pitch, every row drawn.
int lastRowBottomUniform(const ThemeMetrics& metrics, const int rowCount) {
  const int top = MenuLayout::menuTop(metrics);
  const int available = MenuLayout::availableHeight(metrics, kPortraitHeight);
  const int step = MenuLayout::fittedRowStep(available, metrics.menuRowHeight,
                                             metrics.menuRowHeight + metrics.menuSpacing, rowCount);
  return MenuLayout::lastRowBottom(top, metrics.menuRowHeight, step, rowCount);
}

// RoundedRaffTheme sizes rows from the font (getLineHeight(UI_12) + 20, with a
// 6px gap) and paginates rather than compressing, so only pageItems rows land
// on screen at once.
constexpr int kRoundedRaffRowHeight = 29 + 20;  // ubuntu_12_regular advanceY + 10px padding top/bottom
constexpr int kRoundedRaffRowGap = 6;

int lastRowBottomRoundedRaff(const int rowCount) {
  const auto& metrics = RoundedRaffMetrics::values;
  const int top = MenuLayout::menuTop(metrics);
  const int available = MenuLayout::availableHeight(metrics, kPortraitHeight);
  const int step = kRoundedRaffRowHeight + kRoundedRaffRowGap;
  const int pageItems = std::max(1, available / step);
  const int drawnRows = std::min(rowCount, pageItems);
  return MenuLayout::lastRowBottom(top, kRoundedRaffRowHeight, step, drawnRows);
}

void expectClearOfHints(const ThemeMetrics& metrics, const int lastBottom, const std::string& what) {
  const int hintsTop = MenuLayout::buttonHintsTop(metrics, kPortraitHeight);
  EXPECT_LE(lastBottom, hintsTop) << what << ": last row bottom " << lastBottom << " overlaps the button-hints bar at "
                                  << hintsTop;
}

}  // namespace

// --- Classic (BaseTheme / BaseMetrics) --------------------------------------

TEST(HomeMenuLayout, ClassicFitsWithoutOpds) {
  const auto& m = BaseMetrics::values;
  const int count = menuItemCount(m, true, false);
  EXPECT_EQ(count, 5);
  expectClearOfHints(m, lastRowBottomUniform(m, count), "Classic 5 items");
}

// The regression: one OPDS server pushes Classic to 6 rows.
TEST(HomeMenuLayout, ClassicFitsWithOpds) {
  const auto& m = BaseMetrics::values;
  const int count = menuItemCount(m, true, true);
  EXPECT_EQ(count, 6);
  expectClearOfHints(m, lastRowBottomUniform(m, count), "Classic 6 items");
}

// BaseTheme used to inset its first row by verticalSpacing on top of the
// homeMenuTopOffset already baked into menuTop(). That extra 10px is exactly
// what pushed Classic's sixth row past the hints bar, and it also put every
// drawn row 10px below where HomeActivity hit-tests it. The menu must start at
// menuTop().
TEST(HomeMenuLayout, ClassicLeadingInsetWouldOverflowHints) {
  const auto& m = BaseMetrics::values;
  const int top = MenuLayout::menuTop(m);
  const int step = m.menuRowHeight + m.menuSpacing;
  const int hintsTop = MenuLayout::buttonHintsTop(m, kPortraitHeight);

  EXPECT_LE(MenuLayout::lastRowBottom(top, m.menuRowHeight, step, 6), hintsTop);
  EXPECT_GT(MenuLayout::lastRowBottom(top + m.verticalSpacing, m.menuRowHeight, step, 6), hintsTop);
}

// --- Lyra --------------------------------------------------------------------

TEST(HomeMenuLayout, LyraFitsWithoutOpds) {
  const auto& m = LyraMetrics::values;
  expectClearOfHints(m, lastRowBottomUniform(m, menuItemCount(m, true, false)), "Lyra 5 items");
}

TEST(HomeMenuLayout, LyraFitsWithOpds) {
  const auto& m = LyraMetrics::values;
  expectClearOfHints(m, lastRowBottomUniform(m, menuItemCount(m, true, true)), "Lyra 6 items");
}

// Lyra already clears the hints bar at 6 rows, so the fitting logic must leave
// its natural pitch alone.
TEST(HomeMenuLayout, LyraKeepsNaturalPitch) {
  const auto& m = LyraMetrics::values;
  const int available = MenuLayout::availableHeight(m, kPortraitHeight);
  const int natural = m.menuRowHeight + m.menuSpacing;
  EXPECT_EQ(MenuLayout::fittedRowStep(available, m.menuRowHeight, natural, 6), natural);
}

// --- Lyra3Covers -------------------------------------------------------------

TEST(HomeMenuLayout, Lyra3CoversFitsWithoutOpds) {
  const auto& m = Lyra3CoversMetrics::values;
  expectClearOfHints(m, lastRowBottomUniform(m, menuItemCount(m, true, false)), "Lyra3Covers 5 items");
}

// Lyra3Covers has the tallest cover tile of the Lyra pair, so its 6-row menu is
// the case that actually needs the gaps compressed.
TEST(HomeMenuLayout, Lyra3CoversFitsWithOpds) {
  const auto& m = Lyra3CoversMetrics::values;
  expectClearOfHints(m, lastRowBottomUniform(m, menuItemCount(m, true, true)), "Lyra3Covers 6 items");
}

TEST(HomeMenuLayout, Lyra3CoversCompressesGapsAtSixRows) {
  const auto& m = Lyra3CoversMetrics::values;
  const int available = MenuLayout::availableHeight(m, kPortraitHeight);
  const int natural = m.menuRowHeight + m.menuSpacing;
  const int step = MenuLayout::fittedRowStep(available, m.menuRowHeight, natural, 6);
  EXPECT_LT(step, natural);
  // Rows abut at worst -- never overlap.
  EXPECT_GE(step, m.menuRowHeight);
}

// --- RoundedRaff -------------------------------------------------------------

// RoundedRaff hosts Continue Reading in the menu, so it reaches 6 rows without
// OPDS and 7 with it.
TEST(HomeMenuLayout, RoundedRaffFitsWithoutOpds) {
  const auto& m = RoundedRaffMetrics::values;
  const int count = menuItemCount(m, true, false);
  EXPECT_EQ(count, 6);
  expectClearOfHints(m, lastRowBottomRoundedRaff(count), "RoundedRaff 6 items");
}

TEST(HomeMenuLayout, RoundedRaffFitsWithOpds) {
  const auto& m = RoundedRaffMetrics::values;
  const int count = menuItemCount(m, true, true);
  EXPECT_EQ(count, 7);
  expectClearOfHints(m, lastRowBottomRoundedRaff(count), "RoundedRaff 7 items");
}

// RoundedRaff is the one theme whose drawButtonMenu does not consult
// getMenuRowStep -- it paginates at a font-derived pitch instead. Its
// hit-test still goes through the shared helper, so the helper must hand back
// exactly the natural pitch HomeActivity used before this was factored out,
// or the touch targets move under a theme this change claims not to touch.
// The margin is thin: at 7 rows the fit works out to (335 - 42) / 6 = 48.83,
// truncating to precisely the natural 48. Pinned rather than reasoned about.
TEST(HomeMenuLayout, RoundedRaffHitTestPitchIsUnchanged) {
  const auto& m = RoundedRaffMetrics::values;
  const int available = MenuLayout::availableHeight(m, kPortraitHeight);
  const int natural = m.menuRowHeight + m.menuSpacing;
  for (int rows = 1; rows <= 7; ++rows) {
    EXPECT_EQ(MenuLayout::fittedRowStep(available, m.menuRowHeight, natural, rows), natural)
        << "RoundedRaff hit-test pitch changed at " << rows << " rows";
  }
}

// --- Almanac -----------------------------------------------------------------

// Almanac is the default theme and the tightest of the uniform-row themes: its
// hints bar is 8px taller than Classic's, and its selected tile draws a stroke
// kMenuSelectionReserve px OUTSIDE the fill, so the last row needs that much
// clearance beyond itself. AlmanacTheme::getMenuRowStep reserves it; this
// mirrors that reservation rather than re-deriving the pitch.
namespace {
int lastRowBottomAlmanac(const int rowCount) {
  const auto& m = AlmanacMetrics::values;
  const int available =
      std::max(0, MenuLayout::availableHeight(m, kPortraitHeight) - AlmanacTheme::kMenuSelectionReserve);
  const int step = MenuLayout::fittedRowStep(available, m.menuRowHeight, m.menuRowHeight + m.menuSpacing, rowCount);
  return MenuLayout::lastRowBottom(MenuLayout::menuTop(m), m.menuRowHeight, step, rowCount);
}
}  // namespace

TEST(HomeMenuLayout, AlmanacFitsWithoutOpds) {
  const auto& m = AlmanacMetrics::values;
  const int count = menuItemCount(m, true, false);
  EXPECT_EQ(count, 5);
  // The selection stroke must clear the bar too, not just the tile.
  expectClearOfHints(m, lastRowBottomAlmanac(count) + AlmanacTheme::kMenuSelectionReserve, "Almanac 5 items");
}

TEST(HomeMenuLayout, AlmanacFitsWithOpds) {
  const auto& m = AlmanacMetrics::values;
  const int count = menuItemCount(m, true, true);
  EXPECT_EQ(count, 6);
  expectClearOfHints(m, lastRowBottomAlmanac(count) + AlmanacTheme::kMenuSelectionReserve, "Almanac 6 items");
}

// Red/green for the reserve itself: at 6 rows, budgeting only the tiles leaves
// the bottom tile fitting while its stroke still crosses into the bar -- by
// exactly 1px. This is why getMenuRowStep subtracts the reserve up front
// rather than the draw simply clamping afterwards.
TEST(HomeMenuLayout, AlmanacSelectionStrokeNeedsItsOwnReserve) {
  const auto& m = AlmanacMetrics::values;
  const int hintsTop = MenuLayout::buttonHintsTop(m, kPortraitHeight);
  const int top = MenuLayout::menuTop(m);
  const int natural = m.menuRowHeight + m.menuSpacing;

  const int unreserved =
      MenuLayout::fittedRowStep(MenuLayout::availableHeight(m, kPortraitHeight), m.menuRowHeight, natural, 6);
  EXPECT_EQ(MenuLayout::lastRowBottom(top, m.menuRowHeight, unreserved, 6) + AlmanacTheme::kMenuSelectionReserve,
            hintsTop + 1);

  EXPECT_LE(lastRowBottomAlmanac(6) + AlmanacTheme::kMenuSelectionReserve, hintsTop);
}

// Almanac draws its rows at rect.y, the same origin HomeActivity hit-tests
// from. The shipped version inset them by verticalSpacing, putting every drawn
// tile 10px below its own touch target.
TEST(HomeMenuLayout, AlmanacDrawOriginMatchesHitTestOrigin) {
  const auto& m = AlmanacMetrics::values;
  const int hintsTop = MenuLayout::buttonHintsTop(m, kPortraitHeight);
  const int inset = MenuLayout::menuTop(m) + m.verticalSpacing;
  const int step = m.menuRowHeight + m.menuSpacing;

  EXPECT_GT(MenuLayout::lastRowBottom(inset, m.menuRowHeight, step, 6) + AlmanacTheme::kMenuSelectionReserve, hintsTop);
}

// --- Shared invariants -------------------------------------------------------

// The available height has to be measured from the menu's own top edge, not
// from an unrelated stack of metrics. The shipped bug omitted the cover tile
// (242-400px) from the subtraction, which is what let rows run past the hints
// bar and defeated RoundedRaff's own pagination guard.
TEST(HomeMenuLayout, AvailableHeightSpansMenuTopToHintsBar) {
  for (const auto& m : {BaseMetrics::values, LyraMetrics::values, Lyra3CoversMetrics::values,
                        RoundedRaffMetrics::values, AlmanacMetrics::values}) {
    EXPECT_EQ(MenuLayout::availableHeight(m, kPortraitHeight),
              MenuLayout::buttonHintsTop(m, kPortraitHeight) - MenuLayout::menuTop(m));
  }
}

// fittedRowStep only compresses gaps -- it floors at rowHeight and never
// shrinks a row. That is sufficient precisely because the menu cannot grow
// past 6 rows on the themes that use it: only RoundedRaff sets
// homeContinueReadingInMenu, so for everyone else the recent books ride above
// the menu rather than in it, leaving 5 fixed entries plus OPDS. If a theme
// ever flips that flag, gap compression alone stops being enough and the fit
// needs a row-shrinking tier -- this pins the assumption so that change fails
// here rather than on a device.
TEST(HomeMenuLayout, UniformRowThemesCannotExceedSixRows) {
  for (const auto& m : {BaseMetrics::values, LyraMetrics::values, Lyra3CoversMetrics::values, AlmanacMetrics::values}) {
    EXPECT_FALSE(m.homeContinueReadingInMenu);
    EXPECT_EQ(menuItemCount(m, true, true), 6);
  }
  EXPECT_TRUE(RoundedRaffMetrics::values.homeContinueReadingInMenu);
}

TEST(HomeMenuLayout, FittedStepNeverExceedsNatural) {
  // A generous height must not stretch the menu beyond its designed pitch.
  EXPECT_EQ(MenuLayout::fittedRowStep(10000, 45, 53, 6), 53);
}

TEST(HomeMenuLayout, FittedStepDegradesSafelyWhenNothingFits) {
  // Negative/zero space must not produce an overlapping or negative pitch.
  EXPECT_GE(MenuLayout::fittedRowStep(0, 45, 53, 6), 45);
  EXPECT_GE(MenuLayout::fittedRowStep(-100, 45, 53, 6), 45);
  EXPECT_EQ(MenuLayout::fittedRowStep(10, 45, 53, 1), 53);
}

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "components/themes/MenuLayout.h"
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

// --- Almanac (planned, not yet implemented) ----------------------------------

// AlmanacTheme does not exist in the tree yet -- it is specified in
// docs/superpowers/plans/2026-08-05-almanac-theme.md, which copies
// BaseMetrics::values and changes exactly five fields. Only buttonHintsHeight
// (40 -> 48) moves the home menu vertically; headerHeight and contentSidePadding
// do not enter this layout, and the listRowHeight pair drives lists, not the
// button menu. The plan makes Almanac the default theme and has it override
// drawButtonMenu on BaseTheme's shape, so pinning the invariant here now proves
// the fix covers it and guards the work when it lands.
namespace AlmanacPlannedMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = BaseMetrics::values;
  v.headerHeight = 56;
  v.buttonHintsHeight = 48;
  v.listRowHeight = 34;
  v.listWithSubtitleRowHeight = 52;
  v.contentSidePadding = 16;
  return v;
}();
}  // namespace AlmanacPlannedMetrics

TEST(HomeMenuLayout, AlmanacPlannedFitsWithoutOpds) {
  const auto& m = AlmanacPlannedMetrics::values;
  expectClearOfHints(m, lastRowBottomUniform(m, menuItemCount(m, true, false)), "Almanac (planned) 5 items");
}

// Almanac's taller hints bar leaves less room than Classic, so this is the
// tightest of the uniform-row themes.
TEST(HomeMenuLayout, AlmanacPlannedFitsWithOpds) {
  const auto& m = AlmanacPlannedMetrics::values;
  expectClearOfHints(m, lastRowBottomUniform(m, menuItemCount(m, true, true)), "Almanac (planned) 6 items");
}

// --- Shared invariants -------------------------------------------------------

// The available height has to be measured from the menu's own top edge, not
// from an unrelated stack of metrics. The shipped bug omitted the cover tile
// (242-400px) from the subtraction, which is what let rows run past the hints
// bar and defeated RoundedRaff's own pagination guard.
TEST(HomeMenuLayout, AvailableHeightSpansMenuTopToHintsBar) {
  for (const auto& m :
       {BaseMetrics::values, LyraMetrics::values, Lyra3CoversMetrics::values, RoundedRaffMetrics::values}) {
    EXPECT_EQ(MenuLayout::availableHeight(m, kPortraitHeight),
              MenuLayout::buttonHintsTop(m, kPortraitHeight) - MenuLayout::menuTop(m));
  }
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

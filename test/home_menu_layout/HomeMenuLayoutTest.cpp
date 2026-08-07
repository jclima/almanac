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
  // Exact value pinned: this is the regression canary for the fit. 770 was the
  // shipped overlap; 760 lands flush with the hints bar (bottom is exclusive).
  EXPECT_EQ(lastRowBottomUniform(m, count), 760);
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
  // Exact value pinned alongside Classic's: 796 was the shipped overlap.
  EXPECT_EQ(lastRowBottomUniform(m, menuItemCount(m, true, true)), 756);
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

// RoundedRaff's drawn rows have never matched its ThemeMetrics: the drawn row
// is 49px tall on a 55px pitch (font-derived), against metrics that say 42 on
// 48. HomeActivity used to hit-test with the metrics figures, drifting ~7px
// per row. getButtonMenuLayout now reports the drawn geometry instead, so this
// pins the gap the fix closes rather than the old agreement.
TEST(HomeMenuLayout, RoundedRaffReportsDrawnGeometryNotMetrics) {
  const auto& m = RoundedRaffMetrics::values;
  const int metricsStep = m.menuRowHeight + m.menuSpacing;
  EXPECT_EQ(metricsStep, 48);
  EXPECT_EQ(kRoundedRaffRowHeight + kRoundedRaffRowGap, 55);
  // 7px of drift per row is what made row 3 swallow taps meant for row 2.
  EXPECT_NE(metricsStep, kRoundedRaffRowHeight + kRoundedRaffRowGap);
}

// --- Almanac -----------------------------------------------------------------

// Almanac is the default theme and the tightest of the uniform-row themes: its
// hints bar is 8px taller than Classic's, and its selected tile draws a stroke
// kMenuSelectionReserve px OUTSIDE the fill, so the last row needs that much
// clearance beyond itself. AlmanacTheme::getButtonMenuLayout reserves it; this
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
// exactly 1px. This is why getButtonMenuLayout subtracts the reserve up front
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

// --- Row reporting and paging (MenuLayout::menuRowLayout) --------------------
//
// The tests above pin that rows FIT (nothing collides with the button-hints
// bar). These pin that the reported rows are the rows actually drawn, which is
// what makes taps land on them. RoundedRaff is the interesting case: its row
// height follows the title font rather than ThemeMetrics, and it is the only
// theme that pages.

namespace {

// RoundedRaff's real home-menu geometry.
constexpr int kRrTop = 425;  // homeTopPadding 55 + homeCoverTileHeight 350 + homeMenuTopOffset 20
constexpr int kRrRowHeight = kRoundedRaffRowHeight;
constexpr int kRrRowStep = kRoundedRaffRowHeight + kRoundedRaffRowGap;
constexpr int kRrItemCount = 7;       // Continue Reading + Browse + Recent + OPDS + Transfer + Flights + Settings
constexpr int kRrPagingHeight = 280;  // yields pageItems = 5

// Mirrors MappedInputManager::rowTouch's hit predicate, so these assertions
// describe what a real tap resolves to. Returns the menu index, or -1 for a
// miss (below the last drawn row, or inside the gap between two rows).
int rowAt(const MenuRowLayout& layout, const int y) {
  if (layout.rowStep <= 0 || y < layout.top) return -1;
  const int r = (y - layout.top) / layout.rowStep;
  if (r >= layout.visibleCount) return -1;
  if (layout.rowHeight > 0 && (y - layout.top) % layout.rowStep >= layout.rowHeight) return -1;
  return layout.firstIndex + r;
}

}  // namespace

TEST(MenuRowLayout, ReportsPitchFromTheDrawnRowNotFromMetrics) {
  constexpr MenuRowLayout layout =
      MenuLayout::menuRowLayout(kRrTop, kRrPagingHeight, kRrRowHeight, kRrRowStep, kRrItemCount, 0, /*paginate=*/true);
  EXPECT_EQ(layout.top, kRrTop);
  EXPECT_EQ(layout.rowHeight, 49);  // not RoundedRaffMetrics' menuRowHeight of 42
  EXPECT_EQ(layout.rowStep, 55);    // not its menuRowHeight + menuSpacing of 48
}

TEST(MenuRowLayout, TapInsideDrawnRowResolvesToThatRow) {
  constexpr MenuRowLayout layout =
      MenuLayout::menuRowLayout(kRrTop, kRrPagingHeight, kRrRowHeight, kRrRowStep, kRrItemCount, 0, /*paginate=*/true);
  // Drawn row 2 spans y 535..583. Before this fix the hit-test used a 48px
  // pitch and resolved y=570 to row 3.
  EXPECT_EQ(rowAt(layout, 535), 2);
  EXPECT_EQ(rowAt(layout, 570), 2);
  EXPECT_EQ(rowAt(layout, 583), 2);
}

TEST(MenuRowLayout, GapBetweenRowsIsNotTappable) {
  constexpr MenuRowLayout layout =
      MenuLayout::menuRowLayout(kRrTop, kRrPagingHeight, kRrRowHeight, kRrRowStep, kRrItemCount, 0, /*paginate=*/true);
  EXPECT_EQ(rowAt(layout, kRrTop + kRrRowHeight), -1);      // first pixel of the gap
  EXPECT_EQ(rowAt(layout, kRrTop + kRrRowHeight + 5), -1);  // last pixel of the gap
  EXPECT_EQ(rowAt(layout, kRrTop + 55), 1);                 // first pixel of row 1
  EXPECT_EQ(rowAt(layout, kRrTop - 1), -1);                 // above the menu
}

TEST(MenuRowLayout, SecondPageStartsAtThePageBoundary) {
  constexpr MenuRowLayout layout =
      MenuLayout::menuRowLayout(kRrTop, kRrPagingHeight, kRrRowHeight, kRrRowStep, kRrItemCount, 5, /*paginate=*/true);
  EXPECT_EQ(layout.pageItems, 5);
  EXPECT_EQ(layout.firstIndex, 5);
  EXPECT_EQ(layout.visibleCount, 2);  // only items 5 and 6 remain
  // The topmost drawn row on page 2 is item 5, not item 0.
  EXPECT_EQ(rowAt(layout, kRrTop), 5);
  EXPECT_EQ(rowAt(layout, kRrTop + 55), 6);
}

TEST(MenuRowLayout, TapBelowAPartialLastPageIsRejected) {
  constexpr MenuRowLayout layout =
      MenuLayout::menuRowLayout(kRrTop, kRrPagingHeight, kRrRowHeight, kRrRowStep, kRrItemCount, 5, /*paginate=*/true);
  // Page 2 draws 2 of 5 possible rows; the empty space below them is not a row.
  EXPECT_EQ(rowAt(layout, kRrTop + 55 * 2), -1);
  EXPECT_EQ(rowAt(layout, kRrTop + 55 * 4), -1);
}

TEST(MenuRowLayout, NegativeSelectionClampsToTheFirstPage) {
  // HomeActivity passes selectorIndex - recentBooks.size() on themes that keep
  // Continue Reading out of the menu, which is negative while a recent book is
  // selected.
  constexpr MenuRowLayout layout =
      MenuLayout::menuRowLayout(kRrTop, kRrPagingHeight, kRrRowHeight, kRrRowStep, kRrItemCount, -1, /*paginate=*/true);
  EXPECT_EQ(layout.firstIndex, 0);
  EXPECT_EQ(layout.visibleCount, 5);
}

TEST(MenuRowLayout, ShortRectStillDrawsOneRow) {
  // pageItems floors at 1 so a rect too short for a single row does not divide
  // by zero or page infinitely.
  constexpr MenuRowLayout layout =
      MenuLayout::menuRowLayout(kRrTop, 10, kRrRowHeight, kRrRowStep, kRrItemCount, 3, /*paginate=*/true);
  EXPECT_EQ(layout.pageItems, 1);
  EXPECT_EQ(layout.firstIndex, 3);
  EXPECT_EQ(layout.visibleCount, 1);
}

TEST(MenuRowLayout, DegenerateInputsProduceNoTappableRows) {
  constexpr MenuRowLayout empty =
      MenuLayout::menuRowLayout(kRrTop, kRrPagingHeight, kRrRowHeight, kRrRowStep, 0, 0, /*paginate=*/true);
  EXPECT_EQ(empty.visibleCount, 0);
  EXPECT_EQ(empty.pageItems, 0);
  EXPECT_EQ(rowAt(empty, kRrTop), -1);

  constexpr MenuRowLayout zeroStep =
      MenuLayout::menuRowLayout(kRrTop, kRrPagingHeight, 0, 0, kRrItemCount, 0, /*paginate=*/true);
  EXPECT_EQ(zeroStep.visibleCount, 0);
  EXPECT_EQ(rowAt(zeroStep, kRrTop), -1);
}

// RoundedRaff's real rect (335px, not the 280 the paging cases above use)
// holds 6 of its 7 rows, so page 2 is reachable in the shipped configuration
// -- this is not a theoretical path.
TEST(MenuRowLayout, RoundedRaffRealGeometryPagesAtSevenItems) {
  const auto& m = RoundedRaffMetrics::values;
  const int available = MenuLayout::availableHeight(m, kPortraitHeight);
  EXPECT_EQ(available, 335);
  const MenuRowLayout page1 =
      MenuLayout::menuRowLayout(kRrTop, available, kRrRowHeight, kRrRowStep, kRrItemCount, 0, /*paginate=*/true);
  EXPECT_EQ(page1.pageItems, 6);
  EXPECT_EQ(page1.visibleCount, 6);
  const MenuRowLayout page2 =
      MenuLayout::menuRowLayout(kRrTop, available, kRrRowHeight, kRrRowStep, kRrItemCount, 6, /*paginate=*/true);
  EXPECT_EQ(page2.firstIndex, 6);
  EXPECT_EQ(page2.visibleCount, 1);
}

// The non-paginating themes must never drop a row: their fit clears the hints
// bar by compressing gaps, so paging would hide entries instead. Almanac is
// the one that matters most -- it is the default and the tightest.
TEST(MenuRowLayout, NonPaginatingThemesDrawEveryRowAtSix) {
  const auto& m = AlmanacMetrics::values;
  const int available =
      std::max(0, MenuLayout::availableHeight(m, kPortraitHeight) - AlmanacTheme::kMenuSelectionReserve);
  const int step = MenuLayout::fittedRowStep(available, m.menuRowHeight, m.menuRowHeight + m.menuSpacing, 6);
  const MenuRowLayout layout =
      MenuLayout::menuRowLayout(MenuLayout::menuTop(m), available, m.menuRowHeight, step, 6, 5, /*paginate=*/false);
  EXPECT_EQ(layout.firstIndex, 0);
  EXPECT_EQ(layout.visibleCount, 6);
  EXPECT_EQ(layout.pageItems, 6);  // == itemCount, so no row is ever off-page
}

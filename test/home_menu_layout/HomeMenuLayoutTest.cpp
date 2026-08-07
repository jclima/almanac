#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "components/themes/MenuLayout.h"
#include "components/themes/almanac/AlmanacTheme.h"

// The home menu must never draw a row into the button-hints bar. These tests
// pin the arithmetic in MenuLayout.h against the real metrics Almanac feeds
// it, at both menu sizes the firmware can produce, plus the paginate path of
// menuRowLayout() with fixture geometry inherited from the theme that used it
// (the retired RoundedRaff).
//
// Portrait is the only orientation these need to cover: HomeActivity never
// changes the renderer orientation, and the only activities that do (the
// readers, via ReaderUtils::applyOrientation) restore Portrait on exit.
//
// Known limitation: these model the drawing geometry rather than calling
// drawButtonMenu, which needs a GfxRenderer. They pin the shared arithmetic in
// MenuLayout.h and the metrics the theme feeds it; they would not catch a
// change made inside the theme's drawButtonMenu body.

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

void expectClearOfHints(const ThemeMetrics& metrics, const int lastBottom, const std::string& what) {
  const int hintsTop = MenuLayout::buttonHintsTop(metrics, kPortraitHeight);
  EXPECT_LE(lastBottom, hintsTop) << what << ": last row bottom " << lastBottom << " overlaps the button-hints bar at "
                                  << hintsTop;
}

constexpr int kPortraitWidth = 480;

MenuLayout::HomeComposition composition(const bool hasRecentBook, const bool hasOpds) {
  // Base entries: Browse Files, Recent Books, File Transfer, Nearby Flights, Settings.
  const int count = 5 + (hasOpds ? 1 : 0) + (hasRecentBook ? 1 : 0);
  return MenuLayout::HomeComposition{count, hasRecentBook};
}

bool overlaps(const Rect& a, const Rect& b) {
  return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

}  // namespace

// --- Home tile layout (MenuLayout::homeTileRect) -----------------------------

TEST(HomeTileLayout, EveryTileClearsTheMastheadAndHintsBar) {
  const ThemeMetrics& m = AlmanacMetrics::values;
  const int hintsTop = MenuLayout::buttonHintsTop(m, kPortraitHeight);

  for (const bool recent : {false, true}) {
    for (const bool opds : {false, true}) {
      const auto c = composition(recent, opds);
      for (int i = 0; i < c.tileCount; i++) {
        const Rect r = MenuLayout::homeTileRect(m, kPortraitWidth, kPortraitHeight, c, i);
        EXPECT_GE(r.y - AlmanacTheme::kMenuSelectionReserve, MenuLayout::kHomeMastheadHeight)
            << "tile " << i << " recent=" << recent << " opds=" << opds;
        EXPECT_LE(r.y + r.height + AlmanacTheme::kMenuSelectionReserve, hintsTop)
            << "tile " << i << " recent=" << recent << " opds=" << opds;
        EXPECT_GE(r.x, m.contentSidePadding);
        EXPECT_LE(r.x + r.width, kPortraitWidth - m.contentSidePadding);
      }
    }
  }
}

TEST(HomeTileLayout, NoTwoTilesOverlap) {
  const ThemeMetrics& m = AlmanacMetrics::values;
  for (const bool recent : {false, true}) {
    for (const bool opds : {false, true}) {
      const auto c = composition(recent, opds);
      for (int i = 0; i < c.tileCount; i++) {
        for (int j = i + 1; j < c.tileCount; j++) {
          const Rect a = MenuLayout::homeTileRect(m, kPortraitWidth, kPortraitHeight, c, i);
          const Rect b = MenuLayout::homeTileRect(m, kPortraitWidth, kPortraitHeight, c, j);
          EXPECT_FALSE(overlaps(a, b)) << "tiles " << i << " and " << j << " overlap";
        }
      }
    }
  }
}

TEST(HomeTileLayout, GridRowCountAndOriginMatchTheSpec) {
  const ThemeMetrics& m = AlmanacMetrics::values;

  EXPECT_EQ(MenuLayout::homeGridRowCount(composition(true, true)), 3);
  EXPECT_EQ(MenuLayout::homeGridRowCount(composition(true, false)), 2);
  EXPECT_EQ(MenuLayout::homeGridRowCount(composition(false, true)), 3);
  EXPECT_EQ(MenuLayout::homeGridRowCount(composition(false, false)), 2);

  EXPECT_EQ(MenuLayout::homeGridTop(m, kPortraitHeight, composition(true, true)), 228);
  EXPECT_EQ(MenuLayout::homeGridTop(m, kPortraitHeight, composition(true, false)), 298);
  EXPECT_EQ(MenuLayout::homeGridTop(m, kPortraitHeight, composition(false, true)), 176);
  EXPECT_EQ(MenuLayout::homeGridTop(m, kPortraitHeight, composition(false, false)), 246);
}

TEST(HomeTileLayout, FixedTiersNeverMove) {
  const ThemeMetrics& m = AlmanacMetrics::values;

  const Rect crWithOpds = MenuLayout::homeTileRect(m, kPortraitWidth, kPortraitHeight, composition(true, true), 0);
  const Rect crNoOpds = MenuLayout::homeTileRect(m, kPortraitWidth, kPortraitHeight, composition(true, false), 0);
  EXPECT_EQ(crWithOpds.y, 124);
  EXPECT_EQ(crWithOpds.height, MenuLayout::kHomeWideTileHeight);
  EXPECT_EQ(crNoOpds.y, crWithOpds.y);
  EXPECT_EQ(crNoOpds.height, crWithOpds.height);

  for (const bool recent : {false, true}) {
    for (const bool opds : {false, true}) {
      const auto c = composition(recent, opds);
      const Rect s = MenuLayout::homeTileRect(m, kPortraitWidth, kPortraitHeight, c, c.tileCount - 1);
      EXPECT_EQ(s.y, 648) << "recent=" << recent << " opds=" << opds;
      EXPECT_EQ(s.height, MenuLayout::kHomeWideTileHeight);
      EXPECT_EQ(s.x, m.contentSidePadding);
      EXPECT_EQ(s.width, kPortraitWidth - m.contentSidePadding * 2);
    }
  }
}

TEST(HomeTileLayout, LoneTileInFinalRowSpansFullWidth) {
  const ThemeMetrics& m = AlmanacMetrics::values;
  // Recent book + OPDS gives 5 grid tiles, so the third row holds exactly one.
  const auto odd = composition(true, true);
  const Rect r = MenuLayout::homeTileRect(m, kPortraitWidth, kPortraitHeight, odd, odd.tileCount - 2);
  EXPECT_EQ(r.x, m.contentSidePadding);
  EXPECT_EQ(r.width, kPortraitWidth - m.contentSidePadding * 2);

  // 4 grid tiles fill two even rows, so nothing spans.
  const auto even = composition(true, false);
  const Rect r2 = MenuLayout::homeTileRect(m, kPortraitWidth, kPortraitHeight, even, even.tileCount - 2);
  EXPECT_LT(r2.width, kPortraitWidth - m.contentSidePadding * 2);
}

// --- Almanac -----------------------------------------------------------------
//
// These tests exercise AlmanacTheme::drawButtonMenu / getButtonMenuLayout
// directly through MenuLayout's row-fitting arithmetic. HomeActivity no
// longer calls either -- Home's menu draws via MenuLayout::homeTileRect's
// fixed-height tile grid instead (see the HomeTileLayout tests above), so
// these no longer describe what Home puts on screen. They stay because the
// row-fitting mechanism they exercise is still compiled (AlmanacTheme
// overrides both virtuals) and is still worth pinning on its own terms.

// Almanac's hints bar is 8px taller than BaseMetrics', and its selected tile
// draws a stroke kMenuSelectionReserve px OUTSIDE the fill, so the last row
// needs that much clearance beyond itself. AlmanacTheme::getButtonMenuLayout
// reserves it; this mirrors that reservation rather than re-deriving the pitch.
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
  EXPECT_EQ(count, 6);
  // The selection stroke must clear the bar too, not just the tile.
  expectClearOfHints(m, lastRowBottomAlmanac(count) + AlmanacTheme::kMenuSelectionReserve, "Almanac 6 items");
}

// AlmanacMetrics hosts Continue Reading in the menu (homeContinueReadingInMenu
// = true), so a recent book plus an OPDS server pushes the row-menu formula to
// 7 items -- one past what AlmanacFitsWithoutOpds's 6 fit via gap compression
// alone. Row-fitting genuinely cannot seat a 7th row in this budget: this pins
// that impossibility rather than a fit, because it is the reason Home draws
// its menu via MenuLayout::homeTileRect's fixed-height tile grid instead of
// AlmanacTheme::drawButtonMenu -- nothing calls the latter with Home's full
// item count anymore.
TEST(HomeMenuLayout, AlmanacRowFittingCannotSeatSevenItems) {
  const auto& m = AlmanacMetrics::values;
  EXPECT_TRUE(m.homeContinueReadingInMenu);
  const int count = menuItemCount(m, true, true);
  EXPECT_EQ(count, 7);
  const int hintsTop = MenuLayout::buttonHintsTop(m, kPortraitHeight);
  const int lastBottom = lastRowBottomAlmanac(count) + AlmanacTheme::kMenuSelectionReserve;
  EXPECT_GT(lastBottom, hintsTop) << "expected row-fitting to overflow the hints bar at 7 items, not fit";
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
// (400px) from the subtraction, which is what let rows run past the hints bar.
// BaseMetrics rides along: it is still compiled as the base implementation's
// metrics, and a second data point on a pure function costs nothing.
TEST(HomeMenuLayout, AvailableHeightSpansMenuTopToHintsBar) {
  for (const auto& m : {BaseMetrics::values, AlmanacMetrics::values}) {
    EXPECT_EQ(MenuLayout::availableHeight(m, kPortraitHeight),
              MenuLayout::buttonHintsTop(m, kPortraitHeight) - MenuLayout::menuTop(m));
  }
}

// fittedRowStep only compresses gaps -- it floors at rowHeight and never
// shrinks a row. That was sufficient for both metrics tables while neither put
// Continue Reading in the menu, capping both at 6 rows (5 fixed entries plus
// OPDS). AlmanacMetrics has since flipped homeContinueReadingInMenu to true;
// gap compression alone is no longer enough at its resulting 7 rows (see
// AlmanacRowFittingCannotSeatSevenItems above) -- exactly the "row-shrinking
// tier" this test used to warn a flip would require. Home sidesteps that
// entirely by drawing via MenuLayout::homeTileRect instead of row-fitting.
// BaseMetrics still keeps Continue Reading out of the menu, so the original
// invariant still holds for it.
TEST(HomeMenuLayout, BaseMetricsRowMenuCannotExceedSixRows) {
  const auto& m = BaseMetrics::values;
  EXPECT_FALSE(m.homeContinueReadingInMenu);
  EXPECT_EQ(menuItemCount(m, true, true), 6);
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
// what makes taps land on them. No shipped layout paginates today, but the
// paginate path stays part of menuRowLayout's contract, so it stays pinned
// with the geometry of the theme that exercised it (the retired RoundedRaff:
// 49px font-derived rows on a 55px pitch, 7 menu items).

namespace {

constexpr int kPagingTop = 425;
constexpr int kPagingRowHeight = 49;
constexpr int kPagingRowStep = 55;
constexpr int kPagingItemCount = 7;
constexpr int kPagingHeight = 280;  // yields pageItems = 5

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

TEST(MenuRowLayout, TapInsideDrawnRowResolvesToThatRow) {
  constexpr MenuRowLayout layout = MenuLayout::menuRowLayout(kPagingTop, kPagingHeight, kPagingRowHeight,
                                                             kPagingRowStep, kPagingItemCount, 0, /*paginate=*/true);
  // Drawn row 2 spans y 535..583. A hit-test that re-derived a smaller pitch
  // from metrics would resolve y=570 to row 3.
  EXPECT_EQ(rowAt(layout, 535), 2);
  EXPECT_EQ(rowAt(layout, 570), 2);
  EXPECT_EQ(rowAt(layout, 583), 2);
}

TEST(MenuRowLayout, GapBetweenRowsIsNotTappable) {
  constexpr MenuRowLayout layout = MenuLayout::menuRowLayout(kPagingTop, kPagingHeight, kPagingRowHeight,
                                                             kPagingRowStep, kPagingItemCount, 0, /*paginate=*/true);
  EXPECT_EQ(rowAt(layout, kPagingTop + kPagingRowHeight), -1);      // first pixel of the gap
  EXPECT_EQ(rowAt(layout, kPagingTop + kPagingRowHeight + 5), -1);  // last pixel of the gap
  EXPECT_EQ(rowAt(layout, kPagingTop + 55), 1);                     // first pixel of row 1
  EXPECT_EQ(rowAt(layout, kPagingTop - 1), -1);                     // above the menu
}

TEST(MenuRowLayout, SecondPageStartsAtThePageBoundary) {
  constexpr MenuRowLayout layout = MenuLayout::menuRowLayout(kPagingTop, kPagingHeight, kPagingRowHeight,
                                                             kPagingRowStep, kPagingItemCount, 5, /*paginate=*/true);
  EXPECT_EQ(layout.pageItems, 5);
  EXPECT_EQ(layout.firstIndex, 5);
  EXPECT_EQ(layout.visibleCount, 2);  // only items 5 and 6 remain
  // The topmost drawn row on page 2 is item 5, not item 0.
  EXPECT_EQ(rowAt(layout, kPagingTop), 5);
  EXPECT_EQ(rowAt(layout, kPagingTop + 55), 6);
}

TEST(MenuRowLayout, TapBelowAPartialLastPageIsRejected) {
  constexpr MenuRowLayout layout = MenuLayout::menuRowLayout(kPagingTop, kPagingHeight, kPagingRowHeight,
                                                             kPagingRowStep, kPagingItemCount, 5, /*paginate=*/true);
  // Page 2 draws 2 of 5 possible rows; the empty space below them is not a row.
  EXPECT_EQ(rowAt(layout, kPagingTop + 55 * 2), -1);
  EXPECT_EQ(rowAt(layout, kPagingTop + 55 * 4), -1);
}

TEST(MenuRowLayout, NegativeSelectionClampsToTheFirstPage) {
  // HomeActivity passes selectorIndex - recentBooks.size() on themes that keep
  // Continue Reading out of the menu, which is negative while a recent book is
  // selected.
  constexpr MenuRowLayout layout = MenuLayout::menuRowLayout(kPagingTop, kPagingHeight, kPagingRowHeight,
                                                             kPagingRowStep, kPagingItemCount, -1, /*paginate=*/true);
  EXPECT_EQ(layout.firstIndex, 0);
  EXPECT_EQ(layout.visibleCount, 5);
}

TEST(MenuRowLayout, ShortRectStillDrawsOneRow) {
  // pageItems floors at 1 so a rect too short for a single row does not divide
  // by zero or page infinitely.
  constexpr MenuRowLayout layout =
      MenuLayout::menuRowLayout(kPagingTop, 10, kPagingRowHeight, kPagingRowStep, kPagingItemCount, 3,
                                /*paginate=*/true);
  EXPECT_EQ(layout.pageItems, 1);
  EXPECT_EQ(layout.firstIndex, 3);
  EXPECT_EQ(layout.visibleCount, 1);
}

TEST(MenuRowLayout, DegenerateInputsProduceNoTappableRows) {
  constexpr MenuRowLayout empty =
      MenuLayout::menuRowLayout(kPagingTop, kPagingHeight, kPagingRowHeight, kPagingRowStep, 0, 0, /*paginate=*/true);
  EXPECT_EQ(empty.visibleCount, 0);
  EXPECT_EQ(empty.pageItems, 0);
  EXPECT_EQ(rowAt(empty, kPagingTop), -1);

  constexpr MenuRowLayout zeroStep =
      MenuLayout::menuRowLayout(kPagingTop, kPagingHeight, 0, 0, kPagingItemCount, 0, /*paginate=*/true);
  EXPECT_EQ(zeroStep.visibleCount, 0);
  EXPECT_EQ(rowAt(zeroStep, kPagingTop), -1);
}

// The shipped layout must never drop a row: its fit clears the hints bar by
// compressing gaps, so paging would hide entries instead.
TEST(MenuRowLayout, NonPaginatingLayoutDrawsEveryRowAtSix) {
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

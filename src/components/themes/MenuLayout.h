#pragma once

#include <algorithm>

#include "components/themes/BaseTheme.h"

// Vertical layout arithmetic for the home button-menu.
//
// The menu is drawn by each theme's drawButtonMenu() but hit-tested by
// HomeActivity, so both sides have to agree on where every row lands. Keeping
// the arithmetic here — pure, constexpr, flash-resident — is what makes that
// agreement checkable (see test/home_menu_layout/) instead of a coincidence.
namespace MenuLayout {

// Top of the menu: directly below the recent-book cover tile.
constexpr int menuTop(const ThemeMetrics& metrics) {
  return metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
}

// Top edge of the button-hints bar. Almanac parks its hints in the bottom
// `buttonHintsHeight` pixels of the portrait screen as a solid full-width
// fill -- which is why an overlap there eats a row's label text, not just
// its border.
constexpr int buttonHintsTop(const ThemeMetrics& metrics, const int pageHeight) {
  return pageHeight - metrics.buttonHintsHeight;
}

// Vertical space the menu may occupy: from its top edge down to the hints bar.
constexpr int availableHeight(const ThemeMetrics& metrics, const int pageHeight) {
  return std::max(0, buttonHintsTop(metrics, pageHeight) - menuTop(metrics));
}

// Pitch (row height + gap) that keeps `rowCount` rows of `rowHeight` inside
// `availableHeight`. Gaps compress before anything else — rows keep their full
// height so labels and icons stay legible — and the result never exceeds the
// theme's natural step, so a menu that already fits is left pixel-identical.
// The floor of `rowHeight` means rows abut at worst; they never overlap.
constexpr int fittedRowStep(const int availableHeight, const int rowHeight, const int naturalStep, const int rowCount) {
  if (rowCount <= 1 || rowHeight <= 0) {
    return naturalStep;
  }
  // The last row contributes its height; the other (rowCount - 1) rows each
  // contribute a full pitch.
  const int fitted = (availableHeight - rowHeight) / (rowCount - 1);
  return std::min(naturalStep, std::max(rowHeight, fitted));
}

// Bottom edge (exclusive) of the last drawn row.
constexpr int lastRowBottom(const int menuTop, const int rowHeight, const int rowStep, const int rowCount) {
  return rowCount <= 0 ? menuTop : menuTop + (rowCount - 1) * rowStep + rowHeight;
}

// Assembles a MenuRowLayout (declared in BaseTheme.h, next to ThemeMetrics, so
// that header can use it as a return type without including this one) from a
// pitch the caller has already decided on.
//
// rowStep is an input rather than rowHeight + gap because the two families of
// layout derive it differently and both must survive: a non-paginating layout
// passes fittedRowStep() so gaps compress to clear the button-hints bar,
// while a paginating one passes its natural pitch and windows rows to the
// selection's page instead (the retired RoundedRaff theme was that consumer;
// the path stays pinned by test/home_menu_layout).
//
// paginate == false draws every row, which is what Almanac does -- it relies
// on fittedRowStep having already made the rows fit, so dropping rows here
// would silently hide menu entries instead.
constexpr MenuRowLayout menuRowLayout(const int top, const int height, const int rowHeight, const int rowStep,
                                      const int itemCount, const int selectedIndex, const bool paginate) {
  if (rowStep <= 0 || itemCount <= 0) return MenuRowLayout{top, rowHeight, rowStep, 0, 0, 0};

  if (!paginate) return MenuRowLayout{top, rowHeight, rowStep, 0, itemCount, itemCount};

  // selectedIndex arrives negative when a recent-book row is selected on a
  // theme that keeps Continue Reading out of the menu, so clamp before paging.
  const int safeSelectedIndex = selectedIndex > 0 ? selectedIndex : 0;
  const int wholeRows = height / rowStep;
  const int pageItems = wholeRows > 1 ? wholeRows : 1;  // a too-short rect still shows one row
  const int firstIndex = (safeSelectedIndex / pageItems) * pageItems;
  const int remaining = itemCount - firstIndex;
  const int visibleCount = remaining <= 0 ? 0 : (remaining < pageItems ? remaining : pageItems);
  return MenuRowLayout{top, rowHeight, rowStep, firstIndex, visibleCount, pageItems};
}

}  // namespace MenuLayout

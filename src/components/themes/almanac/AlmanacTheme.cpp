#include "components/themes/almanac/AlmanacTheme.h"

#include <algorithm>

int AlmanacTheme::getListRowStep(const bool hasSubtitle) const {
  return hasSubtitle ? AlmanacMetrics::values.listWithSubtitleRowHeight : AlmanacMetrics::values.listRowHeight;
}

int AlmanacTheme::getListPageItems(const int contentHeight, const bool hasSubtitle) const {
  const int rowStep = getListRowStep(hasSubtitle);
  if (rowStep <= 0) return 1;
  return std::max(1, contentHeight / rowStep);
}

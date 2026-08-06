#include "components/themes/almanac/AlmanacTheme.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "components/UITheme.h"
#include "fontIds.h"

// Internal constants
namespace {
// Outer frame around the whole list, drawn flush with the Rect the caller
// hands drawList (GfxRenderer::drawRect's lineWidth overload draws its
// stroke INSIDE the box -- see GfxRenderer.cpp's "Border is inside the
// rectangle" comment -- so this costs no extra space beyond `rect` itself).
// Keeping the frame flush with `rect`, rather than inset by a separate
// margin, means row layout can stay keyed to `rect` exactly like BaseTheme,
// with no second height budget to keep in sync with getListPageItems.
constexpr int kFrameStroke = 2;
// Heavier stroke drawn just outside a selected row's fill -- the two-level
// emphasis that reads as "panel" instead of a plain inverted bar.
constexpr int kSelectionStroke = 3;
constexpr int kMinValueGap = 10;
constexpr int kMenuTileStroke = 2;
}  // namespace

int AlmanacTheme::getListRowStep(const bool hasSubtitle) const {
  return hasSubtitle ? AlmanacMetrics::values.listWithSubtitleRowHeight : AlmanacMetrics::values.listRowHeight;
}

int AlmanacTheme::getListPageItems(const int contentHeight, const bool hasSubtitle) const {
  const int rowStep = getListRowStep(hasSubtitle);
  if (rowStep <= 0) return 1;
  return std::max(1, contentHeight / rowStep);
}

void AlmanacTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  // Solid black bar -- the theme's identity element.
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, true);

  const int sidePadding = AlmanacMetrics::values.contentSidePadding;
  const int titleY = rect.y + (rect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  const int smallTextY = rect.y + (rect.height - renderer.getLineHeight(SMALL_FONT_ID)) / 2;

  // BaseTheme::drawBatteryLeft/Right draw the battery casing via the static,
  // non-virtual drawBatteryOutline() helper, which is hardcoded to draw
  // black lines (see BaseTheme.cpp) -- invisible against this bar's black
  // fill, and shared by every other theme, so recoloring it is out of
  // AlmanacTheme's scope. Show the percentage as white text instead of the
  // pictogram: it preserves the information (still gated by the same
  // hideBatteryPercentage setting Base honors) and, arguably, reads more
  // like an instrument-panel readout than the icon would have.
  std::string batteryText;
  if (SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS) {
    batteryText = std::to_string(powerManager.getBatteryPercentage()) + "%";
  }

  int rightEdge = rect.x + rect.width - sidePadding;
  if (!batteryText.empty()) {
    const int batteryWidth = renderer.getTextWidth(SMALL_FONT_ID, batteryText.c_str());
    renderer.drawText(SMALL_FONT_ID, rightEdge - batteryWidth, smallTextY, batteryText.c_str(), false);
    rightEdge -= batteryWidth + sidePadding;
  }

  // Title (left) and subtitle (right, e.g. Settings' version string or the
  // flight tracker's transient error message) share whatever space remains
  // between the left padding and the battery reservation, split the same
  // way LyraTheme::drawHeader splits title vs subtitle: give each its
  // natural width unless they overflow the available space, in which case
  // shrink the wider one first (or both, if both exceed half).
  int maxTitleWidth = title != nullptr ? renderer.getTextWidth(UI_12_FONT_ID, title, EpdFontFamily::BOLD) : 0;
  int maxSubtitleWidth = subtitle != nullptr ? renderer.getTextWidth(SMALL_FONT_ID, subtitle) : 0;
  const int leftEdge = rect.x + sidePadding;
  const int availableSpace = std::max(0, rightEdge - leftEdge - (subtitle != nullptr ? sidePadding : 0));

  if (maxTitleWidth + maxSubtitleWidth > availableSpace) {
    if (maxTitleWidth > availableSpace / 2 && maxSubtitleWidth > availableSpace / 2) {
      maxTitleWidth = availableSpace / 2;
      maxSubtitleWidth = availableSpace / 2;
    } else if (maxTitleWidth > maxSubtitleWidth) {
      maxTitleWidth = availableSpace - maxSubtitleWidth;
    } else {
      maxSubtitleWidth = availableSpace - maxTitleWidth;
    }
  }

  if (subtitle != nullptr) {
    auto truncatedSubtitle = renderer.truncatedText(SMALL_FONT_ID, subtitle, maxSubtitleWidth);
    const int subtitleWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedSubtitle.c_str());
    renderer.drawText(SMALL_FONT_ID, rightEdge - subtitleWidth, smallTextY, truncatedSubtitle.c_str(), false);
  }

  if (title != nullptr) {
    auto truncatedTitle = renderer.truncatedText(UI_12_FONT_ID, title, maxTitleWidth, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, leftEdge, titleY, truncatedTitle.c_str(), false, EpdFontFamily::BOLD);
  }
}

void AlmanacTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                   const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }

  // Button hints are always drawn against the physical bottom edge in
  // forced Portrait orientation, matching BaseTheme::drawButtonHints /
  // LyraTheme::drawButtonHints.
  const GfxRenderer::Orientation origOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int barHeight = AlmanacMetrics::values.buttonHintsHeight;
  const int barY = pageHeight - barHeight;

  // Solid black bar spanning the full width -- bookends the header bar.
  renderer.fillRect(0, barY, pageWidth, barHeight, true);

  const char* labels[] = {btn1, btn2, btn3, btn4};
  constexpr int kSlots = 4;
  const int slotWidth = pageWidth / kSlots;
  const int textY = barY + (barHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;

  // Evenly distribute the four slots; a single continuous black bar (no
  // per-slot box or divider) means an empty label just leaves that slot's
  // portion of the bar blank -- no stray border or gap artifact to avoid.
  for (int i = 0; i < kSlots; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') {
      continue;
    }
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, labels[i]);
    const int slotX = i * slotWidth;
    const int textX = slotX + (slotWidth - textWidth) / 2;
    renderer.drawText(UI_10_FONT_ID, textX, textY, labels[i], false);
  }

  renderer.setOrientation(origOrientation);
}

void AlmanacTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                            const std::function<std::string(int index)>& rowTitle,
                            const std::function<std::string(int index)>& rowSubtitle,
                            const std::function<UIIcon(int index)>& rowIcon,
                            const std::function<std::string(int index)>& rowValue, bool highlightValue,
                            const std::function<bool(int index)>& rowDimmed) const {
  // GfxRenderer::drawIcon has no invert/state parameter (see GfxRenderer.cpp
  // -- it always plots black ink pixels), so a row icon would vanish on the
  // selected row's solid black fill. BaseTheme::drawList never reads
  // rowIcon either (ignoring it there is faithful to Base, not a drop), and
  // AlmanacTheme inherits showsFileIcons() == false from BaseTheme, so the
  // file browser already renders "[folder]" bracket notation instead of
  // relying on an icon -- self-consistent with not drawing one here.
  (void)rowIcon;
  // BaseTheme::drawList never reads highlightValue either -- its only use
  // (LyraTheme) draws an extra inverted box behind the value text so it
  // stands out against LyraTheme's own selection fill. Almanac's selected
  // row is already fully inverted (filled black, white text), so a nested
  // highlight box would be redundant emphasis inside emphasis.
  (void)highlightValue;

  const bool hasSubtitle = rowSubtitle != nullptr;
  // Use this theme's own overrides rather than re-deriving row height/page
  // size inline (as BaseTheme/LyraTheme do): every other caller that needs
  // the same numbers -- MappedInputManager's page-navigation, several
  // Activities' own pageItems math -- goes through these same two virtuals,
  // so routing drawList's internal windowing through them too is what keeps
  // "how many rows are on screen" and "where does paging jump to" in sync.
  const int rowHeight = getListRowStep(hasSubtitle);
  const int pageItems = getListPageItems(rect.height, hasSubtitle);
  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  const auto pageStartIndex = selectedIndex / pageItems * pageItems;
  const int visibleCount = std::max(0, std::min(itemCount, pageStartIndex + pageItems) - pageStartIndex);

  // 1) One frame around the whole list. The stroke draws inward (see
  // GfxRenderer::drawRect's lineWidth overload), so it costs no extra space
  // beyond `rect` -- rows stay keyed to `rect.y`/`rect.height` exactly like
  // BaseTheme, with nothing extra to keep in sync with getListPageItems.
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, kFrameStroke, true);

  // Reserve room for a scrollbar (mirroring LyraTheme/RoundedRaffTheme) when
  // there's more than one page -- BaseTheme::drawList's up/down corner
  // arrows are this theme's closest carried-over behaviour, reimagined as a
  // gauge-like strip rather than arrows, but the point (a paging indicator
  // exists at all) is preserved.
  const bool showScrollBar = totalPages > 1 && itemCount > 0;
  int contentRight = rect.x + rect.width - kFrameStroke;
  if (showScrollBar) {
    contentRight -= AlmanacMetrics::values.scrollBarWidth + AlmanacMetrics::values.scrollBarRightOffset;
  }
  const int contentLeft = rect.x + kFrameStroke;
  const int contentWidth = std::max(0, contentRight - contentLeft);

  // 2) Hairline separators between visible rows (not before the first or
  // after the last -- those edges are the frame itself).
  for (int j = 1; j < visibleCount; j++) {
    const int y = rect.y + j * rowHeight;
    renderer.drawLine(contentLeft, y, contentRight - 1, y, 1, true);
  }

  // 3) Selected row: fill black, then a heavier stroke just outside the
  // fill. Clamped to the frame's interior rather than inset from it, so a
  // selection at the first/last visible row degenerates into (reinforces)
  // the frame line there instead of escaping past it -- the frame's own
  // bounds are the clip, not a separately-budgeted margin.
  if (selectedIndex >= 0) {
    const int selRowIdx = selectedIndex - pageStartIndex;
    const int selY = rect.y + selRowIdx * rowHeight;
    renderer.fillRect(rect.x, selY, rect.width, rowHeight, true);

    const int strokeTop = std::max(rect.y, selY - kSelectionStroke);
    const int strokeBottom = std::min(rect.y + rect.height, selY + rowHeight + kSelectionStroke);
    renderer.drawRect(rect.x, strokeTop, rect.width, strokeBottom - strokeTop, kSelectionStroke, true);
  }

  // 4) Scrollbar thumb, drawn after the selection fill/stroke so it isn't
  // painted over by either.
  if (showScrollBar) {
    const int barX = rect.x + rect.width - kFrameStroke - AlmanacMetrics::values.scrollBarWidth -
                     AlmanacMetrics::values.scrollBarRightOffset;
    const int trackHeight = rect.height - kFrameStroke * 2;
    const int thumbHeight = std::max(10, (trackHeight * pageItems) / itemCount);
    const int maxStart = std::max(1, itemCount - pageItems);
    const int maxTravel = std::max(1, trackHeight - thumbHeight);
    const int clampedStart = std::clamp(pageStartIndex, 0, maxStart);
    const int thumbY = rect.y + kFrameStroke + (clampedStart * maxTravel) / maxStart;
    renderer.fillRect(barX, thumbY, AlmanacMetrics::values.scrollBarWidth, thumbHeight, true);
  }

  // 5) Row content: title/subtitle/value, inverted (white) on the selected
  // row, honouring rowDimmed exactly as BaseTheme does (checkerboard-dither
  // the title on non-selected dimmed rows).
  //
  // With a subtitle, the title is top-anchored at itemY and the subtitle
  // sits kSubtitleOffsetY below it -- BaseTheme::drawList's exact numbers,
  // reused deliberately rather than recomputed from font metrics. A first
  // attempt here stacked the two lines using getLineHeight() (each font's
  // full newline pitch: 24px for UI_10, 23px for SMALL) plus padding, which
  // measured out to ~59px of content in Almanac's 52px subtitle row --
  // overflowing into the next row. BaseTheme's fixed 22px offset already
  // ships this same two-line layout inside a *tighter* 50px row, so reusing
  // it here (with Almanac's row 2px taller still) is safe by construction.
  const int titleLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  constexpr int kSubtitleOffsetY = 22;
  constexpr int kSubtitleValueOffsetY = 10;
  for (int i = pageStartIndex; i < pageStartIndex + visibleCount; i++) {
    const bool isSelected = i == selectedIndex;
    const int itemY = rect.y + (i - pageStartIndex) * rowHeight;
    // Without a subtitle, center the single line in the row; with one,
    // top-anchor it (matching BaseTheme) so there's room below for the
    // subtitle line without recentering the whole two-line block.
    const int textY = hasSubtitle ? itemY : itemY + (rowHeight - titleLineHeight) / 2;

    int rowTextWidth = contentWidth - AlmanacMetrics::values.contentSidePadding * 2;
    std::string valueText;
    if (rowValue != nullptr) {
      valueText = rowValue(i);
      if (!valueText.empty()) {
        const int maxValW = std::max(0, rowTextWidth - 40 - kMinValueGap);
        valueText = renderer.truncatedText(UI_10_FONT_ID, valueText.c_str(), maxValW);
        const int valueWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str()) + kMinValueGap;
        rowTextWidth -= valueWidth;
      }
    }

    auto itemName = rowTitle(i);
    auto item = renderer.truncatedText(UI_10_FONT_ID, itemName.c_str(), rowTextWidth);
    const int textX = contentLeft + AlmanacMetrics::values.contentSidePadding;
    renderer.drawText(UI_10_FONT_ID, textX, textY, item.c_str(), !isSelected);

    // Checkerboard-dither the title to a gray effect for dimmed, non-selected rows.
    if (rowDimmed && rowDimmed(i) && !isSelected) {
      const int titleWidth = renderer.getTextWidth(UI_10_FONT_ID, item.c_str());
      for (int py = textY; py < textY + titleLineHeight; py++) {
        for (int px = textX; px < textX + titleWidth; px++) {
          if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
        }
      }
    }

    if (rowSubtitle != nullptr) {
      std::string subtitleText = rowSubtitle(i);
      if (!subtitleText.empty()) {
        auto subtitleStr = renderer.truncatedText(SMALL_FONT_ID, subtitleText.c_str(), rowTextWidth);
        renderer.drawText(SMALL_FONT_ID, textX, textY + kSubtitleOffsetY, subtitleStr.c_str(), !isSelected);
      }
    }

    if (!valueText.empty()) {
      const int valueTextWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str());
      const int valueX = contentRight - AlmanacMetrics::values.contentSidePadding - valueTextWidth;
      const int valueY = hasSubtitle ? textY + kSubtitleValueOffsetY : textY;
      renderer.drawText(UI_10_FONT_ID, valueX, valueY, valueText.c_str(), !isSelected);
    }
  }
}

void AlmanacTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                  const std::function<std::string(int index)>& buttonLabel,
                                  const std::function<UIIcon(int index)>& rowIcon) const {
  // Same constraint as drawList: GfxRenderer::drawIcon always plots black
  // ink, so it would vanish against the selected tile's black fill. Matches
  // BaseTheme and RoundedRaffTheme, both of which also ignore this callback.
  (void)rowIcon;

  for (int i = 0; i < buttonCount; ++i) {
    const int tileY = AlmanacMetrics::values.verticalSpacing + rect.y +
                      i * (AlmanacMetrics::values.menuRowHeight + AlmanacMetrics::values.menuSpacing);
    const int tileX = rect.x + AlmanacMetrics::values.contentSidePadding;
    const int tileWidth = rect.width - AlmanacMetrics::values.contentSidePadding * 2;
    const int tileHeight = AlmanacMetrics::values.menuRowHeight;

    const bool selected = selectedIndex == i;
    if (selected) {
      // Same two-level emphasis as the list's selection: filled black, plus
      // a heavier stroke drawn just outside the fill. Unlike the list, menu
      // tiles aren't packed edge-to-edge inside a shared frame -- each tile
      // has menuSpacing (8px) between it and its neighbours, comfortably
      // more than the stroke's reach (3px), so no clamping against adjacent
      // tiles is needed here. (This does NOT cover the *last* tile against
      // the button-hints bar below the menu -- see the report: with 6 menu
      // items (OPDS enabled) HomeActivity's own tile-position formula
      // already lands the last tile's unstroked bottom past the
      // button-hints bar in every theme, pre-existing and out of this
      // file's scope; the stroke here adds 3px on top of that.)
      renderer.fillRect(tileX, tileY, tileWidth, tileHeight, true);
      renderer.drawRect(tileX - kSelectionStroke, tileY - kSelectionStroke, tileWidth + kSelectionStroke * 2,
                        tileHeight + kSelectionStroke * 2, kSelectionStroke, true);
    } else {
      renderer.drawRect(tileX, tileY, tileWidth, tileHeight, kMenuTileStroke, true);
    }

    std::string labelStr = buttonLabel(i);
    const char* label = labelStr.c_str();
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label);
    const int textX = rect.x + (rect.width - textWidth) / 2;
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int textY = tileY + (tileHeight - lineHeight) / 2;
    renderer.drawText(UI_10_FONT_ID, textX, textY, label, !selected);
  }
}

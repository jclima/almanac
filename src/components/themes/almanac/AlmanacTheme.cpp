#include "components/themes/almanac/AlmanacTheme.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "components/UITheme.h"
#include "components/themes/MenuLayout.h"
#include "fontIds.h"

// Internal constants
namespace {
// Outer frame around the whole list, drawn flush with the Rect the caller
// hands drawList. GfxRenderer::drawRect's lineWidth overload does NOT keep
// its stroke inside the box despite its own comment's claim: at i=0 it draws
// the right edge at column x+width and the bottom edge at row y+height, one
// pixel past the box's actual last column/row (x+width-1 / y+height-1). Every
// call site below compensates by passing `width - 1` / `height - 1` so the
// stroke lands on the box's own last column/row instead of one past it --
// see each call site for why this matters there.
// Keeping the frame flush with `rect`, rather than inset by a separate
// margin, means row layout can stay keyed to `rect` exactly like BaseTheme,
// with no second height budget to keep in sync with getListPageItems.
constexpr int kFrameStroke = 2;
// Heavier stroke drawn just outside a selected row's fill -- the two-level
// emphasis that reads as "panel" instead of a plain inverted bar. This is the
// full reach of that emphasis beyond the fill (used to offset/inflate the
// stroke's rect); the ink itself is only kSelectionStrokeWidth thick (see
// below), so the reach's innermost pixels -- the ones touching the fill --
// are left unpainted.
constexpr int kSelectionStroke = AlmanacTheme::kMenuSelectionReserve;
// Ink thickness of the selection stroke. Kept smaller than kSelectionStroke
// so the (kSelectionStroke - kSelectionStrokeWidth) px nearest the fill stay
// white: without that gap the fill and stroke are both solid black with
// nothing between them, so on a 1-bit panel they merge into one bar and the
// "two-level" emphasis reads as no different from a plain inverted bar (see
// each call site for the visual and neighbouring-row consequences).
constexpr int kSelectionStrokeWidth = 2;
constexpr int kMinValueGap = 10;
constexpr int kMenuTileStroke = 2;

// Minimal white-ink battery pictogram for this theme's black header bar,
// used only when hideBatteryPercentage == HIDE_ALWAYS (see drawHeader) and
// there is no percentage text to fall back on. BaseTheme::drawBatteryOutline
// / fillBatteryIcon can't be reused for this: both default their draw calls'
// `state` parameter to true (black ink), which is invisible against this
// bar's black fill -- the same reason AlmanacTheme::drawHeader doesn't call
// them at all. This mirrors their casing-plus-proportional-fill shape with
// every call's state pinned to false instead, the same "white/inverted on
// black fill for visibility" convention BaseTheme::drawBatteryLightningBolt
// (reused as-is elsewhere in this file) already follows. Sized from
// AlmanacMetrics::values.batteryWidth/batteryHeight -- the same icon
// footprint BaseTheme's pictogram uses -- so it reads as the same battery
// icon, just recoloured for this bar.
void drawBatteryPictogramWhite(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                               const int percentage) {
  // Casing: top/bottom walls, left wall, right end-cap plus a small terminal
  // nub -- same geometry as BaseTheme::drawBatteryOutline.
  renderer.drawLine(x + 1, y, x + width - 3, y, false);
  renderer.drawLine(x + 1, y + height - 1, x + width - 3, y + height - 1, false);
  renderer.drawLine(x, y + 1, x, y + height - 2, false);
  renderer.drawLine(x + width - 2, y + 1, x + width - 2, y + height - 2, false);
  renderer.drawPixel(x + width - 1, y + 3, false);
  renderer.drawPixel(x + width - 1, y + height - 4, false);
  renderer.drawLine(x + width - 0, y + 4, x + width - 0, y + height - 5, false);

  // Charge-level fill, mirroring BaseTheme::fillBatteryIcon: a proportional
  // white bar inset from the casing. The uncharged remainder is left as the
  // bar's own black fill -- the same visual language as the black-on-white
  // pictogram, with the two colours swapped.
  const int maxFillWidth = width - 5;
  const int fillHeight = height - 4;
  if (maxFillWidth > 0 && fillHeight > 0) {
    int filledWidth = percentage * maxFillWidth / 100 + 1;  // +1 rounds up so at least 1px always fills
    filledWidth = std::min(filledWidth, maxFillWidth);
    renderer.fillRect(x + 2, y + 2, filledWidth, fillHeight, false);
  }
}
}  // namespace

int AlmanacTheme::getListRowStep(const bool hasSubtitle) const {
  return hasSubtitle ? AlmanacMetrics::values.listWithSubtitleRowHeight : AlmanacMetrics::values.listRowHeight;
}

int AlmanacTheme::getListPageItems(const int contentHeight, const bool hasSubtitle) const {
  const int rowStep = getListRowStep(hasSubtitle);
  if (rowStep <= 0) return 1;
  return std::max(1, contentHeight / rowStep);
}

int AlmanacTheme::getMenuRowStep(const int availableHeight, const int rowCount) const {
  // Reserve the selection stroke: it is drawn kSelectionStroke px outside the
  // selected tile's fill, so budgeting only the tiles leaves the bottom tile
  // fitting while its stroke still crosses into the button-hints bar. Without
  // this the 6-tile menu (OPDS configured) overshoots by exactly 1px.
  return MenuLayout::fittedRowStep(std::max(0, availableHeight - kSelectionStroke),
                                   AlmanacMetrics::values.menuRowHeight,
                                   AlmanacMetrics::values.menuRowHeight + AlmanacMetrics::values.menuSpacing, rowCount);
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
  // pictogram when hideBatteryPercentage allows it: reads more like an
  // instrument-panel readout than the icon would have. Under HIDE_ALWAYS
  // there is no percentage to fall back on, so this draws
  // drawBatteryPictogramWhite (a separate minimal white-ink pictogram,
  // defined above) instead -- omitting it would make HIDE_ALWAYS mean "no
  // battery indicator at all" in this header, unlike BaseTheme::drawHeader,
  // which always draws its pictogram via drawBatteryRight and only gates the
  // percentage *text* (see BaseTheme.cpp's drawBatteryRight -> fillBatteryIcon
  // call, which runs unconditionally).
  const bool showPercentage = SETTINGS.hideBatteryPercentage != AlmanacSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  std::string batteryText;
  if (showPercentage) {
    batteryText = std::to_string(powerManager.getBatteryPercentage()) + "%";
  }

  // Charging cue: every other theme draws BaseTheme::fillBatteryIcon's
  // lightning bolt whenever gpio.isUsbConnected(), and does so
  // unconditionally on hideBatteryPercentage (see BaseTheme.cpp's
  // drawBatteryRight -> fillBatteryIcon call, which always runs; only the
  // percentage *text* is gated there). The pictogram itself is gone for the
  // same drawBatteryOutline reason as above, but drawBatteryLightningBolt is
  // a separate, static, colour-parameterless-but-already-white helper
  // (it plots with state=false -- "white/inverted on black fill for
  // visibility", per its own comment -- exactly this bar's convention) that
  // AlmanacTheme inherits, so it's reused directly rather than reinvented.
  // It must render even when showPercentage is false, so the whole block
  // below is unconditional rather than gated on showPercentage.
  const bool charging = gpio.isUsbConnected();
  int rightEdge = rect.x + rect.width - sidePadding;
  {
    constexpr int kBoltWidth = 6;
    constexpr int kBoltHeight = 8;
    constexpr int kBoltTextGap = 4;

    // batteryText is populated iff showPercentage, so textWidth > 0 iff
    // showPercentage; iconWidth (the HIDE_ALWAYS pictogram fallback) is
    // therefore the exact complement and the two are never both nonzero.
    const int textWidth = batteryText.empty() ? 0 : renderer.getTextWidth(SMALL_FONT_ID, batteryText.c_str());
    const int iconWidth = showPercentage ? 0 : AlmanacMetrics::values.batteryWidth;
    const int rightElementWidth = textWidth + iconWidth;
    const int innerGap = (charging && rightElementWidth > 0) ? kBoltTextGap : 0;

    if (textWidth > 0) {
      renderer.drawText(SMALL_FONT_ID, rightEdge - textWidth, smallTextY, batteryText.c_str(), false);
    } else if (iconWidth > 0) {
      const int iconHeight = AlmanacMetrics::values.batteryHeight;
      const int iconX = rightEdge - iconWidth;
      const int iconY = smallTextY + (renderer.getLineHeight(SMALL_FONT_ID) - iconHeight) / 2;
      drawBatteryPictogramWhite(renderer, iconX, iconY, iconWidth, iconHeight, powerManager.getBatteryPercentage());
    }
    if (charging) {
      const int boltX = rightEdge - rightElementWidth - innerGap - kBoltWidth;
      const int boltY = smallTextY + (renderer.getLineHeight(SMALL_FONT_ID) - kBoltHeight) / 2;
      drawBatteryLightningBolt(renderer, boltX, boltY);
    }

    const int boltWidth = charging ? kBoltWidth : 0;
    rightEdge -= rightElementWidth + innerGap + boltWidth + sidePadding;
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
  //
  // Labels are truncated to their slot. Without it a long translation runs
  // into its neighbour: the slot is only pageWidth/4 (120px on X4 portrait)
  // and the longest hint across the 31 shipped languages is Brazilian
  // Portuguese's "Tentar novamente" (STR_RETRY, 16 characters). truncatedText
  // returns the string unchanged when it already fits, so this costs nothing
  // in the common case and cannot make a fitting label worse.
  constexpr int kSlotTextPadding = 4;
  const int maxLabelWidth = std::max(0, slotWidth - kSlotTextPadding * 2);
  for (int i = 0; i < kSlots; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') {
      continue;
    }
    const std::string label = renderer.truncatedText(UI_10_FONT_ID, labels[i], maxLabelWidth);
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label.c_str());
    const int slotX = i * slotWidth;
    const int textX = slotX + (slotWidth - textWidth) / 2;
    renderer.drawText(UI_10_FONT_ID, textX, textY, label.c_str(), false);
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

  // 1) One frame around the whole list. Every drawList caller passes a
  // full-width rect (`Rect{0, contentTop, pageWidth, contentHeight}`), so
  // width-1/height-1 (see the kFrameStroke comment above for why) is what
  // keeps the frame's right/bottom edges on rect's own last column/row --
  // without it, the right edge lands at logical x == getScreenWidth(), out
  // of panel range in every orientation, and GfxRenderer::drawPixel logs an
  // error for every pixel of that edge (once per row, per stroke layer).
  // Rows still stay keyed to `rect.y`/`rect.height` exactly like BaseTheme,
  // with nothing extra to keep in sync with getListPageItems.
  renderer.drawRect(rect.x, rect.y, rect.width - 1, rect.height - 1, kFrameStroke, true);

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

  // Row index of the selection within this page, used by both step 2 (to
  // skip a separator) and step 3 below; -1 when nothing is selected, which
  // both uses treat as "never matches".
  const int selRowIdx = selectedIndex >= 0 ? selectedIndex - pageStartIndex : -1;

  // 2) Hairline separators between visible rows (not before the first or
  // after the last -- those edges are the frame itself). The separator at
  // j == selRowIdx + 1 is skipped: it sits at row selY+rowHeight, exactly
  // the pixel step 3's white gap leaves unpainted below the selection (see
  // its comment). Left in, this line would repaint that row black, right
  // where step 3 needs it white, silently erasing the gap and putting the
  // stroke back in direct contact with the fill. (j == selRowIdx is safe to
  // draw or skip either way -- it lands on the fill's own top row, which
  // step 3 paints over regardless -- but it's skipped too since drawing it
  // is a wasted call once the fill is known to cover it.)
  for (int j = 1; j < visibleCount; j++) {
    if (j == selRowIdx || j == selRowIdx + 1) continue;
    const int y = rect.y + j * rowHeight;
    renderer.drawLine(contentLeft, y, contentRight - 1, y, 1, true);
  }

  // 3) Selected row: fill black, then a heavier stroke just outside the
  // fill, with a 1px white gap between them (kSelectionStroke -
  // kSelectionStrokeWidth) so the two levels read as distinct rather than
  // merging into one solid bar -- see the kSelectionStrokeWidth comment
  // above. That gap also keeps the stroke's ink off the row immediately
  // below/above the selection: with subtitle rows, that neighbour's title
  // is top-anchored right at the row boundary (itemY, below), so the
  // now-white pixel closest to the fill is what used to land directly on
  // its ascenders -- this only holds because step 2 above also stopped
  // repainting that same pixel black via the hairline separator. Clamped to
  // the frame's interior rather than inset from it, so a selection at the
  // first/last visible row degenerates into (reinforces) the frame line
  // there instead of escaping past it -- the frame's own bounds are the
  // clip, not a separately-budgeted margin. width-1/height-1 below for the
  // same reason as the frame draw above.
  if (selectedIndex >= 0) {
    const int selY = rect.y + selRowIdx * rowHeight;
    renderer.fillRect(rect.x, selY, rect.width, rowHeight, true);

    const int strokeTop = std::max(rect.y, selY - kSelectionStroke);
    const int strokeBottom = std::min(rect.y + rect.height, selY + rowHeight + kSelectionStroke);
    renderer.drawRect(rect.x, strokeTop, rect.width - 1, strokeBottom - strokeTop - 1, kSelectionStrokeWidth, true);
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

  // Fit the tiles to rect.height instead of laying them out at a fixed pitch
  // that ignores it.
  //
  // HomeActivity shows a 6th menu tile once an OPDS server is configured. At
  // the nominal pitch that tile's bottom lands 18px past the button-hints bar
  // under this theme (10px under Base -- same formula, shorter bar), and
  // because this theme's bar is a solid full-width black fill, the overlap
  // eats the tile's LABEL, not just its border. rect.height is authoritative:
  // HomeActivity derives it as `pageHeight - buttonHintsHeight - menuTop`, so
  // staying inside it is exactly "don't collide with the bar".
  //
  // getMenuRowStep (overridden below to reserve kSelectionStroke) compresses
  // the gaps; rows keep their full height so labels stay legible. Rows start
  // at rect.y with no leading verticalSpacing -- homeMenuTopOffset already
  // separates them from the cover tile, and the offset previously added here
  // put the drawn rows 10px below the rows HomeActivity hit-tests.
  const int rowStep = getMenuRowStep(rect.height, buttonCount);
  const int rowHeight = AlmanacMetrics::values.menuRowHeight;

  for (int i = 0; i < buttonCount; ++i) {
    const int tileY = rect.y + i * rowStep;
    const int tileX = rect.x + AlmanacMetrics::values.contentSidePadding;
    const int tileWidth = rect.width - AlmanacMetrics::values.contentSidePadding * 2;
    const int tileHeight = rowHeight;

    const bool selected = selectedIndex == i;
    if (selected) {
      // Same two-level emphasis as the list's selection: filled black, plus
      // a heavier stroke drawn just outside the fill, with the same 1px
      // white gap between them (see the kSelectionStrokeWidth comment near
      // the top of this file) so the two levels read as distinct on all
      // four sides of the tile, not just top/bottom. Unlike the list, menu
      // tiles aren't packed edge-to-edge inside a shared frame -- there is a
      // gap of (rowStep - rowHeight) between neighbours, which the fit above
      // compresses from menuSpacing (8px) down to 5px at the largest menu
      // this screen can produce (6 tiles, OPDS enabled). Still wider than the
      // stroke's 3px reach, so no clamping against adjacent tiles is needed.
      // The *last* tile against the button-hints bar used to be the exception
      // -- the old fixed pitch pushed its bottom past the bar and this stroke
      // added 3px on top of that -- but getMenuRowStep reserves that reach,
      // so every tile, stroke included, now stays inside the menu's rect.
      // width-1/height-1 below for the same reason as drawList's frame draw.
      renderer.fillRect(tileX, tileY, tileWidth, tileHeight, true);
      renderer.drawRect(tileX - kSelectionStroke, tileY - kSelectionStroke, tileWidth + kSelectionStroke * 2 - 1,
                        tileHeight + kSelectionStroke * 2 - 1, kSelectionStrokeWidth, true);
    } else {
      renderer.drawRect(tileX, tileY, tileWidth - 1, tileHeight - 1, kMenuTileStroke, true);
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

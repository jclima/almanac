#pragma once

#include "components/themes/BaseTheme.h"

class GfxRenderer;

// Almanac theme metrics (zero runtime cost -- constexpr, lives in flash).
// Copied verbatim from BaseMetrics::values with exactly five fields changed:
// headerHeight, buttonHintsHeight, listRowHeight, listWithSubtitleRowHeight,
// contentSidePadding.
namespace AlmanacMetrics {
constexpr ThemeMetrics values = {.batteryWidth = 15,
                                 .batteryHeight = 12,
                                 .topPadding = 5,
                                 .batteryBarHeight = 20,
                                 .headerHeight = 56,
                                 .verticalSpacing = 10,
                                 .previewPadding = 12,
                                 .previewHeightPercent = 30,
                                 .contentSidePadding = 16,
                                 .listRowHeight = 34,
                                 .listWithSubtitleRowHeight = 52,
                                 .menuRowHeight = 45,
                                 .menuSpacing = 8,
                                 .tabSpacing = 10,
                                 .tabBarHeight = 50,
                                 .scrollBarWidth = 4,
                                 .scrollBarRightOffset = 5,
                                 .homeTopPadding = 40,
                                 .homeCoverHeight = 400,
                                 .homeCoverTileHeight = 400,
                                 .homeRecentBooksCount = 1,
                                 .homeContinueReadingInMenu = false,
                                 .homeMenuTopOffset = 10,
                                 .buttonHintsHeight = 48,
                                 .sideButtonHintsWidth = 30,
                                 .progressBarHeight = 16,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 19,
                                 .keyboardKeyHeight = 48,
                                 .keyboardKeySpacing = 0,
                                 .keyboardCenteredText = false,
                                 .keyboardVerticalOffset = -13,
                                 .keyboardTextFieldWidthPercent = 85,
                                 .keyboardWidthPercent = 94,
                                 .popupTopOffsetRatio = 0.075f,
                                 .popupMarginX = 15,
                                 .popupMarginY = 15,
                                 .popupFrameThickness = 2,
                                 .popupCornerRadius = 0,
                                 .popupTextBold = true,
                                 .popupTextInverted = true,
                                 .popupTextBaselineOffsetY = -2,
                                 .popupProgressBarHeight = 4,
                                 .popupProgressDrawOutline = false,
                                 .popupProgressClampPercent = false,
                                 .popupProgressFillInverted = true,
                                 .popupProgressOutlineInverted = true,
                                 .optionPopupItemSpacing = 6,
                                 .optionPopupInnerPadding = 16,
                                 .optionPopupSelectionHPadding = 8,
                                 .optionPopupSelectionVPadding = 4,
                                 .optionPopupTitleGap = 10,
                                 .optionPopupUseSmallFont = true,
                                 .optionPopupOptionFontBold = true,
                                 .optionPopupSelectionRadius = 0,
                                 .optionPopupSelectionLight = false,
                                 .optionPopupDrawAllRows = false,
                                 .optionPopupDialogSideMargin = 20,
                                 .optionPopupTitleSeparator = true,
                                 .textFieldHorizontalPadding = 6,
                                 .textFieldNormalThickness = 1,
                                 .textFieldCursorThickness = 3,
                                 .textFieldLineEndOffset = 0};
}  // namespace AlmanacMetrics

// "Instrument panel" theme: solid header/footer bars, a single frame around
// the list with hairline row separators, and a two-level selection (filled
// row plus a heavier stroke outside it).
class AlmanacTheme final : public BaseTheme {
 public:
  // Reach of a selected tile's stroke beyond its fill, which getButtonMenuLayout
  // reserves below the last home-menu row. Declared here rather than only in
  // the .cpp's anonymous namespace so test/home_menu_layout/ pins the same
  // value the layout actually applies instead of a copy of it.
  static constexpr int kMenuSelectionReserve = 3;

  // BaseTheme::getListRowStep reads BaseMetrics directly rather than the
  // active theme's metrics, so a theme with different row heights MUST
  // override both of these or lists draw and step at different pitches.
  int getListRowStep(bool hasSubtitle) const override;
  int getListPageItems(int contentHeight, bool hasSubtitle) const override;

  // This theme's selected tile draws a stroke OUTSIDE its fill, so the last
  // row needs that much clearance beyond itself. Reserved here rather than in
  // drawButtonMenu alone so HomeActivity's hit-test, which calls this same
  // virtual, derives the identical geometry.
  MenuRowLayout getButtonMenuLayout(const GfxRenderer& renderer, Rect rect, int buttonCount,
                                    int selectedIndex) const override;

  // Signatures copied verbatim from BaseTheme.h (see that file for the
  // authoritative declarations) so a drift is a compiler error, not a
  // silently-created new overload.
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                const std::function<std::string(int index)>& rowTitle,
                const std::function<std::string(int index)>& rowSubtitle = nullptr,
                const std::function<UIIcon(int index)>& rowIcon = nullptr,
                const std::function<std::string(int index)>& rowValue = nullptr, bool highlightValue = false,
                const std::function<bool(int index)>& rowDimmed = nullptr) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
};

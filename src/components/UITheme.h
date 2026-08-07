#pragma once

#include <EpdFontFamily.h>

#include "components/themes/almanac/AlmanacTheme.h"

class UITheme {
  // Static instance
  static UITheme instance;

 public:
  enum class TextVerticalAlignment { TOP, CENTER, BOTTOM };

  static UITheme& getInstance() { return instance; }

  const ThemeMetrics& getMetrics() const;
  // Returns AlmanacTheme, not BaseTheme: currentTheme is concretely
  // AlmanacTheme (see below), and Almanac is a hard fork with exactly one
  // theme -- no other theme will ever implement drawHomeMasthead/
  // drawHomeMenu, so those stay AlmanacTheme-only methods rather than
  // BaseTheme virtuals. Every existing GUI.foo()/getTheme().foo() call site
  // calls a BaseTheme method, which AlmanacTheme still has (inherited or
  // overridden), so this is source-compatible with all of them.
  const AlmanacTheme& getTheme() const { return currentTheme; }
  Rect getScreenSafeArea(const GfxRenderer& renderer, bool hasFrontButtonHints = false,
                         bool hasSideButtonHints = false);
  static void drawCenteredText(const GfxRenderer& renderer, Rect screen, int fontId, int y, const char* text,
                               bool black = true, EpdFontFamily::Style style = EpdFontFamily::REGULAR);
  // Wraps only overflowing text, then aligns the complete line block within bounds.
  static void drawCenteredWrappedText(const GfxRenderer& renderer, Rect bounds, int fontId, const char* text,
                                      int maxLines, bool black = true,
                                      EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                                      TextVerticalAlignment verticalAlignment = TextVerticalAlignment::CENTER);
  static int getNumberOfItemsPerPage(const GfxRenderer& renderer, bool hasHeader, bool hasTabBar, bool hasButtonHints,
                                     bool hasSubtitle, int extraReservedHeight = 0);
  static std::string getCoverThumbPath(std::string coverBmpPath, int coverHeight);
  static UIIcon getFileIcon(const std::string& filename);
  static int getStatusBarHeight();
  static int getProgressBarHeight();

 private:
  AlmanacTheme currentTheme;
  mutable ThemeMetrics adjustedMetrics;
  mutable bool metricsValid = false;
  mutable bool metricsForTouch = false;
};

// Helper macro to access current theme
#define GUI UITheme::getInstance().getTheme()

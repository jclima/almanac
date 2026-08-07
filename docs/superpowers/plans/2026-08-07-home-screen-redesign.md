# Home Screen Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Home's 400px recent-book cover with a branded masthead, a full-width Continue Reading tile, a 2-column label grid, and a pinned Settings tile.

**Architecture:** All Home tile geometry becomes `constexpr` free functions in `src/components/themes/MenuLayout.h`, called by both `AlmanacTheme` (to draw) and `HomeActivity` (to hit-test), so the two cannot disagree. Drawing changes live in `AlmanacTheme`; `HomeActivity` loses its cover tile and its ~16 KB snapshot buffer. Nothing in `lib/` changes.

**Tech Stack:** C++20 (`-std=c++2a`, no exceptions, no RTTI), PlatformIO, ESP32-C3, GoogleTest on host via CMake/CTest.

**Spec:** [docs/superpowers/specs/2026-08-07-home-screen-redesign-design.md](../specs/2026-08-07-home-screen-redesign-design.md)

## Global Constraints

- Work in the worktree `/Users/jclima/opt/flightreader/.claude/worktrees/home-redesign` on branch `feature/home-screen-redesign`. Do **not** touch `/Users/jclima/opt/flightreader` itself — it is on an unrelated branch (`codex/tesserae-integration`) with uncommitted work belonging to another session.
- `pio` is not on `PATH`. Use `~/.platformio/penv/bin/pio`.
- Never use bare `new`. Use `makeUniqueNoThrow` from `lib/Memory/Memory.h`, or `new (std::nothrow)` with a null check. Under `-fno-exceptions` bare `new` calls `abort()` on OOM.
- All user-facing text uses `tr(STR_*)`. Log lines (`LOG_DBG`/`LOG_ERR`) stay hardcoded.
- Never hardcode 800 or 480. Use `renderer.getScreenWidth()` / `getScreenHeight()`.
- Every `std::vector` gets `.reserve(n)` before a `push_back` loop.
- Line limit is 120 columns (`.clang-format`). Format with `./bin/clang-format-fix`, which needs clang-format **21** on `PATH` (`pip install 'clang-format==21.*'`). Never use Apple's `/usr/bin/clang-format`.
- Never commit `*.generated.h`, `.pio/`, `compile_commands.json`, or `platformio.local.ini`. Run `git status` before every `git add`.
- Commit messages: `<type>: <summary>` with types `feat`/`fix`/`refactor`/`docs`/`test`/`chore`/`perf`.
- Push only to the `fork` remote, never `origin` (which is upstream CrossPoint). Do not push without asking.

---

### Task 1: Preflight — green baseline

**Files:** none modified.

**Interfaces:**
- Consumes: nothing.
- Produces: a verified-green starting point and the RAM/Flash baseline every later task compares against.

- [ ] **Step 1: Initialise submodules**

A freshly created worktree has empty submodules and will not build without this.

```bash
cd /Users/jclima/opt/flightreader/.claude/worktrees/home-redesign
git submodule update --init --recursive
```

- [ ] **Step 2: Confirm the branch and a clean tree**

```bash
git -C /Users/jclima/opt/flightreader/.claude/worktrees/home-redesign status --short --branch
```

Expected: `## feature/home-screen-redesign` and no modified files.

- [ ] **Step 3: Baseline firmware build**

```bash
~/.platformio/penv/bin/pio run -e default
```

Expected: `SUCCESS`. Record the reported `RAM:` and `Flash:` lines.

- [ ] **Step 4: Baseline host tests**

```bash
~/.platformio/penv/bin/pio run -t unit-tests
```

Expected: all pass. Record the total count; it must not drop later.

- [ ] **Step 5: Install the formatter if missing**

```bash
clang-format --version || pip install 'clang-format==21.*'
```

Expected: version 21.x. Version 20 or Apple's clang-format reformats unrelated files and fails CI.

---

### Task 2: Home tile geometry in MenuLayout

The core of the change, and the only host-testable part, so it goes first and is written test-first.

**Files:**
- Modify: `src/components/themes/BaseTheme.h` (make `Rect`'s constructor `constexpr`)
- Modify: `src/components/themes/MenuLayout.h` (add the Home tier geometry)
- Test: `test/home_menu_layout/HomeMenuLayoutTest.cpp`

**Interfaces:**
- Consumes: `ThemeMetrics`, `Rect`, `MenuLayout::buttonHintsTop` — all already in these two headers.
- Produces, all in namespace `MenuLayout`, all `constexpr`:
  - `struct HomeComposition { int tileCount; bool leadingWideTile; };`
  - `int homeGridTileCount(HomeComposition)`
  - `int homeGridRowCount(HomeComposition)`
  - `int homeSettingsTop(const ThemeMetrics&, int pageHeight)`
  - `int homeGridTop(const ThemeMetrics&, int pageHeight, HomeComposition)`
  - `Rect homeTileRect(const ThemeMetrics&, int pageWidth, int pageHeight, HomeComposition, int index)`
  - constants `kHomeMastheadHeight = 112`, `kHomeTierGap = 12`, `kHomeWideTileHeight = 92`, `kHomeGridTileHeight = 128`, `kHomeGridColumnGap = 14`, `kHomeGridColumns = 2`

- [ ] **Step 1: Make `Rect` a literal type**

`homeTileRect` returns a `Rect` from a `constexpr` function, which needs a `constexpr` constructor. `Rect` is otherwise already a literal type.

In `src/components/themes/BaseTheme.h`, replace:

```cpp
  explicit Rect(int x = 0, int y = 0, int width = 0, int height = 0) : x(x), y(y), width(width), height(height) {}
```

with:

```cpp
  constexpr explicit Rect(int x = 0, int y = 0, int width = 0, int height = 0)
      : x(x), y(y), width(width), height(height) {}
```

- [ ] **Step 2: Write the failing tests**

In `test/home_menu_layout/HomeMenuLayoutTest.cpp`, add these two helpers inside the existing anonymous namespace:

```cpp
constexpr int kPortraitWidth = 480;

MenuLayout::HomeComposition composition(const bool hasRecentBook, const bool hasOpds) {
  // Base entries: Browse Files, Recent Books, File Transfer, Nearby Flights, Settings.
  const int count = 5 + (hasOpds ? 1 : 0) + (hasRecentBook ? 1 : 0);
  return MenuLayout::HomeComposition{count, hasRecentBook};
}

bool overlaps(const Rect& a, const Rect& b) {
  return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}
```

Then add these tests after the anonymous namespace closes:

```cpp
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
```

- [ ] **Step 3: Run the tests to verify they fail**

```bash
~/.platformio/penv/bin/pio run -t unit-tests
```

Expected: compile error — `homeTileRect` is not a member of `MenuLayout`. That is the correct failure; it proves the tests reach the new API.

- [ ] **Step 4: Implement the geometry**

Append inside `namespace MenuLayout {` in `src/components/themes/MenuLayout.h`, before the closing brace:

```cpp
// --- Home tile layout -------------------------------------------------------
// Home is four fixed tiers (masthead, Continue Reading, grid, Settings) rather
// than the uniform rows menuRowLayout() describes, so it gets its own
// arithmetic. It lives here, constexpr and renderer-free, because
// test/home_menu_layout/ links no theme translation unit and can only reach
// header-inline code -- the same reason the rest of this header exists.
constexpr int kHomeMastheadHeight = 112;   // full-bleed brand bar; carries the mark, not just a baseline
constexpr int kHomeTierGap = 12;           // gap between tiers, and between grid rows
constexpr int kHomeWideTileHeight = 92;    // Continue Reading and Settings
constexpr int kHomeGridTileHeight = 128;   // fixed in every composition; the centring rule depends on it
constexpr int kHomeGridColumnGap = 14;
constexpr int kHomeGridColumns = 2;

// tileCount is every selectable entry including both wide tiles.
// leadingWideTile is true when index 0 is Continue Reading. Without it the
// "index 0 is full width" rule would make Browse Files full width on a device
// with no recent book, because the theme cannot see HomeActivity's book list.
struct HomeComposition {
  int tileCount;
  bool leadingWideTile;
};

// Tiles in the grid: everything except the leading Continue Reading tile (when
// present) and the trailing Settings tile (always).
constexpr int homeGridTileCount(const HomeComposition c) {
  const int wide = (c.leadingWideTile ? 1 : 0) + 1;
  return c.tileCount > wide ? c.tileCount - wide : 0;
}

constexpr int homeGridRowCount(const HomeComposition c) {
  const int n = homeGridTileCount(c);
  return (n + kHomeGridColumns - 1) / kHomeGridColumns;
}

// Top of the Settings tier, measured up from the hints bar so the grid always
// has a fixed floor.
constexpr int homeSettingsTop(const ThemeMetrics& metrics, const int pageHeight) {
  return buttonHintsTop(metrics, pageHeight) - kHomeTierGap - kHomeWideTileHeight;
}

// Top of the region the grid may occupy: directly below whichever tier precedes it.
constexpr int homeGridRegionTop(const HomeComposition c) {
  return c.leadingWideTile ? kHomeMastheadHeight + kHomeTierGap + kHomeWideTileHeight + kHomeTierGap
                           : kHomeMastheadHeight + kHomeTierGap;
}

// The grid block is centred in its region rather than stretched, so a row count
// that changes with OPDS needs no special-casing and tile height stays fixed.
constexpr int homeGridTop(const ThemeMetrics& metrics, const int pageHeight, const HomeComposition c) {
  const int regionTop = homeGridRegionTop(c);
  const int regionHeight = homeSettingsTop(metrics, pageHeight) - kHomeTierGap - regionTop;
  const int rows = homeGridRowCount(c);
  const int blockHeight = rows > 0 ? rows * kHomeGridTileHeight + (rows - 1) * kHomeTierGap : 0;
  const int slack = regionHeight - blockHeight;
  return regionTop + (slack > 0 ? slack / 2 : 0);
}

// Rect for one tile. Queried by AlmanacTheme to draw and by HomeActivity to
// hit-test, which is what keeps drawn tiles and touch targets from drifting.
constexpr Rect homeTileRect(const ThemeMetrics& metrics, const int pageWidth, const int pageHeight,
                            const HomeComposition c, const int index) {
  const int sidePad = metrics.contentSidePadding;
  const int fullWidth = pageWidth - sidePad * 2;

  if (index <= 0 && c.leadingWideTile) {
    return Rect{sidePad, kHomeMastheadHeight + kHomeTierGap, fullWidth, kHomeWideTileHeight};
  }
  if (index >= c.tileCount - 1) {
    return Rect{sidePad, homeSettingsTop(metrics, pageHeight), fullWidth, kHomeWideTileHeight};
  }

  const int gridIndex = index - (c.leadingWideTile ? 1 : 0);
  const int row = gridIndex / kHomeGridColumns;
  const int col = gridIndex % kHomeGridColumns;
  const int y = homeGridTop(metrics, pageHeight, c) + row * (kHomeGridTileHeight + kHomeTierGap);

  // A final row holding one tile spans the full width instead of leaving a gap.
  const bool loneInFinalRow = row == homeGridRowCount(c) - 1 && homeGridTileCount(c) % kHomeGridColumns == 1;
  if (loneInFinalRow) {
    return Rect{sidePad, y, fullWidth, kHomeGridTileHeight};
  }

  const int tileWidth = (fullWidth - kHomeGridColumnGap) / kHomeGridColumns;
  return Rect{sidePad + col * (tileWidth + kHomeGridColumnGap), y, tileWidth, kHomeGridTileHeight};
}
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
~/.platformio/penv/bin/pio run -t unit-tests
```

Expected: all pass, including the five new `HomeTileLayout` cases, with the pre-existing `HomeMenuLayout` cases still passing.

- [ ] **Step 6: Format and commit**

```bash
cd /Users/jclima/opt/flightreader/.claude/worktrees/home-redesign
./bin/clang-format-fix -g
git status --short
git add src/components/themes/MenuLayout.h src/components/themes/BaseTheme.h test/home_menu_layout/HomeMenuLayoutTest.cpp
git commit -m "feat: add Home tile geometry to MenuLayout

Four fixed tiers -- masthead, Continue Reading, a 2-column grid and a
pinned Settings tile -- as constexpr functions queried by both the theme
and HomeActivity, so drawn tiles and touch targets cannot drift apart.

The grid block is centred in its region rather than stretched, so a row
count that changes with OPDS needs no special-casing and tile height
stays fixed at 128 in all four compositions.

Rect's constructor becomes constexpr so homeTileRect can return one.

Tests cover all four compositions: nothing overlaps the masthead or the
hints bar, no two tiles overlap, row counts and grid origins match the
spec, and the fixed tiers never move."
```

---

### Task 3: Masthead mark asset

**Files:**
- Modify: `scripts/generate_logo.py`
- Create: `src/images/Logo64Inv.h` (generated)

**Interfaces:**
- Consumes: nothing.
- Produces: `static const uint8_t Logo64Inv[]`, a packed 1-bit 64×64 array with inverted bits, drawn by the existing `GfxRenderer::drawImage`.

**Why inverted:** `drawImage` blits a 1-bit array in which `bit == 0` is ink, and has no invert parameter. Flipping the bits at generation time gives white ink over the masthead's black fill with no renderer change. The mark's former white margin becomes ink and coincides with the bar's own black.

- [ ] **Step 1: Parameterise the generator**

`scripts/generate_logo.py:24` hardcodes `SIZE = 120` and `ARRAY_NAME = "Logo120"`. Read the script first — it supersamples, downsamples with LANCZOS and thresholds, so it already handles arbitrary sizes; only the constants and filenames are fixed.

Add `--size` (default 120) and `--invert` (default off) arguments. Derive `SIZE` from `--size`; derive `ARRAY_NAME` and the output paths as `Logo{SIZE}` plus an `Inv` suffix when inverting. Apply inversion in `pack()` by flipping the threshold comparison, so the packed bits are inverted rather than the preview image.

Defaults must leave `python3 scripts/generate_logo.py` producing byte-identical `Logo120.h`.

- [ ] **Step 2: Preview before committing to a size**

The script's header comment records that 120 was chosen so the compass rose's diagonals land cleanly at 1 bit. Smaller sizes are where stair-stepping appears, so look before choosing.

```bash
cd /Users/jclima/opt/flightreader/.claude/worktrees/home-redesign
python3 scripts/generate_logo.py --size 56 --preview
python3 scripts/generate_logo.py --size 64 --preview
```

Open `src/images/Logo56.png` and `src/images/Logo64.png`. Pick whichever renders the rose cleanly; 64 is expected, being a whole fraction of the supersampling grid.

**If neither is acceptable:** stop and report. The fallback is to keep the 120px mark and raise `kHomeMastheadHeight` to fit it, which moves the grid and changes Task 2's expected origins — a spec change, not a judgement call to make silently.

- [ ] **Step 3: Generate the committed asset**

```bash
python3 scripts/generate_logo.py --size 64 --invert
```

Expected: `src/images/Logo64Inv.h` containing `static const uint8_t Logo64Inv[]`.

- [ ] **Step 4: Verify the boot mark is untouched**

```bash
python3 scripts/generate_logo.py
git diff --stat src/images/Logo120.h
```

Expected: no output from the diff. Boot and sleep keep the 120px mark.

- [ ] **Step 5: Commit**

```bash
git status --short
git add scripts/generate_logo.py src/images/Logo64Inv.h
git commit -m "feat: generate an inverted 64px mark for the Home masthead

generate_logo.py takes --size and --invert, both defaulting so the
boot/sleep mark regenerates byte-identically.

The masthead is a black fill and drawImage has no invert parameter, so
the asset ships pre-inverted: bit == 0 is ink, and flipping at
generation time yields white ink on black with no renderer change.
Logo120 cannot be reused because drawImage blits at native size."
```

Do not commit the `.png` previews unless `src/images/*.png` is already tracked — check `git status` first.

---

### Task 4: Draw the masthead and the tiered menu

**Files:**
- Modify: `src/components/themes/almanac/AlmanacTheme.h`
- Modify: `src/components/themes/almanac/AlmanacTheme.cpp`

**Interfaces:**
- Consumes: `MenuLayout::homeTileRect`, `HomeComposition`, `kHomeMastheadHeight` (Task 2); `Logo64Inv` (Task 3).
- Produces:
  - `void drawHomeMasthead(GfxRenderer& renderer, Rect rect) const;`
  - `void drawHomeMenu(GfxRenderer& renderer, int pageWidth, int pageHeight, MenuLayout::HomeComposition composition, int selectedIndex, const std::function<std::string(int index)>& tileLabel) const;`

`drawButtonMenu` is left untouched — the settings screens still use it. These are additive, which keeps a signature change from rippling into unrelated call sites.

- [ ] **Step 1: Declare the new methods**

In `AlmanacTheme.h`, add `#include "components/themes/MenuLayout.h"` and declare inside the class:

```cpp
  // Home's brand bar. Home-specific rather than a drawHeader variant: it
  // carries the mark and no title, and its height is a layout tier rather
  // than a ThemeMetrics value.
  void drawHomeMasthead(GfxRenderer& renderer, Rect rect) const;

  // Home's tiered tile menu. Takes the composition rather than deriving it,
  // because only HomeActivity knows whether a recent book exists.
  void drawHomeMenu(GfxRenderer& renderer, int pageWidth, int pageHeight, MenuLayout::HomeComposition composition,
                    int selectedIndex, const std::function<std::string(int index)>& tileLabel) const;
```

- [ ] **Step 2: Implement the masthead**

In `AlmanacTheme.cpp`, add `#include "components/themes/MenuLayout.h"` and `#include "images/Logo64Inv.h"`, then:

```cpp
void AlmanacTheme::drawHomeMasthead(GfxRenderer& renderer, const Rect rect) const {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, true);

  constexpr int kMarkSize = 64;
  const int sidePadding = AlmanacMetrics::values.contentSidePadding;
  // x must stay byte-aligned: drawImage is a byte-aligned blit and snaps to
  // 8px along the rotated axis. contentSidePadding is 16, which is aligned.
  const int markX = rect.x + sidePadding;
  const int markY = rect.y + (rect.height - kMarkSize) / 2;
  renderer.drawImage(Logo64Inv, markX, markY, kMarkSize, kMarkSize);

  const int wordmarkX = markX + kMarkSize + sidePadding;
  const int wordmarkY = rect.y + (rect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  renderer.drawText(UI_12_FONT_ID, wordmarkX, wordmarkY, tr(STR_APP_NAME), false, EpdFontFamily::BOLD);

  // Battery, right-aligned, white on the bar. BaseTheme::drawBatteryRight
  // cannot be reused here for the same reason drawHeader avoids it: its
  // casing helper is hardcoded to black ink. See drawBatteryPictogramWhite.
  const bool showPercentage =
      SETTINGS.hideBatteryPercentage != AlmanacSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int smallTextY = rect.y + (rect.height - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
  const int rightEdge = rect.x + rect.width - sidePadding;
  if (showPercentage) {
    const std::string batteryText = std::to_string(powerManager.getBatteryPercentage()) + "%";
    const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, batteryText.c_str());
    renderer.drawText(SMALL_FONT_ID, rightEdge - textWidth, smallTextY, batteryText.c_str(), false);
  } else {
    const int iconWidth = AlmanacMetrics::values.batteryWidth;
    const int iconHeight = AlmanacMetrics::values.batteryHeight;
    drawBatteryPictogramWhite(renderer, rightEdge - iconWidth,
                              smallTextY + (renderer.getLineHeight(SMALL_FONT_ID) - iconHeight) / 2, iconWidth,
                              iconHeight, powerManager.getBatteryPercentage());
  }
}
```

`drawBatteryPictogramWhite` is already in this file's anonymous namespace. `STR_APP_NAME` already exists — `BootActivity` draws it under the splash mark.

- [ ] **Step 3: Implement the tiered menu**

Append to `AlmanacTheme.cpp`:

```cpp
void AlmanacTheme::drawHomeMenu(GfxRenderer& renderer, const int pageWidth, const int pageHeight,
                                const MenuLayout::HomeComposition composition, const int selectedIndex,
                                const std::function<std::string(int index)>& tileLabel) const {
  for (int i = 0; i < composition.tileCount; i++) {
    const Rect tile = MenuLayout::homeTileRect(AlmanacMetrics::values, pageWidth, pageHeight, composition, i);
    const bool selected = i == selectedIndex;

    if (selected) {
      // Same two-level emphasis the list selection uses: filled, plus a
      // heavier stroke outside it with a white gap between the two.
      renderer.fillRect(tile.x, tile.y, tile.width, tile.height, true);
      renderer.drawRect(tile.x - kSelectionStroke, tile.y - kSelectionStroke, tile.width + kSelectionStroke * 2 - 1,
                        tile.height + kSelectionStroke * 2 - 1, kSelectionStrokeWidth, true);
    } else {
      renderer.drawRect(tile.x, tile.y, tile.width - 1, tile.height - 1, kMenuTileStroke, true);
    }

    // Wrapped to two lines rather than truncated: the longest
    // STR_MENU_RECENT_BOOKS translation is ~23 characters, which does not fit
    // a 217px tile on one line, and "Recent Bo..." is worse than two lines.
    const std::string label = tileLabel(i);
    const int inset = AlmanacMetrics::values.contentSidePadding / 2;
    UITheme::drawCenteredWrappedText(renderer, Rect{tile.x + inset, tile.y, tile.width - inset * 2, tile.height},
                                     UI_10_FONT_ID, label.c_str(), 2, !selected);
  }
}
```

- [ ] **Step 4: Build**

```bash
~/.platformio/penv/bin/pio run -e default
```

Expected: `SUCCESS`. Nothing calls these yet, so behaviour is unchanged; this proves they compile.

- [ ] **Step 5: Commit**

```bash
./bin/clang-format-fix -g
git status --short
git add src/components/themes/almanac/AlmanacTheme.h src/components/themes/almanac/AlmanacTheme.cpp
git commit -m "feat: draw the Almanac Home masthead and tiered tile menu

Additive: drawButtonMenu is untouched, so the settings screens that use
it are unaffected. Nothing calls these yet -- HomeActivity is wired next.

Tile rects come from MenuLayout::homeTileRect, the same function
HomeActivity hit-tests against.

Tiles are label-only. The UIIcon enum is threaded through every theme
API but never rendered anywhere: the tree's only drawIcon call site is
OpdsBookBrowserActivity, and src/components/icons/ holds just bookmark
and search24. Icon tiles would mean six new assets plus a UIIcon
mapping, which is its own change."
```

---

### Task 5: Wire HomeActivity to the new layout

**Files:**
- Modify: `src/components/themes/almanac/AlmanacTheme.h` (flip `homeContinueReadingInMenu`)
- Modify: `src/activities/home/HomeActivity.h`
- Modify: `src/activities/home/HomeActivity.cpp`

**Interfaces:**
- Consumes: `drawHomeMasthead`, `drawHomeMenu` (Task 4); `MenuLayout::homeTileRect`, `HomeComposition`, `kHomeMastheadHeight` (Task 2).
- Produces: `MenuLayout::HomeComposition HomeActivity::menuComposition() const`, used by both `render()` and `loop()`.

- [ ] **Step 1: Flip the metric**

In `AlmanacMetrics::values` in `src/components/themes/almanac/AlmanacTheme.h`, change `.homeContinueReadingInMenu = false,` to `.homeContinueReadingInMenu = true,`.

This makes the existing code at `HomeActivity.cpp:341-344` insert `STR_CONTINUE_READING` at index 0, and makes the selection arithmetic at `HomeActivity.cpp:270-291` stop subtracting `recentBooks.size()`. Both branches already exist — they were written for a theme since deleted.

**Verify the count balances.** `getMenuItemCount()` returns `5 + recentBooks.size() + (opds ? 1 : 0)`; `render()` builds `menuItems.size() == 5 + (opds ? 1 : 0) + (continueReading ? 1 : 0)`. These agree only while `homeRecentBooksCount == 1`, which `AlmanacMetrics` sets. Do not change `homeRecentBooksCount`.

- [ ] **Step 2: Add the composition accessor**

In `HomeActivity.h` add `#include "components/themes/MenuLayout.h"` and declare next to `menuRect()`:

```cpp
  // Shared by render() and loop() so drawn tiles and touch targets agree.
  MenuLayout::HomeComposition menuComposition() const;
```

In `HomeActivity.cpp`:

```cpp
MenuLayout::HomeComposition HomeActivity::menuComposition() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const bool hasContinueReading = metrics.homeContinueReadingInMenu && !recentBooks.empty();
  return MenuLayout::HomeComposition{getMenuItemCount(), hasContinueReading};
}
```

- [ ] **Step 3: Replace the render body**

Delete the `drawHeader` call, the four `coverRect*` assignments and the `drawRecentBookCover` call (lines 316-329), and replace the `drawButtonMenu` call. The `menuItems`/`menuIcons` construction at lines 331-345 stays exactly as-is — `menuIcons` is still built and simply not consumed, which Task 6 cleans up.

```cpp
void HomeActivity::render(RenderLock&&) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHomeMasthead(renderer, Rect{0, 0, pageWidth, MenuLayout::kHomeMastheadHeight});

  // ... menuItems / menuIcons construction unchanged ...

  GUI.drawHomeMenu(renderer, pageWidth, pageHeight, menuComposition(), selectorIndex,
                   [&menuItems](int index) { return std::string(menuItems[index]); });

  const auto labels = mappedInput.mapLabels(recentBooks.empty() ? "" : tr(STR_RESUME), tr(STR_SELECT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  }
}
```

The `loadRecentCovers` call goes with the cover; `recentsLoaded`/`recentsLoading` become dead and are removed in Task 6.

- [ ] **Step 4: Replace the hit-test**

In `HomeActivity::loop`, delete the two cover-tile touch blocks (the `homeTopPadding`/`homeCoverTileHeight` rect tests) — that region is the masthead now and is not selectable. Replace the `rowTouch` block with a point-in-rect test against the same function the drawing uses:

```cpp
  const auto composition = menuComposition();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto tileAt = [&](const int px, const int py, int& index) {
    for (int i = 0; i < composition.tileCount; i++) {
      const Rect t = MenuLayout::homeTileRect(metrics, renderer.getScreenWidth(), renderer.getScreenHeight(),
                                              composition, i);
      if (px >= t.x && px < t.x + t.width && py >= t.y && py < t.y + t.height) {
        index = i;
        return true;
      }
    }
    return false;
  };

  int tx = 0;
  int ty = 0;
  int touchedIndex = -1;
  if (mappedInput.wasScreenTouchDown(tx, ty) && tileAt(tx, ty, touchedIndex)) {
    if (selectorIndex != touchedIndex) {
      selectorIndex = touchedIndex;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasScreenTap(tx, ty) && tileAt(tx, ty, touchedIndex)) {
    selectorIndex = touchedIndex;
    activateSelection();
    return;
  }
```

**Check the tap API before writing this.** `mappedInput.wasScreenTap(tx, ty)` is assumed by symmetry with `wasScreenTouchDown`; the existing code uses `wasTapInRect(...)` instead. Grep `MappedInputManager.h` for the available tap accessors and use whichever reports tap coordinates. If only `wasTapInRect` exists, loop the tiles and call it per rect rather than inventing an accessor.

- [ ] **Step 5: Build and test**

```bash
~/.platformio/penv/bin/pio run -e default && ~/.platformio/penv/bin/pio run -t unit-tests
```

Expected: `SUCCESS`, all tests pass. Report the Flash delta against Task 1.

- [ ] **Step 6: See it before committing**

```bash
~/.platformio/penv/bin/pio run -e simulator -t run_simulator
```

Check in the simulator: the mark renders white-on-black and is not stair-stepped or byte-misaligned; the wordmark and battery are legible; the Continue Reading tile is full width and shows the book title; grid labels wrap rather than clip; nothing touches the hints bar. Confirm the grid re-centres with OPDS on versus off.

- [ ] **Step 7: Commit**

```bash
./bin/clang-format-fix -g
git status --short
git add src/activities/home/HomeActivity.h src/activities/home/HomeActivity.cpp src/components/themes/almanac/AlmanacTheme.h
git commit -m "feat: rebuild Home around the masthead and tiered tiles

The 400px recent-book cover -- half an 800px screen -- is gone. Home now
opens with the Almanac mark and a full-width Continue Reading tile.

homeContinueReadingInMenu flips to true, which the existing insertion
and selection arithmetic already handled; it was written for a theme
since deleted. The count balance depends on homeRecentBooksCount
staying 1.

Touch hit-tests against MenuLayout::homeTileRect, the same function the
theme draws from, so drawn tiles and touch targets cannot drift."
```

---

### Task 6: Remove the dead cover path

Separate commit: deletion of now-unreachable code, not part of the behaviour change above.

**Files:**
- Modify: `src/activities/home/HomeActivity.h`, `src/activities/home/HomeActivity.cpp`

**Interfaces:**
- Consumes: Task 5 (nothing calls the cover path any more).
- Produces: no API. Removes `coverBuffer` and its ~16 KB transient allocation.

- [ ] **Step 1: Delete the members and methods**

From `HomeActivity.h` remove `coverRendered`, `coverBufferStored`, `coverBuffer`, `coverBufferSize`, `coverRectX/Y/W/H`, `recentsLoading`, `recentsLoaded`, and the declarations of `storeCoverBuffer`, `restoreCoverBuffer`, `freeCoverBuffer`, `loadRecentCovers`.

From `HomeActivity.cpp` remove all four definitions and the `freeCoverBuffer()` call in `onExit()`. Also remove the now-unused `menuIcons` vector and the `<Bitmap.h>` include if nothing else in the file uses them.

- [ ] **Step 2: Confirm nothing else referenced them**

```bash
cd /Users/jclima/opt/flightreader/.claude/worktrees/home-redesign
grep -rn "storeCoverBuffer\|restoreCoverBuffer\|freeCoverBuffer\|loadRecentCovers\|coverBuffer" src/ lib/
```

Expected: no output. `BaseTheme::drawRecentBookCover` remains and is now unused — leave it; removing a theme virtual is a separate concern that would widen this diff.

- [ ] **Step 3: Build and test**

```bash
~/.platformio/penv/bin/pio run -e default && ~/.platformio/penv/bin/pio run -t unit-tests
```

Expected: `SUCCESS`, tests pass. Report the Flash delta — this one should be negative.

- [ ] **Step 4: Commit**

```bash
./bin/clang-format-fix -g
git status --short
git add src/activities/home/HomeActivity.h src/activities/home/HomeActivity.cpp
git commit -m "refactor: drop HomeActivity's dead cover-snapshot path

The cover tile is gone, so storeCoverBuffer/restoreCoverBuffer/
freeCoverBuffer/loadRecentCovers are unreachable. Removing them also
removes a ~16KB transient malloc taken on every Home render, which is
real relief on a device with no PSRAM and one 48KB framebuffer.

BaseTheme::drawRecentBookCover is left in place; removing a theme
virtual is its own concern."
```

---

### Task 7: Full verification

**Files:** none modified.

**Interfaces:**
- Consumes: Tasks 1-6.
- Produces: the numbers and the device checklist for handoff.

- [ ] **Step 1: Clean build**

```bash
cd /Users/jclima/opt/flightreader/.claude/worktrees/home-redesign
~/.platformio/penv/bin/pio run -t clean
~/.platformio/penv/bin/pio run -e default
```

Expected: `SUCCESS`, zero warnings. Report RAM and Flash against Task 1's baseline.

- [ ] **Step 2: Full host test suite**

```bash
~/.platformio/penv/bin/pio run -t unit-tests
```

Expected: every test passes; the total is no lower than Task 1's baseline.

- [ ] **Step 3: Static analysis**

```bash
~/.platformio/penv/bin/pio check
```

Expected: no new defects versus baseline.

- [ ] **Step 4: Formatting is idempotent**

```bash
./bin/clang-format-fix
git diff --exit-code
```

Expected: no diff. A diff means CI's format job would fail.

- [ ] **Step 5: Confirm no generated or ignored files were committed**

```bash
git log --stat develop..HEAD | grep -E "generated\.h|\.pio/|compile_commands|platformio\.local"
```

Expected: no output.

- [ ] **Step 6: Hand off the device checklist**

These need hardware and cannot be self-verified. Report them rather than claiming them:

- 🔲 Home with a recent book and with none (fresh install / cleared recents) — the grid re-centres and Continue Reading appears only when a book exists.
- 🔲 Home with and without an OPDS server configured — 2 rows versus 3, nothing touching the hints bar.
- 🔲 The masthead mark is crisp at 64px on the real panel, and byte-aligned (no horizontal smear).
- 🔲 Continue Reading opens the most recent book; Back still resumes it.
- 🔲 Serial log shows no `Outside range` from `GfxRenderer` — the specific symptom of an off-screen draw, logged once per out-of-bounds pixel.
- 🔲 Free heap after several Home entries and exits shows no downward drift.

---

## Self-Review

**Spec coverage.** Masthead and mark: Tasks 3, 4. Continue Reading tile: Tasks 4, 5. Grid and Settings tier: Tasks 2, 4. `homeTileRect` as single source of truth: Task 2, consumed by 4 and 5. Two vertical variants: Task 2 Step 2 asserts all four origins. 1-D navigation: no task, correctly — the spec's decision is to change nothing, and `ButtonNavigator` stays untouched. Label wrapping: Task 4 Step 3. Test generalisation: Task 2 Step 2. Cover buffer removal: Task 6. Network tile, tile icons, progress bar: all correctly absent, each with a spec section explaining why.

**Placeholder scan.** Two steps stop rather than guess, both with an explicit instruction not to invent an API: the tap-coordinate accessor in Task 5 Step 4, and the mark size in Task 3 Step 2 (whose fallback changes the spec's geometry). Both name the fallback. Everything else carries the actual code. The two placeholders in the first draft of this plan — a `UIIcon`-to-bitmap mapping and a `RecentBook::progress` field — were resolved by checking: neither exists, so icons and the progress bar were cut from the spec rather than left as guesses in the plan.

**Type consistency.** `HomeComposition{tileCount, leadingWideTile}` is defined in Task 2 and used unchanged in Tasks 4 and 5. `homeTileRect(metrics, pageWidth, pageHeight, composition, index)` keeps that argument order at all three call sites. `drawHomeMenu(renderer, pageWidth, pageHeight, composition, selectedIndex, tileLabel)` matches between its declaration (Task 4 Step 1), definition (Step 3) and call (Task 5 Step 3). `drawHomeMasthead(renderer, rect)` likewise. The generated array is `Logo64Inv` in Task 3 and Task 4.

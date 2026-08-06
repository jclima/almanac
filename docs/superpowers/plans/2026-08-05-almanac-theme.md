# Almanac Theme Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an "instrument panel" UI theme called Almanac — solid header/footer bars, a framed list with hairline separators, double-stroke selection — available alongside the existing four themes and set as the new default.

**Architecture:** A new `AlmanacTheme` subclassing `BaseTheme` in `src/components/themes/almanac/`, with its own `AlmanacMetrics::values` constant, overriding four draw virtuals plus the two list-metric accessors. Registered via one enum value, one `UITheme::setTheme` case, one Settings picker entry, and one i18n string.

**Tech Stack:** C++20 (Arduino/ESP-IDF via pioarduino). No host tests — no theme or `Activity` in this codebase has one; verification is the firmware build plus arithmetic re-derivation plus on-device checks.

**Spec:** [docs/superpowers/specs/2026-08-05-almanac-theme-design.md](../specs/2026-08-05-almanac-theme-design.md)

## Global Constraints

- Target: Xteink X4, ESP32-C3, ~380KB RAM, no PSRAM, 800×480 1-bit e-ink. Baseline before this plan: **178/178 host tests, firmware SUCCESS, RAM 15.5%, Flash 85.1%**. Flash is the tight resource — report the delta.
- All user-facing strings via `tr(STR_*)`; log lines hardcoded English.
- No hardcoded layout numbers in *activity* code — but a theme's own metrics struct is exactly where literals belong. Inside `AlmanacTheme`'s draw methods, derive from `AlmanacMetrics::values` and the passed `Rect`, never from `BaseMetrics`.
- `-fno-exceptions`: never bare `new`; use `makeUniqueNoThrow` if any allocation is needed (it should not be — themes draw, they don't allocate).
- Lines ≤120 columns (`.clang-format`, `ColumnLimit: 120`).

**Verification commands** (from the repo root, `/Users/jclima/opt/flightreader`):
```bash
~/.platformio/penv/bin/pio run
```
```bash
ctest --test-dir build/test --output-on-failure
```
```bash
python3 scripts/gen_i18n.py lib/I18n/translations /tmp/i18n-check --verbose
```

PlatformIO is installed at `~/.platformio/penv/bin/pio` (not on PATH), ~40s incremental.

## Two traps that will silently break this

Both were found while scoping. Neither produces a compiler error.

**Trap 1 — `getListRowStep` hardcodes `BaseMetrics`.**
`BaseTheme::getListRowStep` (`src/components/themes/BaseTheme.cpp`) reads
`BaseMetrics::values.listRowHeight` / `.listWithSubtitleRowHeight` **directly**,
not the active theme's metrics. This is why `LyraTheme` overrides it. If
Almanac changes row heights without overriding `getListRowStep` and
`getListPageItems`, lists will *draw* rows at Almanac's heights while
*stepping* and *paginating* at Base's — overlapping or gapped rows, and a
page-item count that doesn't match what's visible. **Overriding both is
mandatory, not optional.**

**Trap 2 — the enum and the picker list must change together.**
`CrossPointSettings::UI_THEME` values are persisted numerically in settings
JSON, and the Settings picker's `enumValues` list in `SettingsList.h` is
*positional* (index 0 = `CLASSIC`, 1 = `LYRA`, 2 = `LYRA_3_COVERS`,
3 = `ROUNDEDRAFF`). `CrossPointSettings::fromJson` clamps `SettingType::ENUM`
values to `enumValues.size()`. So:
- **Append** `ALMANAC = 4`; never insert mid-enum (that would reassign every
  saved theme).
- **Append** a fifth `StrId` to the picker list in the same change. Adding the
  enum value alone means the clamp resets the setting to the default on every
  load — an unselectable theme with no error anywhere.

---

## Task 1: AlmanacMetrics, theme skeleton, and wiring

Deliverable: "Almanac" appears in Settings, can be selected, persists across a reboot, and is the default on a fresh install. It renders identically to Classic apart from the new spacing — the distinctive drawing comes in Task 2.

**Files:**
- Create: `src/components/themes/almanac/AlmanacTheme.h`
- Create: `src/components/themes/almanac/AlmanacTheme.cpp`
- Modify: `src/CrossPointSettings.h`
- Modify: `src/components/UITheme.cpp`
- Modify: `src/SettingsList.h`
- Modify: `lib/I18n/translations/english.yaml`

**Interfaces:**
- Produces: `AlmanacMetrics::values` (a `constexpr ThemeMetrics`), `class AlmanacTheme : public BaseTheme`, and `CrossPointSettings::UI_THEME::ALMANAC` — all consumed by Task 2.

- [ ] **Step 1: Create the metrics struct and class declaration**

Create `src/components/themes/almanac/AlmanacTheme.h`.

**Do not hand-type the metrics.** `ThemeMetrics` has ~60 fields and
`LyraTheme.h` initializes every one with a designated initializer. Copy
`BaseMetrics::values` verbatim from `src/components/themes/BaseTheme.h`,
rename the namespace to `AlmanacMetrics`, and change exactly these five:

| Field | Base value | Almanac value |
|---|---|---|
| `headerHeight` | 45 | **56** |
| `buttonHintsHeight` | 40 | **48** |
| `listRowHeight` | 30 | **34** |
| `listWithSubtitleRowHeight` | 50 | **52** |
| `contentSidePadding` | 20 | **16** |

Every other field keeps its Base value. Structure the file on `LyraTheme.h`:

```cpp
#pragma once

#include "components/themes/BaseTheme.h"

class GfxRenderer;

// Almanac theme metrics (zero runtime cost -- constexpr, lives in flash).
namespace AlmanacMetrics {
constexpr ThemeMetrics values = {
    // ... every field copied from BaseMetrics::values, with the five above changed ...
};
}  // namespace AlmanacMetrics

// "Instrument panel" theme: solid header/footer bars, a single frame around
// the list with hairline row separators, and a two-level selection (filled
// row plus a heavier stroke outside it).
class AlmanacTheme final : public BaseTheme {
 public:
  // BaseTheme::getListRowStep reads BaseMetrics directly rather than the
  // active theme's metrics, so a theme with different row heights MUST
  // override both of these or lists draw and step at different pitches.
  int getListRowStep(bool hasSubtitle) const override;
  int getListPageItems(int contentHeight, bool hasSubtitle) const override;
};
```

Confirm the exact `override` signatures against `src/components/themes/BaseTheme.h` before writing them — do not copy from memory.

- [ ] **Step 2: Implement the list-metric overrides**

Create `src/components/themes/almanac/AlmanacTheme.cpp`:

```cpp
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
```

Match `BaseTheme`'s equivalents for behaviour (including the `rowStep <= 0` guard and the `std::max(1, ...)` floor) — only the metrics source differs.

- [ ] **Step 3: Add the enum value**

In `src/CrossPointSettings.h`, change:
```cpp
  enum UI_THEME { CLASSIC = 0, LYRA = 1, LYRA_3_COVERS = 2, ROUNDEDRAFF = 3 };
```
to:
```cpp
  enum UI_THEME { CLASSIC = 0, LYRA = 1, LYRA_3_COVERS = 2, ROUNDEDRAFF = 3, ALMANAC = 4 };
```

Appended, not inserted — see Trap 2.

Then find the `uiTheme` member declaration in the same file and change its default initializer to `ALMANAC` so a fresh install boots into the new theme. Quote the before/after line in your report.

- [ ] **Step 4: Register in UITheme**

In `src/components/UITheme.cpp`, add the include alongside the others:
```cpp
#include "components/themes/almanac/AlmanacTheme.h"
```

and add a case to `setTheme`'s switch (which has no `default:`, so it will not compile until you do):
```cpp
    case CrossPointSettings::UI_THEME::ALMANAC:
      LOG_DBG("UI", "Using Almanac theme");
      currentTheme = std::make_unique<AlmanacTheme>();
      currentMetrics = &AlmanacMetrics::values;
      break;
```

- [ ] **Step 5: Add the i18n string and the picker entry**

Add to `lib/I18n/translations/english.yaml`, next to the other theme names:
```yaml
STR_THEME_ALMANAC: "Almanac"
```

Then in `src/SettingsList.h`, append it to the theme picker's value list — currently:
```cpp
        SettingInfo::Enum(StrId::STR_UI_THEME, &CrossPointSettings::uiTheme,
                          {StrId::STR_THEME_CLASSIC, StrId::STR_THEME_LYRA, StrId::STR_THEME_LYRA_EXTENDED,
                           StrId::STR_THEME_ROUNDEDRAFF},
                          "uiTheme", StrId::STR_CAT_DISPLAY),
```
The new entry goes **last**, so its index (4) matches `ALMANAC = 4`. Getting this wrong doesn't fail to build — it silently mislabels themes or makes Almanac unselectable (Trap 2).

- [ ] **Step 6: Verify**

```bash
python3 scripts/gen_i18n.py lib/I18n/translations /tmp/i18n-check --verbose
```
Expected: zero missing keys.

```bash
~/.platformio/penv/bin/pio run
```
Expected: SUCCESS, no new warnings. Report the RAM/Flash line and the flash delta against the 5,575,819 B baseline.

```bash
ctest --test-dir build/test --output-on-failure
```
Expected: 178/178 (unchanged — nothing here is host-testable).

```bash
awk 'length > 120 {print FILENAME":"FNR}' src/components/themes/almanac/AlmanacTheme.h src/components/themes/almanac/AlmanacTheme.cpp
```
Expected: no output.

**Also verify by inspection:** that `AlmanacMetrics::values` differs from `BaseMetrics::values` in exactly the five fields listed in Step 1 and no others. A stray difference introduced while copying is the most likely defect in this task. Diff them field by field and state the result in your report.

- [ ] **Step 7: Commit**

```bash
git add src/components/themes/almanac src/CrossPointSettings.h src/components/UITheme.cpp src/SettingsList.h lib/I18n/translations/english.yaml
git commit -m "feat: add Almanac theme skeleton, metrics, and registration"
```

- [ ] **Step 8: Flag for device check**

🔲 **Device**: Settings › Display › Theme should list "Almanac" as a fifth option; selecting it should apply and survive a reboot. At this stage it should look like Classic with slightly roomier spacing.

---

## Task 2: The instrument-panel drawing

Deliverable: the theme's actual identity — solid bars, framed list, two-level selection — plus verification that the new metrics don't break the flights/radar geometry.

**Files:**
- Modify: `src/components/themes/almanac/AlmanacTheme.h`
- Modify: `src/components/themes/almanac/AlmanacTheme.cpp`

**Interfaces:**
- Consumes: `AlmanacMetrics::values`, `AlmanacTheme` (Task 1); `GfxRenderer`'s `fillRect`, `drawRect`, `drawLine`, `drawText`, `drawCenteredText`, `truncatedText`; `BaseTheme`'s virtual signatures and `UIIcon`.

- [ ] **Step 1: Read the base implementations you are replacing**

Before writing anything, read these in `src/components/themes/BaseTheme.cpp`:
`drawHeader`, `drawButtonHints`, `drawList`, `drawButtonMenu`. Also read
`LyraTheme.cpp`'s versions of the same four — Lyra already overrides all of
them, so it shows the established shape for a full override (how it handles
the `Rect`, the callbacks, truncation, icons, and the selected index).

Your overrides must preserve every behaviour the base versions provide that
isn't explicitly being changed: text truncation to the available width, the
optional subtitle/icon/value callbacks in `drawList`, the `rowDimmed`
predicate, the selected-index handling, and the paging arithmetic. Dropping
one of those silently breaks a screen you haven't looked at — `drawList` is
used by Settings, the file browser, OPDS, and the flights list, not just one
place.

State in your report which base behaviours you carried over and how.

- [ ] **Step 2: Declare the four overrides**

Add to `AlmanacTheme`'s public section in the header, matching
`BaseTheme.h`'s signatures exactly (verify them; `drawButtonHints` takes a
non-const `GfxRenderer&` while `drawList`/`drawHeader` take a const ref):

```cpp
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
                      const std::function<UIIcon(int index)>& buttonIcon) const override;
```

Copy the real signatures from `BaseTheme.h` rather than these — if they have
drifted, the header is the truth and a mismatched signature silently creates
a new overload instead of an override. Consider adding `override` (already
shown) so the compiler catches exactly that.

- [ ] **Step 3: Implement `drawHeader` — solid bar**

Fill `rect` black, then draw the title in white, left-aligned at
`AlmanacMetrics::values.contentSidePadding`, vertically centred in the bar.
If `subtitle` is non-null, render it too — the flights feature uses the
subtitle slot for a transient error message, so dropping it would lose that.
Truncate the title to the width available (minus the padding, and minus the
subtitle's width if both share the bar) using `renderer.truncatedText`.

White text is `drawText(..., /*black=*/false, ...)`.

- [ ] **Step 4: Implement `drawButtonHints` — solid bar**

Fill the hints strip black across the full screen width, at height
`AlmanacMetrics::values.buttonHintsHeight`, anchored to the bottom of the
screen (derive from `renderer.getScreenHeight()`, matching how `BaseTheme`
positions it — read it rather than assuming). Draw the four labels in white,
evenly distributed, skipping empty strings (several screens pass `""` for
unused slots and must not render a stray separator or gap artifact).

- [ ] **Step 5: Implement `drawList` — frame, separators, two-level selection**

The structural change, in order:

1. Draw one rectangle around the whole list area (`drawRect` with the `Rect`
   inset by a small margin), 1–2 px stroke.
2. For each visible row after the first, draw a hairline separator
   (`drawLine`, 1 px) across the frame's interior width.
3. For the selected row: `fillRect` it black, then draw a heavier stroke
   (`drawRect`, ~3 px) just *outside* that filled area. The two-level
   emphasis is what reads as "panel" rather than merely "inverted" — a single
   inverted bar is what Classic already does.
4. Draw each row's title/subtitle/value/icon via the callbacks, inverted
   (white) for the selected row and black otherwise, honouring `rowDimmed`.

Row pitch must come from `getListRowStep(hasSubtitle)` — the override from
Task 1 — never from a literal. Paging must use `getListPageItems`. Deriving
the visible window any other way is how the two get out of sync.

Watch the selection stroke at the first and last visible rows: drawn outside
the fill, it can extend past the frame. Decide how to handle that (inset the
frame enough to absorb it, or clip the stroke at the frame) and say which.

- [ ] **Step 6: Implement `drawButtonMenu` — boxed menu**

The Home screen's icon menu. Box each button to match the list's frame
language, with the selected one filled plus the heavier outside stroke, same
as the list's selection. Preserve the icon rendering and label truncation the
base version does.

Note `RoundedRaffTheme::drawButtonMenu` ignores its icon callback
(`(void)rowIcon`) while `LyraTheme` renders icons via `iconForName` — read
both and decide which behaviour Almanac wants. State the choice.

- [ ] **Step 7: Re-derive the flights/radar geometry for the new metrics**

**This is the integration check the spec calls out and it must be done from
the code as written, not from the spec's figures.**

`NearbyFlightsActivity::renderRadar` and `renderDetail` both compute their
geometry from `UITheme::getInstance().getMetrics()` at render time, so
Almanac is a new configuration for code validated only against the other
four themes.

Read the actual formulas in `src/activities/flights/NearbyFlightsActivity.cpp`
and compute, for Almanac in **both** orientations (480×800 and 800×480):

- `contentTop`, `stripHeight`, `plotBottom`, `plotHeight`, `cx`, `cy`, and
  `radiusPx` for the radar. **Is `radiusPx` positive?** Do the ring labels and
  compass letters stay clear of the header bar and the readout strip?
- `firstLineY`, `maxY`, and how many of the 10 detail lines fit. Confirm the
  two-pass truncation keeps every drawn line inside
  `getScreenHeight() - buttonHintsHeight`.

Present both as a table in your report alongside the same figures for the
other four themes, so the comparison is explicit. A negative or absurd
`radiusPx`, or any line drawing past its bound, is a blocker — report it
rather than nudging constants until it looks right.

- [ ] **Step 8: Verify**

```bash
~/.platformio/penv/bin/pio run
```
Expected: SUCCESS, no new warnings. Report RAM/Flash and the delta.

```bash
ctest --test-dir build/test --output-on-failure
```
Expected: 178/178.

```bash
awk 'length > 120 {print FILENAME":"FNR}' src/components/themes/almanac/AlmanacTheme.h src/components/themes/almanac/AlmanacTheme.cpp
```
Expected: no output.

- [ ] **Step 9: Commit**

```bash
git add src/components/themes/almanac
git commit -m "feat: implement Almanac instrument-panel drawing"
```

- [ ] **Step 10: Flag for device check**

🔲 **Device**: with Almanac selected, check **every** screen the theme
touches, in **both** orientations: Home, Settings (including a long scrolling
list), the file browser, the flights list, the radar, flight detail, and the
reader's status bar. Confirm nothing clips, collides, or overlaps, and that
list rows neither overlap nor leave gaps (the `getListRowStep` trap would show
up exactly there).

🔲 **Device**: watch the serial log for `Outside range` from `GfxRenderer` —
that is the specific symptom of an off-screen draw, and it logs once per
out-of-bounds pixel, so it is loud when it happens.

---

## Self-Review Notes

- **Spec coverage:** instrument direction (Task 2 Steps 3–6), framed list with hairline separators rather than per-row boxes (Step 5), coexists as the new default (Task 1 Steps 3–5), five metrics changes (Task 1 Step 1), chrome-only with no typography or branding (nothing in either task touches fonts, `STR_CROSSPOINT`, or the splash).
- **Both traps have explicit steps:** `getListRowStep`/`getListPageItems` overrides are Task 1 Step 2; the enum-plus-picker coupling is Task 1 Steps 3 and 5, with the failure mode spelled out.
- **Type consistency:** `AlmanacMetrics::values` and `AlmanacTheme` are defined in Task 1 and consumed in Task 2; the four override signatures are specified to be copied from `BaseTheme.h` rather than from this plan, because a signature drift here fails silently as a new overload.
- **Ordering:** Task 2 depends on Task 1. Task 1 alone produces a selectable, working theme.

# Almanac-Only Theme — Design Spec

Date: 2026-08-07
Status: Approved (autonomous session; decisions derived from code evidence and
the prior Almanac spec/review chain, recorded here in lieu of interactive
approval)
Scope: Personal fork of CrossPoint Reader for the Xteink X4.

## Goal

Finish the Almanac theme work started in
`2026-08-05-almanac-theme-design.md`: fix the one defect that blocked it from
becoming the default (the 6th Home tile clipping under the button-hints bar),
make it the default, and — superseding the earlier spec's "coexistence"
decision — **delete the other themes** (Classic as a selectable option, Lyra,
Lyra 3 Covers, RoundedRaff) so Almanac is the fork's only look.

## Why deletion now

The earlier spec kept the other themes "available for comparison". The
comparison served its purpose during review; the standing direction now is
Almanac-only. Deleting the rest removes ~6 theme translation strings × 31
languages and three theme implementations from flash, removes a Settings
picker with only one meaningful answer, and shrinks the surface future
upstream merges have to reconcile.

## Part 1 — Fix: `AlmanacTheme::drawButtonMenu` pages on `rect.height`

The defect (documented in commits `938678e6` and `962091a2`): Home passes a
correct menu rect (Portrait: y=450, height=302 under Almanac), but
`drawButtonMenu` lays tiles at fixed pitch — `verticalSpacing` (10) top
offset, `menuRowHeight` (45) + `menuSpacing` (8) pitch — and never reads
`rect.height`. Six tiles (OPDS configured) need 320 px in 302, an 18 px
overlap into the solid hints bar that clips the 6th tile's label.

**Fix: page on `rect.height`**, the same windowing Almanac's own `drawList`
and `RoundedRaffTheme::drawButtonMenu` already use:

- `rowStep = menuRowHeight + menuSpacing` (53)
- usable tile span = `rect.height - verticalSpacing - kSelectionStroke`
  (the selected tile's outer stroke reaches `kSelectionStroke` = 3 px past
  its fill, so the last tile keeps its bottom that far inside the rect)
- `pageItems = max(1, (usable + menuSpacing) / rowStep)`
- `pageStart = (max(0, selectedIndex) / pageItems) * pageItems`; draw
  `[pageStart, min(buttonCount, pageStart + pageItems))` at
  `tileY = rect.y + verticalSpacing + (i - pageStart) * rowStep`
- a scrollbar thumb (same 4 px gauge strip as `drawList` step 4) when
  `buttonCount > pageItems`, so page 1 shows there is more below.

Worked figures (Portrait 480×800, menu rect height 302):
`usable = 289`, `pageItems = (289+8)/53 = 5`. Five tiles end at y-offset 267,
stroke reach 270 ≤ 302. With 5 menu items (no OPDS) nothing pages and the
rendered layout is pixel-identical to today. With 6, page 1 holds five tiles
and page 2 holds Settings alone; `ButtonNavigator` wraps, so page 2 is one
press Up from the top.

Touch devices: `getMetrics()` zeroes `buttonHintsHeight`, so the rect is
350 px, `pageItems = (337+8)/53 = 6`, and all six tiles fit on one page —
paging never activates where `HomeActivity`'s unpaged `rowTouch` mapping is
in play, so the known touch/pitch assumptions are unchanged.

`BaseTheme::drawButtonMenu` has the same defect but becomes unreachable in
Part 2 (Almanac overrides it; no other theme survives), so it is left alone.

## Part 2 — Flip the default

`CrossPointSettings::uiTheme` default `LYRA → ALMANAC`, deleting the hold
comment. This is the exact revert `962091a2` instructed once the Part 1 fix
landed, kept as its own commit so the intermediate "all themes present,
Almanac default" state the original branch review approved exists in history.

## Part 3 — Delete the other themes

**Deleted:** `src/components/themes/lyra/` (4 files),
`src/components/themes/roundedraff/` (2 files), and
`src/components/icons/cover.h` (included only by those three `.cpp` files).

**Kept: `BaseTheme`.** It is the base class supplying the ~15 draw methods
Almanac does not override (battery, popups, status bar, tab bar, recent-book
cover, keyboard field, …). Only its role as the selectable "Classic" theme
ends; its class comment is updated to say so.

**Settings surface collapses:**

- `CrossPointSettings`: `UI_THEME` enum and `uiTheme` field removed.
- `SettingsList.h`: the "UI Theme" picker entry removed — a one-option
  picker is noise.
- `UITheme`: `setTheme()` and `reload()` removed; the instance holds an
  `AlmanacTheme` by value (drops the `unique_ptr` and a static-init heap
  allocation; the object is stateless). `getMetrics()` reads
  `AlmanacMetrics::values` directly. The `reload()` call sites in
  `main.cpp` (post-`loadFromFile` re-apply) and `SettingsActivity::onExit`
  (re-apply on theme change) are removed with it — both exist only to apply
  a theme *choice*, which no longer exists.
- i18n: `STR_UI_THEME` and the five `STR_THEME_*` keys removed from all 31
  translation YAMLs; generated tables regenerated (gitignored).

**Migration:** none needed. `fromJson`/`toJson` iterate `getSettingsList()`,
so with the picker entry gone the persisted `"uiTheme"` key is ignored on
load and dropped on the next save. Any saved value (including this device's
likely `LYRA`) simply stops mattering — the device boots Almanac
unconditionally, which is the requested behavior.

**Untouched:** `ThemeMetrics` keeps all its fields (activities read them
generically; `homeContinueReadingInMenu` etc. are metric-driven, not
theme-class-driven), and no activity code changes beyond the two `reload()`
call sites.

## Commit sequence

1. `fix:` Almanac `drawButtonMenu` pages on `rect.height` (Part 1)
2. `fix:` default theme to Almanac (Part 2)
3. `refactor:` delete non-Almanac themes and the theme setting (Part 3)

Each state builds and is shippable on its own.

## Verification

- `pio run` clean per commit; report RAM/Flash deltas (deletion should
  *reduce* flash; report the number).
- `ctest` host suite still 178/178 (no theme has host tests; guards against
  collateral damage).
- Arithmetic re-derived in Part 1 above; the radar/detail metric constraints
  from the earlier spec are unaffected (metrics values do not change).
- Device checks to flag for the user: 6-tile Home paging on hardware
  (OPDS configured), and one pass over Home/Settings/browser/flights/reader
  confirming nothing regressed with the setting removed. Watch serial for
  `Outside range`.

## Out of scope

- Any visual change to Almanac beyond the paging fix — its look was
  device-verified in the prior review.
- Removing now-dead generic branches (e.g. `homeContinueReadingInMenu`
  paths) or `ThemeMetrics` fields — wide ripple, no reader-facing benefit.
- Renaming `BaseTheme`, or making themes SD-loadable.

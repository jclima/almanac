# Almanac Theme — Design Spec

Date: 2026-08-05
Status: Approved
Scope: Personal fork of CrossPoint Reader for the Xteink **X4**.

## Goal

Give the fork its own visual identity — an "instrument panel" theme called
**Almanac** — so it no longer reads as stock CrossPoint. Chrome and layout
only: no typography change, no branding change, no boot-splash change.

## Why a theme rather than editing the existing ones

`BaseTheme` already exposes 14 virtual draw methods and a per-theme
`ThemeMetrics` struct, and `UITheme::setTheme` dispatches on a `UI_THEME`
enum. Adding a theme is therefore self-contained in new files plus a handful
of one-line registrations — which keeps future merges from upstream clean,
and lets the other three themes stay available for comparison.

Note upstream's `SCOPE.md` freezes the theming surface and wants themes moved
off-firmware to the SD card. That's upstream policy; this is a personal fork
and does not inherit it. The flash cost here is small (one class, one metrics
struct) and does not foreclose an SD-loaded theme system later.

## Decisions

| Question | Decision |
|---|---|
| Visual direction | **Instrument panel** — solid header/footer bars, framed list, double-stroke selection |
| Depth | Chrome + layout only. No custom fonts, no branding, no splash |
| Coexistence | Joins Classic / Lyra / Lyra 3 Covers / RoundedRaff, and becomes the **default** |
| Row treatment | **One frame around the list with hairline separators**, not a box per row |

### Why framed-with-separators rather than a box per row

Per-row boxes are the strongest version of the look but need internal padding
plus a border, pushing rows from ~50 px to ~62 px. In portrait that drops the
flight list from ~13 visible rows to ~10 — and the test location reliably
returns the full 20-match cap, so the scrolling cost is real and constant.
A single enclosing frame with thin separators preserves the panel feel (the
solid bars and the double-stroke selection do most of that work) at roughly
today's density.

## What the theme overrides

Four of `BaseTheme`'s virtuals, all in `AlmanacTheme`:

- **`drawHeader`** — a filled black bar spanning the full width with white
  title text, replacing the base theme's title-plus-rule.
- **`drawButtonHints`** — the same treatment at the bottom, so the two bars
  bookend the screen. This is the single most recognisable element.
- **`drawList`** — one rectangle enclosing the whole list; hairline rules
  between rows; the selected row filled black with a heavier stroke drawn
  just outside it (two-level emphasis is what reads as "panel" rather than
  merely "inverted").
- **`drawButtonMenu`** — the Home screen's icon menu, boxed to match.

Everything else inherits `BaseTheme` unchanged. The theme applies globally —
Home, Settings, the flights/radar screens, and the reader's status bar — not
just to one screen.

## Metrics

A new `AlmanacMetrics::values`, taking `BaseMetrics::values` as the starting
point and changing only what the look requires:

| Field | Base | Almanac | Why |
|---|---|---|---|
| `headerHeight` | 45 | 56 | A solid bar needs more presence than a text baseline |
| `buttonHintsHeight` | 40 | 48 | Matches the header bar visually |
| `listRowHeight` | 30 | 34 | Breathing room inside the frame |
| `listWithSubtitleRowHeight` | 50 | 52 | Minimal increase — density is the constraint |
| `contentSidePadding` | 20 | 16 | The frame supplies the visual margin |

All other fields keep their `BaseMetrics` values.

## Integration risk: these metrics feed the flights feature

This is the part that needs real verification rather than assumption. The
radar plot and the detail-screen truncation both derive their geometry from
`ThemeMetrics` at render time, so a new metrics set is a new configuration
for code that was only ever validated against the existing three.

Worked through in advance:

- **Radar**, portrait 480×800: `contentTop = 5+56+10 = 71`,
  `stripHeight = 34×3 = 102`, `plotBottom = 800−48−20−102 = 630`,
  `plotHeight = 559`, `radiusPx = min(240, 279) − 22 = 218`. Healthy.
- **Radar**, landscape 800×480: `plotBottom = 480−48−20−102 = 310`,
  `plotHeight = 239`, `radiusPx = min(400, 119) − 22 = 97`. Healthy.
  (`plotMargin` is `max(contentSidePadding, SELECTED_RING_RADIUS=22)`, so
  dropping side padding to 16 does not shrink the margin.)
- **Detail screen**, landscape 800×480: `firstLineY = 5+56+10+34 = 105`,
  `maxY = 480−48−34 = 398` → **9 of 10 lines fit**. The two-pass truncation
  added for exactly this case begins doing real work in this theme, dropping
  the lowest-priority line (data age). That is correct behaviour, not a
  defect, but it must be confirmed rather than assumed.

The implementation must re-derive all of the above from the code as written
rather than trusting these figures, and must confirm `radiusPx` stays
positive and the detail screen never draws past its bound in **every**
theme × orientation combination — Almanac included.

## Wiring

- `AlmanacSettings::UI_THEME` gains `ALMANAC`. **Append it** rather than
  inserting: the enum's numeric values are persisted in settings JSON, so
  inserting in the middle would silently reassign every user's saved theme.
- `UITheme::setTheme` gains a case (the switch is exhaustive with no
  `default:`, so the compiler will flag it until handled).
- The Settings theme picker gains an entry, and `english.yaml` gains one
  string for the theme's display name.

  **This registration is load-bearing, not cosmetic.** The picker's
  `enumValues` list in `SettingsList.h` is *positional* — index 0 is
  `CLASSIC`, 1 is `LYRA`, 2 is `LYRA_3_COVERS`, 3 is `ROUNDEDRAFF` — and
  `AlmanacSettings::fromJson` clamps `SettingType::ENUM` values to
  `enumValues.size()`. Adding `ALMANAC = 4` to the enum *without* appending a
  fifth entry to that list would make the clamp silently reset the setting to
  the default on every load, producing a theme that cannot be selected and no
  error anywhere. Both must change together, and the list order must continue
  to match the enum's numeric order exactly.
- `AlmanacSettings::uiTheme`'s default flips to `ALMANAC` so a fresh
  install boots into it. Existing installs keep whatever is already saved.

## Testing

No host tests: no theme or `Activity` in this codebase has one, and this is
rendering code. Verification is:

- Firmware builds clean with no new warnings; flash cost stays well inside
  the current 85.1 % (a class plus a metrics struct should be a rounding
  error, but report the delta).
- The metrics arithmetic above re-derived from the code for all five themes
  × both orientations.
- On-device: every screen the theme touches — Home, Settings, file browser,
  flights list, radar, flight detail, and the reader status bar — checked in
  portrait and landscape, confirming nothing clips, collides, or renders
  off-screen. Watch the serial log for `Outside range` from `GfxRenderer`,
  which is the specific symptom of an off-screen draw.

## Out of scope

- Typography. UI fonts are baked into flash at every size and style, and at
  85.1 % there may not be room; it would need measuring first.
- Branding: the "CrossPoint" name on screen, the boot splash, the version
  string. Deliberately left so upstream merges don't conflict on them and so
  the version string stays usable for identifying which build is flashed.
- Any change to the existing four themes.
- Moving themes to SD-card loading (upstream's stated direction). This theme
  is written so that migration is not made harder, but does not attempt it.
- Custom icons — the existing `UIIcon` set is reused.

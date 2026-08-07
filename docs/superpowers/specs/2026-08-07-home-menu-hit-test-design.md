# Home Menu Touch Hit-Test — Design Spec

Date: 2026-08-07
Status: Implemented
Scope: Tap accuracy on the home menu. **No drawn pixels move.**

## Goal

`HomeActivity::loop()` re-derives the home menu's row geometry from
`ThemeMetrics` instead of asking the active theme what it actually drew. Make
the theme the single source of truth for its own menu geometry, and have both
the draw pass and the hit-test read from it.

Button navigation already works — items are reachable, just not tappable in the
right place. This is an accuracy fix, not a functionality fix.

## Relationship to the overlap fix

This follows [the button-hints overlap fix](2026-08-05-almanac-theme-design.md)
(PR #5, `166969d9`), which solved a *different* problem: rows colliding with the
button-hints bar. That change removed `BaseTheme`'s leading `verticalSpacing`
inset, which as a side effect already aligned Classic's drawn rows with its
hit-test. Lyra never drifted.

**So RoundedRaff is the only theme with a remaining mismatch** — but it is the
worst of the three, and PR #5 explicitly deferred it.

## The RoundedRaff mismatch

`RoundedRaffTheme::drawButtonMenu` derives its row height from the title font,
not from `ThemeMetrics`:

| | Drawn | Hit-tested (before) |
|---|---|---|
| Row height | `getLineHeight(UI_12) + 20` = **49** | `menuRowHeight` = 42 |
| Pitch | 49 + `kSelectableRowGap` 6 = **55** | `menuRowHeight + menuSpacing` = 48 |
| Paging | `pageItems = rect.height / rowStep` | none |

Roughly 7px of drift per row, accumulating downward. Worked example: drawn row 2
spans y 535–583, but a tap at y=570 resolved to `(570 - 425) / 48 = 3` — row 3.

A font-derived row height is why the geometry query **needs the renderer** and
cannot be a pure `ThemeMetrics` lookup. That is also why PR #5's
`getMenuRowStep(int, int) -> int` could not express it, and why this change
widens that virtual rather than adding a second one.

### Pagination is now live, not latent

Before `938678e6` the menu `Rect` overran the screen by ~350px, which inflated
`pageItems` to 11 and meant 7 items never reached page 2 — the oversized rect
masked the paging bug. With the rect corrected, RoundedRaff's real menu height
is 335px, so `pageItems = 335 / 55 = 6` against a maximum of 7 items
(RoundedRaff is the one theme with `homeContinueReadingInMenu = true`).

**Page 2 is reachable in the shipped configuration**, and the hit-test had no
page awareness at all, so every tap there resolved to a page-1 index.

## Design

### `MenuLayout::menuRowLayout` — shared arithmetic

`MenuRowLayout` is declared in `BaseTheme.h` next to `ThemeMetrics` (that header
is what `MenuLayout.h` depends on, so declaring it the other way round would
cycle). The factory is `constexpr` and host-testable:

```cpp
constexpr MenuRowLayout menuRowLayout(int top, int height, int rowHeight, int rowStep,
                                      int itemCount, int selectedIndex, bool paginate);
```

`rowStep` is an input rather than `rowHeight + gap` because the two families of
theme derive it differently and **both must survive**:

- Non-paginating themes pass `MenuLayout::fittedRowStep(...)`, so gaps compress
  to clear the button-hints bar. This is PR #5's mechanism and dropping it would
  reintroduce the overlap.
- RoundedRaff passes its natural font-derived pitch and paginates instead.

Two behaviours it preserves exactly:

- **Negative-selection clamp.** `selectedIndex` arrives negative when a
  recent-book row is selected on a theme with `homeContinueReadingInMenu =
  false`. Clamped before paging, matching what `RoundedRaffTheme` did inline.
- **Partial last page.** `visibleCount = min(pageItems, itemCount - firstIndex)`,
  so a tap in the empty space below a short final page is rejected.

Degenerate inputs (`rowStep <= 0`, `itemCount <= 0`) yield `visibleCount = 0`;
`rowTouch` already treats that as no-hit.

### `BaseTheme::getButtonMenuLayout` — a geometry virtual

```cpp
virtual MenuRowLayout getButtonMenuLayout(const GfxRenderer& renderer, Rect rect,
                                          int buttonCount, int selectedIndex) const;
```

Follows the existing `getListRowStep` / `getListPageItems` precedent. It
replaces PR #5's `getMenuRowStep`, which had exactly one caller and could not
carry row height or paging.

| Theme | `top` | `rowHeight` | `rowStep` | `paginate` |
|---|---|---|---|---|
| `BaseTheme` (Classic, and inherited by Lyra / Lyra 3 Covers) | `rect.y` | active `menuRowHeight` | `fittedRowStep(...)` | false |
| `AlmanacTheme` | `rect.y` | `menuRowHeight` | `fittedRowStep(height - kMenuSelectionReserve, ...)` | false |
| `RoundedRaffTheme` | `rect.y` | `getLineHeight(kTitleFontId) + 20` | `+ kSelectableRowGap` | true |

`paginate` **must** stay false for the first two families: they clear the hints
bar by compressing gaps, so paging there would silently hide menu entries rather
than shrink them. Pinned by `NonPaginatingThemesDrawEveryRowAtSix`.

### `HomeActivity::menuRect()`

`render()` and `loop()` now share one rect helper. PR #5 could only state that
contract in a comment on the virtual's declaration; this makes it structural.

Its height depends on `buttonHintsHeight`, which `getMetrics()` zeroes on touch
hardware, so it is read live rather than cached.

## Index spaces

`HomeActivity` passes the *rendered* selection (already offset by
`recentBooks.size()`) so it shares an index space with the returned
`firstIndex`, and maps back with `firstIndex + menuRow + offset`.

Today the only paging theme is also the only one with
`homeContinueReadingInMenu = true`, which makes that offset zero — so the
round-trip would work even if the spaces disagreed. Keeping both in rendered
space is what would keep a future paging theme correct without that coincidence.

## Testing

`test/home_menu_layout/` covers both concerns in one suite: the fit tests assert
rows never reach the hints bar (Classic pinned at 760, Lyra 3 Covers at 756 —
the regression canaries), and the layout tests assert reported rows are the rows
drawn, using a local mirror of `rowTouch`'s hit predicate so assertions describe
what a real tap resolves to.

Known limitation: these model the drawing geometry rather than calling
`drawButtonMenu`, which needs a `GfxRenderer`. They would not catch a change
made inside a theme's draw body.

## Out of scope

- Any change to drawn pixels. RoundedRaff's rows render exactly as before; only
  what a tap resolves to changes.
- The scroll-bar thumb now derives from the same layout, which is a correctness
  follow-on rather than a new feature.

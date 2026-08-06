# Almanac — Firmware Overhaul Design Spec

Date: 2026-08-05
Status: Approved
Scope: Hard fork of CrossPoint Reader, rebranded as **Almanac**. Target: Xteink X4 (ESP32-C3).

## Goal

Turn this personal fork into its own finished product: **Almanac**, an e-reader
firmware that also tracks nearby aircraft. Distinct identity, polished
presentation, and hardened fork-owned code.

## Decisions (confirmed with the user)

| Question | Decision |
|---|---|
| Depth of "make it our own" | **Full rebrand** — name, logo, boot splash, version string, docs |
| Relationship to upstream | **Hard fork.** No further merges from `crosspoint-reader/crosspoint-reader` |
| Product name | **Almanac** |
| Priority areas | **All four**: flight tracking, identity/UI, reading experience, footprint/code quality |

## What "full overhaul" means here — and what it does not

A literal overhaul of the 240K-LOC tree is not the deliverable and would not
serve the product. Most of that line count is vendored third-party code
(`expat`, `miniz`, `uzlib`, and ~80 generated font tables); the reader core is
mature and works today; and **no hardware is available in this session**, so a
blind rewrite of EPUB rendering would trade a working product for an untested
one. Reading-experience work is therefore limited to defects provable at the
host level, not speculative re-engineering.

The overhaul is scoped to what this fork actually owns:

1. Repair the build baseline.
2. Give the firmware its own identity (name, mark, splash, version, docs).
3. Ship the approved signature theme.
4. Harden and decompose the fork's own flights code.
5. Rename the internal namespace to match the product.
6. Hold every step to verifiable quality gates.

## Phase A — Baseline repair

The worktree could not build at all: `freeink-sdk` is a git submodule and git
worktrees do not populate submodules. `pio run` died at
`PackageException: Can not create a symbolic link for freeink-sdk/libs/hardware/BatteryMonitor`.

Fixed by `git submodule update --init --recursive`. Without this, no change
below could be distinguished from a pre-existing breakage.

**Deliverable:** clean `pio run -e default`, recorded flash/RAM baseline in
bytes (not just the 85.1% figure quoted in the theme spec).

## Phase B — Identity

### Product name

`STR_CROSSPOINT` → `STR_APP_NAME`, value `"Almanac"`, across all **31**
translation YAMLs. The product name is a proper noun and is not translated, so
every file carries the same value. Rendered by `BootActivity.cpp:17` and
`SleepActivity.cpp:163`.

`STR_CALIBRE_INSTRUCTION_1` ("Install CrossPoint Reader plugin") is left alone:
it names the third-party Calibre plugin the user must actually install, which
really is called that. Rebranding it would misdirect the user.

### Mark

The current `Logo120` is a 120×120 1-bit bookmark/chevron glyph. Almanac gets
its own: a compass rose inside an instrument bezel, tying the product name (an
almanac is a navigator's reference book), the Instrument theme's panel chrome,
and the radar screen's range rings into one shape.

`scripts/convert_icon.py` is the wrong tool for it — it rotates its input 90°
and writes to `src/components/icons/` with an `…Icon` array name, all of which
suit icons and not the logo — and `cairosvg` is not installed here. The mark
therefore ships with its own reproducible generator,
`scripts/generate_logo.py`, which draws with PIL at 8× and downsamples with
LANCZOS before thresholding so the curves and diagonals land cleanly at 120px.

A four-point rose, not the classic eight: at 120px in one bit, eight points
render as ~5px-wide spindles with no body. This was checked by rendering both.

### Version and build identity

- `CROSSPOINT_VERSION` → `ALMANAC_VERSION` (`scripts/git_branch.py`, plus every
  consumer across `main.cpp`, `OtaUpdater`, the web server, `HttpDownloader`,
  `OtaUpdateActivity`, `SettingsActivity`, `BootActivity`, and `HalSystem.cpp`).
- `CROSSPOINT_RC_HASH` → `ALMANAC_RC_HASH`, in `platformio.ini` and in
  `.github/workflows/release_candidate.yml` together — the workflow sets the
  variable the ini reads, so renaming one without the other breaks RC builds.
- `platformio.ini`'s `[crosspoint]` section → `[almanac]`; version reset to
  `1.0.0` — this is Almanac's first release, not CrossPoint's 1.5.0.
- HTTP `User-Agent` → `Almanac-ESP32-<version>`.

### OTA (a real correctness bug, not just branding)

`OtaUpdater.cpp:17` points `latestReleaseUrl` at
`crosspoint-reader/crosspoint-reader`. The version check is a plain string
inequality against `ALMANAC_VERSION`, so **every** upstream release reads as
"an update is available", and accepting it flashes CrossPoint over Almanac.

Repointed at this fork's own releases. With no release published there the
check degrades to "No update available", which is the correct outcome.

### Deliberately NOT renamed: the `.crosspoint` SD directory

`/.crosspoint/` on the SD card holds book caches, **reading progress**,
bookmarks, cover bitmaps, and sleep frames (`AlmanacState.h:24`,
`main.cpp:171`, `BookmarkUtil.cpp:6`, `RecentBooksStore.cpp:127`). Renaming it
silently orphans every one of those: progress resets to page one and bookmarks
disappear, because cache identity is a hash of the file path under that root.

The directory is on-disk state, not a user-visible brand surface. It stays.
Changing it later would require a migration pass, which is out of scope for a
rebrand.

## Phase C — Signature theme

Implements the already-approved
[Almanac theme spec](2026-08-05-almanac-theme-design.md) unchanged in substance.

**One naming change:** the product is now called Almanac, so a theme also named
"Almanac" would read as *Almanac → Settings → Theme → Almanac*. The theme ships
as **Instrument** (`InstrumentTheme`, `InstrumentMetrics`, `UI_THEME::INSTRUMENT`),
which also describes the look more accurately. Everything else — the four
overridden virtuals, the metrics table, the framed-list-with-separators
decision — carries over as specced.

The two load-bearing traps that spec identifies are carried forward verbatim:

- `UI_THEME` values are persisted in settings JSON, so `INSTRUMENT` must be
  **appended** (`= 4`), never inserted.
- `SettingsList.h`'s `enumValues` list is **positional**, and
  `AlmanacSettings::fromJson` clamps `ENUM` values to `enumValues.size()`.
  Adding the enum value without appending a fifth list entry makes the clamp
  silently reset the setting on every load — an unselectable theme with no
  error anywhere. Both change together.

Rows deliberately stay on `BaseTheme`'s grid rather than being inset inside the
frame. `MappedInputManager::listItemFromPoint` maps a tap with
`(y - listTop) / rowStep` and knows nothing about per-theme padding, so
insetting the rows would shift every row down relative to where touch believes
it is. The frame is outset above the grid instead, into the `verticalSpacing`
gap. `getListPageItems` keeps `BaseTheme`'s exact formula for the same reason —
`listItemFromPoint` calls it to decide which page a tap lands on.

### Geometry verification (re-derived from code, not from the theme spec)

Radar `radiusPx` and detail-screen line budget, computed from each theme's
actual `ThemeMetrics` values:

| Theme | X4 portrait | X4 landscape | X3 portrait | X3 landscape |
|---|---|---|---|---|
| Classic | 218 · 10/10 | 113 · 10/10 | 242 · 10/10 | 137 · 10/10 |
| Lyra | 218 · 10/10 | **69** · 7/10 | 242 · 10/10 | 93 · 8/10 |
| Lyra 3 Covers | 218 · 10/10 | **69** · 7/10 | 242 · 10/10 | 93 · 8/10 |
| RoundedRaff | 218 · 10/10 | 97 · 8/10 | 242 · 10/10 | 121 · 9/10 |
| **Instrument** | 218 · 10/10 | 97 · 9/10 | 242 · 10/10 | 121 · 10/10 |

(`radiusPx` · detail lines fitting of 10.)

`radiusPx > 0` in all 20 combinations. The theme spec's predicted figures —
portrait 218, landscape 97, and 9 of 10 detail lines in landscape — are
confirmed exactly. The two-pass detail truncation already does more work under
Lyra (7/10) than it will under Instrument, so this theme is not the worst case
for it.

## Phase D — Flights hardening

`NearbyFlightsActivity.cpp` is 794 lines — the largest file this fork owns — and
has taken seven consecutive fix commits. The code is careful and heavily
reasoned; the work here is structural and defensive, not a rewrite.

### D1. Degenerate radar geometry (latent, not live)

`renderRadar()` derives `plotHeight` from theme metrics and then
`radiusPx = min(pageWidth/2, plotHeight/2) - plotMargin`
(`NearbyFlightsActivity.cpp:437-467`). `drawCircle` guards `radius <= 0`;
nothing else does. If `radiusPx` went negative, the unguarded consumers would
draw far off-screen:

- `drawLine(cx, cy - radiusPx, cx, cy + radiusPx, ...)` — inverted crosshair
- `drawText(..., cy - radiusPx - 22, "N")` — compass letters off-panel
- `GeoMath::polarToScreen(..., radiusPx)` — every aircraft mis-plotted

`GfxRenderer::drawPixel` logs `LOG_ERR("GFX", "!! Outside range")` **once per
out-of-range pixel**, so this floods the serial log rather than failing quietly.

**It is not currently reachable.** Deriving the geometry across all five themes
× four screen configurations (see the table below) puts the tightest case at
`radiusPx = 69` (Lyra, X4 landscape). Instrument is not the tightest — it sits
at 97, level with RoundedRaff.

The guard is therefore *hardening*, not a bug fix: the margin is thin, the
failure mode is loud and ugly, and a future theme with a taller header is
exactly what would cross the line. Clamp, and skip the plot when the area is
unusable while keeping the readout strip and button hints.

### D2. Decomposition

Split rendering out of the activity so no single file carries state machine,
input handling, network orchestration, and four screen renderers at once. The
state machine, input dispatch, and fetch orchestration stay in
`NearbyFlightsActivity.cpp`; the screen painters move to a sibling translation
unit. Behaviour-preserving; verified by the build and by unchanged rendering
logic.

## Phase E — Internal namespace

`AlmanacSettings` (259 references / 49 files), `AlmanacState` (8 files),
and `AlmanacWebServer` (8 files) → `AlmanacSettings`, `AlmanacState`,
`AlmanacWebServer`, including file names.

Mechanical and fully compiler-verified. Sequenced **last** because it is the
change most likely to produce a large, noisy diff, and it lands as its own
isolated commit so it stays reviewable and revertable.

## Phase F — Documentation

`README.md` is currently upstream's, with a fork notice bolted on top. It is
rewritten for Almanac: what it is, what it does, how to build and flash, and an
honest statement of its ancestry and MIT licence.

`SCOPE.md` and `ROADMAP.md` describe upstream's governance, phases, and funding
model, none of which bind a hard fork. They are replaced with Almanac's own
scope. `GOVERNANCE.md` — upstream's community code of conduct, naming upstream's
maintainers — is removed as inapplicable to a single-author fork.

Attribution to CrossPoint Reader and Dave Allie's MIT copyright is **retained**
in `LICENSE` and credited in the README. This is a rebrand, not a claim of
sole authorship.

## Verification

### What I can verify (and will, before claiming completion)

- `pio run` clean for `default`, `gh_release`, and `slim`, with no new warnings.
- Host unit tests pass (`test/` — 14 suites, including `geo_math`,
  `opensky_states_parser`, `aircraft_info_parser`).
- `clang-format` clean across `src/`.
- Flash/RAM delta reported in bytes per phase, against the Phase A baseline.
- Radar and detail geometry re-derived arithmetically for all five themes ×
  both orientations, confirming `radiusPx > 0` and that the detail screen never
  draws past `maxY`.

### What I cannot verify — the user's step

There is no device in this session. On-device testing is explicitly the human
tester's scope per `CLAUDE.md`, and nothing below was validated visually:

1. Every screen the theme touches — Home, Settings, file browser, flights list,
   radar, flight detail, reader status bar — in **portrait and landscape**.
2. Boot splash and sleep screen render the new mark cleanly at 1-bit 120×120.
3. Serial log free of `GFX !! Outside range` on the radar screen in both
   orientations.
4. Free heap stays above ~50 KB through a flights fetch and a reading session.
5. Existing reading progress and bookmarks survive the upgrade (they should —
   `.crosspoint` is untouched — but this is the assertion most worth checking).

## Out of scope

- Rewriting or "modernising" the EPUB reader core, `BaseTheme.cpp`, or the
  vendored libraries. Untestable here, working today.
- Renaming the `.crosspoint` SD directory (see Phase B).
- Moving themes to SD-card loading — upstream's direction, a separate project.
- New features. This overhaul finishes what exists; it does not widen the
  firmware's surface.

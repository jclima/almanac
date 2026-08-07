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
almanac is a navigator's reference book), the Almanac theme's panel chrome,
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

## Phase C — Signature theme: SUPERSEDED, work discarded

This phase originally built an `InstrumentTheme` from the approved
[Almanac theme spec](2026-08-05-almanac-theme-design.md). **It was written, then
thrown away**, because `develop` had meanwhile grown a complete `AlmanacTheme`
implementing the same spec — and a better one.

Six commits on `develop` (`f13b9c62` … `962091a2`) had already landed it, three
of them fixes found *on hardware*:

- `a6822a0b` restores the charging indicator in the header. The parallel
  implementation drew a white "well" into the black bar and hosted the stock
  battery pictogram in it; it would have lost the charging bolt entirely.
  `AlmanacTheme` instead renders the percentage as white text and reuses
  `drawBatteryLightningBolt`, which already plots white — a better answer to the
  same problem (the battery outline helper is hardcoded to black ink).
- `c9da82ce` stops the list frame and selection stroke drawing off-screen, by
  framing at `width - 1, height - 1`. The discarded version framed at full
  width and would have reproduced exactly the off-panel `drawPixel` error
  storm this spec's Phase D warns about.
- `938678e6` corrects `HomeActivity`'s button-menu rect height.

`962091a2` also holds the default at **Lyra** pending a Home 6th-tile fix. The
discarded work flipped the default to its own theme, which would have reverted
that deliberate decision.

**Kept from the discarded branch:** one thing `AlmanacTheme` lacked — button
hints are drawn into a `pageWidth / 4` slot (120 px on X4 portrait) with no
truncation, while the longest hint across the 31 shipped languages is Brazilian
Portuguese's "Tentar novamente" (`STR_RETRY`, 16 characters). Ported as a
defensive `truncatedText` call, which returns the string unchanged when it fits.

**The lesson worth recording:** the theme was the one part of this overhaul
whose correctness could not be established without hardware, and it is the one
part that turned out to be redundant. Four screens of careful reasoning lost to
three commits of someone actually looking at the panel.

### Geometry verification (still valid, and now validating `AlmanacTheme`)

`AlmanacMetrics` and the discarded `InstrumentMetrics` are numerically
identical — both come from the same approved spec (`headerHeight` 56,
`contentSidePadding` 16, `listRowHeight` 34, `listWithSubtitleRowHeight` 52,
`buttonHintsHeight` 48). This derivation therefore applies verbatim to the
theme that shipped.

Radar `radiusPx` and detail-screen line budget, computed from each theme's
actual `ThemeMetrics` values:

| Theme | X4 portrait | X4 landscape | X3 portrait | X3 landscape |
|---|---|---|---|---|
| Classic | 218 · 10/10 | 113 · 10/10 | 242 · 10/10 | 137 · 10/10 |
| Lyra | 218 · 10/10 | **69** · 7/10 | 242 · 10/10 | 93 · 8/10 |
| Lyra 3 Covers | 218 · 10/10 | **69** · 7/10 | 242 · 10/10 | 93 · 8/10 |
| RoundedRaff | 218 · 10/10 | 97 · 8/10 | 242 · 10/10 | 121 · 9/10 |
| **Almanac** | 218 · 10/10 | 97 · 9/10 | 242 · 10/10 | 121 · 10/10 |

(`radiusPx` · detail lines fitting of 10.)

`radiusPx > 0` in all 20 combinations. The theme spec's predicted figures —
portrait 218, landscape 97, and 9 of 10 detail lines in landscape — are
confirmed exactly. The two-pass detail truncation already does more work under
Lyra (7/10) than it does under Almanac, so the new theme is not the worst case
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
`radiusPx = 69` (Lyra, X4 landscape). Almanac is not the tightest — it sits
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

### Verified (results, not intentions)

| Check | Result |
|---|---|
| `pio run -e default` | SUCCESS |
| `pio run -e gh_release` | SUCCESS |
| `pio run -e slim` (serial logging compiled out) | SUCCESS |
| `pio run -e sticky` (ESP32-S3, other MCU family) | SUCCESS |
| Host unit tests (`ctest`) | **178/178 passed** |
| `./bin/clang-format-fix` then `git diff --exit-code` | PASS (CI's exact gate) |
| cppcheck | **0 findings in code this branch authored** |

**On cppcheck:** `pio check -e default` passed early on with 0 high / 0 medium
and a single low `unreadVariable`, which is now suppressed inline with a
rationale. Later runs could not reinstall `tool-cppcheck` (the PlatformIO
package mirror refused connections from this machine), so the final pass was
run with cppcheck 2.17.1 and the project's own `check_flags` instead. That
configuration is noisier than PlatformIO's, and it is worth being precise about
what it found:

- **Nothing** in `NearbyFlightsRender.cpp` or
  `NearbyFlightsActivity.cpp` — the code this branch actually wrote.
- Everything it did flag is pre-existing and was left alone:
  `badBitmaskCheck` on ArduinoJson's `doc["k"] | default` idiom (that operator
  is ArduinoJson's default-value API, not a bitmask); `unknownMacro` on
  `PROGMEM` in a generated header; and three `uninitMemberVar` on
  `StreamingJsonParser`'s fixed arrays. The last was checked rather than
  assumed: `tokenBuf`, `nestingStack` and `literalExpected` are only ever read
  up to `tokenLen` / `nestingDepth` / `literalLen`, all of which `reset()`
  zeroes from the constructor. Zero-filling the arrays would cost cycles on
  every construction and change no behaviour.

All four environments matter because the version macro reaches them by two
different routes: `default` gets it injected by `scripts/git_branch.py`, while
`gh_release`/`slim`/`sticky` set it via `build_flags`. Both routes were edited,
so both were exercised. `slim` additionally proves the new `LOG_ERR` in the
radar guard compiles away cleanly with `-UENABLE_SERIAL_LOG`.

**Footprint** (against the Phase A baseline of 5,575,895 B flash / 50,668 B RAM,
with 977,705 B of app-partition headroom):

| Build | Flash | RAM |
|---|---|---|
| Baseline (`default`) | 5,575,895 | 50,668 |
| Final (`default`) | 5,579,345 (**+3,450**) | 50,660 (**−8**) |
| Final (`gh_release`) | 5,536,275 | 50,644 |
| Final (`slim`) | 5,496,349 | 50,644 |
| Final (`sticky`, ESP32-S3) | 5,388,115 | 60,308 |

The entire overhaul — a fifth theme, a new mark, the radar guard — costs
**+3.4 KB of flash and no RAM**, against ~955 KB of remaining headroom.

Geometry was re-derived arithmetically from each theme's real `ThemeMetrics`
for all five themes × four screen configurations (see the table in Phase C),
confirming `radiusPx > 0` everywhere and that the detail screen never draws
past `maxY`.

### NOT verified — the user's step

There is no device in this session, and **no pixel below was ever seen**.
On-device testing is explicitly the human tester's scope per `CLAUDE.md`.

Dropping the parallel theme removed the largest untested surface — `AlmanacTheme`
arrives already validated on hardware. What remains, in rough priority order:

1. **Existing reading progress and bookmarks survive the upgrade.** They should —
   `/.crosspoint/` is untouched by the rebrand, deliberately — but this is the
   assertion most worth checking, because it is the only one whose failure loses
   user data. The same applies to the browser's saved upload settings, whose
   localStorage key was kept for the same reason.
2. **Boot splash and sleep screen** render the new mark cleanly at 1-bit 120×120.
   It was inspected as ASCII art and as a PNG, never on e-ink.
3. **Button hints in a long-label language.** The truncation ported into
   `AlmanacTheme::drawButtonHints` changes rendering for any label that would
   have overflowed its slot. Brazilian Portuguese on the flights error screen
   ("Tentar novamente") is the case it was written for; check it reads sensibly
   truncated rather than confusingly clipped.
4. **Serial log free of `GFX !! Outside range`** on the radar screen in both
   orientations, across all five themes — the guard added in Phase D should make
   this unconditionally true, but it has only been reasoned about.
5. **Web UI** pages show Almanac and the compass mark, and the file transfer,
   settings and fonts flows still work after the rebrand.
6. **Free heap** stays above ~50 KB through a flights fetch and a reading session.
7. **OTA check** reports "no update available" rather than offering an upstream
   release.

## Out of scope

- Rewriting or "modernising" the EPUB reader core, `BaseTheme.cpp`, or the
  vendored libraries. Untestable here, working today.
- Renaming the `.crosspoint` SD directory (see Phase B).
- Moving themes to SD-card loading — upstream's direction, a separate project.
- New features. This overhaul finishes what exists; it does not widen the
  firmware's surface.

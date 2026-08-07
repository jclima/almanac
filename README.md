# Almanac

**Almanac** is e-reader firmware for the ESP32-C3-based [Xteink](https://www.xteink.com)
X4 and X3. It does two things: it renders EPUBs well on a very constrained
device, and it shows you what is flying overhead.

The name is the honest description. A nautical or aeronautical almanac is a
book of tables you carry to navigate by — part reference, part sky. That is
what this firmware is.

![Almanac running on an Xteink device](./docs/images/cover.jpg)

> ### Lineage
>
> Almanac is a **hard fork** of
> [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
> by Dave Allie and the CrossPoint contributors, used under the MIT licence
> (see [LICENSE](./LICENSE)). Essentially all of the reader engine is their
> work, and it is excellent.
>
> The fork exists because Almanac adds things CrossPoint's
> scope deliberately excludes — a network-backed flight tracker, and its own
> theming and identity. It takes no further merges from upstream and sends
> nothing back. **If you want the reader without the aviation parts, use
> CrossPoint** — it is the better-maintained, more widely tested project, and
> Almanac is one person's build.

## What it does

### Nearby Flights

Almanac's own feature. Press it and it fetches aircraft near a configured home
location from [OpenSky Network](https://opensky-network.org)'s free anonymous
API, then shows them three ways:

- **List** — distance-sorted, with callsign, bearing and altitude.
- **Radar** — a plan view with range rings, heading-oriented aircraft markers,
  and a readout for the selected aircraft.
- **Detail** — altitude, speed, heading, vertical rate, distance, origin
  country, and an on-demand aircraft-type and registration lookup via
  [adsbdb](https://www.adsbdb.com).

Fetches are **user-initiated only** — there is no background polling — and
memory use is bounded regardless of how busy the airspace is, capped at the 20
closest aircraft. Both matter on a device with ~380 KB of RAM and a battery.

### Reading

Inherited from CrossPoint and unchanged:

- **Reader engine**: EPUB 2/3 with embedded styles, images, hyphenation,
  kerning, chapter navigation, footnotes, bookmarks, dictionary lookups
  ([StarDict](docs/dictionary.md)), go-to-percent, auto page turn, orientation
  control, focus reading, and KOReader progress sync.
- **Formats**: `.epub`, `.xtc/.xtch`, `.txt`, `.bmp`.
- **Custom fonts** from the SD card.
- **Library**: folder browser, recent books, long-press delete, cache management.
- **Wireless**: file-transfer web UI, EPUB optimiser, web settings, WebDAV,
  AP and STA modes with QR helpers, Calibre wireless, OPDS browser, OTA updates.
- **Localisation**: 31 UI languages, with RTL support.

### Looking like itself

- **Almanac** — the default theme, an instrument panel. Solid black header and
  footer bars bookend the screen, the list sits in a single frame with hairline
  separators, and the selected row gets two-level emphasis: a filled bar plus a
  heavier stroke hugging it, separated by a 1px gap so the two levels stay
  distinct on a 1-bit panel. The battery reads as white text in the header bar
  rather than the usual pictogram, because the shared battery-outline helper
  only draws black ink. Classic, Lyra, Lyra Extended and RoundedRaff all remain
  available in Settings → Display → Theme.
- Its own mark — a compass rose in an instrument bezel — plus boot splash,
  sleep screen and version identity.

Existing devices keep whichever theme they already have saved; the default only
applies to a fresh install.

## Status

**Version 1.0.0** — Almanac's own numbering, restarted at 1.0.0 rather than
continuing CrossPoint's. Built and flashed on real X4 hardware.

Verified by CI on every change: the `default` and `sticky` build environments
(the two target MCU families — see [Build environments](#build-environments));
the host unit-test suite; `clang-format` (pinned to version 21); and `cppcheck`,
which fails the build on a single finding of any severity. The `gh_release`,
`gh_release_rc` and `slim` environments differ from `default` only in logging
level, so CI does not build them per change.

Verified on device: boots to Home, reads settings and caches from the SD card,
no off-panel draw errors, no panics, ~162 KB free heap at idle against a ~380 KB
total.

Not yet verified on device: the theme across all screens in both orientations,
the flight tracker end to end, and reading progress surviving an upgrade. This
is one person's firmware on one device — treat it accordingly.

## Install

Almanac publishes no binaries yet — build and flash it yourself (below).

To go back to CrossPoint or to Xteink's official firmware, use the flash tools
at <https://crosspointreader.com/#flash-tools>.

> **Note on OTA:** the in-firmware update check points at *this* repository's
> releases. It must never point at CrossPoint's — the version check is a string
> comparison, so every upstream release would read as an available update and
> installing it would flash CrossPoint over Almanac.

## Development quick start

### Prerequisites

- [pioarduino](https://github.com/pioarduino/pioarduino), or VS Code + the pioarduino plugin
- Python 3.8+
- `clang-format` 21 (the version CI pins; newer versions format differently)
- A USB-C cable that carries data

### Setup

```bash
git clone --recursive https://github.com/jclima/almanac
```

If you cloned without `--recursive` — or you are working in a **git worktree**,
which does not populate submodules — the build will fail with
`PackageException: Can not create a symbolic link for freeink-sdk/...`. Fix it with:

```bash
git submodule update --init --recursive
```

### Build / flash / monitor

```bash
pio run --target upload
```

```bash
python3 scripts/debugging_monitor.py
```

### Pre-commit checks

```bash
./bin/clang-format-fix && pio check -e default && pio run -e default
```

### Host unit tests

```bash
cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j
```

### Regenerating the mark

```bash
python3 scripts/generate_logo.py --preview
```

### Build environments

| Env | Purpose |
|---|---|
| `default` | Development — debug logging, version stamped with branch and SHA |
| `gh_release` | Production — info-level logging |
| `gh_release_rc` | Release candidate |
| `slim` | No serial logging |
| `sticky` | Seeed Sticky (ESP32-S3, 800×480) — different MCU family |

## Internals

Almanac caches aggressively to the SD card to keep RAM free; the ESP32-C3 has
only ~380 KB usable and no PSRAM. Most design decisions follow from that.

Caches, **reading progress** and bookmarks live in `/.crosspoint/` on the SD
card. That directory keeps its original name deliberately: cache identity is a
hash of each book's path beneath it, so renaming it would silently orphan every
book's progress and bookmarks.

See [docs/](docs/) for the file formats, activity manager, i18n and webserver
documentation, and [CLAUDE.md](CLAUDE.md) for the engineering constraints any
change has to respect.

## Licence

MIT — see [LICENSE](./LICENSE). Copyright (c) 2025 Dave Allie for the original
CrossPoint Reader work, which this project is built on.

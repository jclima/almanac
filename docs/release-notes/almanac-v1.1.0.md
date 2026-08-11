A feature release. The device gains a puzzle and an honest answer to "what
version am I running?", and the repository gains a release process and a
host-side tool that builds a daily briefing you can read on the panel.

The minor bump is the scope change, not the line count: `SCOPE.md` now has a
third mission pillar, and this is the release that opens it.

## 2048

A new Home tile. Four directions, one screen update per move, best score kept
across games.

It is worth being clear about what this is, because the document that governs
this fork used to forbid it. `SCOPE.md` listed "Interactive apps — games,
calculators, notepads" as out of scope. Rather than quietly cross that line,
this release moves it: the Mission gains **"Pass the time — a small turn-based
diversion for when you are waiting and not reading"**, and a new **Diversions**
section puts a frame around the door it opens. A diversion is in scope only if
all four hold:

1. **Turn-based.** One user action, one screen update. Anything that assumes a
   frame rate is out — the panel refreshes in 770–1720 ms.
2. **Playable on the buttons we have.** Seven physical buttons, four
   directional. This is what rules out text adventures, despite their being a
   natural e-reader match.
3. **No network.** Ever. Not even optional.
4. **No steady-state RAM.** State measured in bytes, no heap allocation,
   nothing retained while you are reading.

2048 was chosen because it passes all four rather than because it was the most
wanted: the board is 16 cells stored as exponents, the four directions map onto
the screen-direction buttons the input layer already rotates per orientation,
and there is no heap allocation anywhere in the feature.

**Emulators are permanently out**, and `SCOPE.md` now records why so it does
not have to be re-argued: every working ESP32 port of an NES- or Game Boy-class
emulator needs PSRAM, which the C3 does not have, and a frame rate this panel
cannot produce.

Practical details:

- The board is drawn with `FAST_REFRESH` per move and a `HALF_REFRESH` deghost
  pass on a cycle, following the reader's own refresh discipline. A move that
  changes nothing refreshes nothing, so a blocked swipe costs no ink time.
- The game is saved to `/.crosspoint/2048.json` once, on exit, behind a dirty
  flag — never per move. SPIFFS and SD erase cycles are finite and page turns
  already spend them.

## The running version is now visible

Settings shows the firmware version the device is actually running. Previously
the only way to know was to remember what you flashed.

## A daily briefing, built on your computer

New host-side tool: `scripts/generate_dashboard_epub.py`. It fetches weather,
news headlines, sun and moon times, and aircraft currently overhead, renders
them to a dated EPUB, and uploads it to the device over the File Transfer
screen you already use.

```bash
python3 -m venv .venv && .venv/bin/pip install -r scripts/requirements.txt
.venv/bin/python scripts/generate_dashboard_epub.py
```

**No firmware code changed for this.** That is the whole design, and it is why
a feature that fetches RSS can exist in a project whose scope document rules
out RSS: the exclusion protects the device's RAM, flash and battery, and this
spends none of them. The radio comes up only for the File Transfer session you
start by hand. The device's side of it is opening an EPUB, which it already
knew how to do.

Every section fails independently — if a source is unreachable it renders as
"unavailable" with a reason and the page still builds. See
[docs/mini-dashboard.md](../mini-dashboard.md).

## Fixes and internals

- **The SDL2 simulator builds and runs again.** Three regressions had stacked
  up against an environment CI does not build: `[env:simulator]` was the last
  place still defining `CROSSPOINT_VERSION` after the rename, an
  unconditional `sdkconfig.h` include had started reaching the host build
  through `OtaUpdater.h`, and the external simulator library's
  `SecureHttpClient` shim had drifted behind `freeink-sdk`. It is still not in
  CI, so it will drift again.
- **Release machinery.** A manual Release Train workflow now resolves the next
  version, bumps `platformio.ini` and `README.md`, drafts notes, and tags —
  behind gates that refuse to release from a non-`develop` ref, on red CI, or
  on a docs-only diff. Its version and notes logic is host-testable and
  covered by `scripts/test_release_train.py`.
- **Nearby Flights is documented** in the user guide.
- **The contributor guide's font IDs were wrong.** It named `FONT_UI_MEDIUM`
  and `FONT_UI`, neither of which exists; code written against it would not
  compile.

## Upgrading

**Settings → Check for Update** works from 1.0.1 or later. If you are still on
1.0.0, its update check is the bug 1.0.1 fixed and cannot be relied on — flash
`firmware.bin` over USB at offset `0x10000` once, and OTA is trustworthy from
there.

Settings, caches and reading progress in `/.crosspoint/` are unaffected. This
release adds `/.crosspoint/2048.json`; nothing existing is rewritten.

## Assets

`firmware.bin` is the `gh_release` build for the Xteink X4/X3. For a device
coming from stock or CrossPoint, flash all three binaries:

```bash
esptool.py --chip esp32c3 write_flash 0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
```

`firmware.elf` and `firmware.map` are for symbolicating crash traces. There is
no Seeed Sticky binary — it is a different MCU family and has to be built from
source (`pio run -e sticky`).

This build uses 15.6% of RAM (51,156 of 327,680 bytes) and 84.4% of the
application partition (5,531,127 of 6,553,600 bytes).

## Lineage

Almanac is a hard fork of
[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) by
Dave Allie and contributors (MIT licence). It takes no further merges from
upstream and sends nothing back. If you want the reader without the aviation
and theming additions, use CrossPoint — it is the better-maintained, more
widely tested project.

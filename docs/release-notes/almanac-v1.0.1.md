A fix release. The headline change is that the OTA update check now reads this
repository's release tags correctly; in 1.0.0 its result was indeterminate.

## Fixed

- **OTA update check could not parse Almanac's release tags.** `OtaUpdater`
  compared the release's raw `tag_name` — `almanac-v1.0.0` — against the
  firmware's bare `ALMANAC_VERSION` using `sscanf("%d.%d.%d", ...)`. `sscanf`
  fails to match on the leading `almanac-v`, returns 0 and leaves its outputs
  untouched, so the comparison ran on three uninitialized stack values: the
  result was indeterminate, and could report an update that did not exist.
  Version comparison now lives in `lib/Version/SemVer`, which skips a
  non-numeric tag prefix, requires a complete `major.minor.patch` triple, and
  reports "no update" for anything it cannot parse rather than acting on
  half-read numbers. Covered by 13 host tests.

  Bare numeric tags are not an alternative here: `0.4.0` through `1.5.0`
  already exist in this repository, inherited from CrossPoint.

- `UITheme::adjustedMetrics` is value-initialized. No behaviour change —
  `getMetrics()` already overwrote it before any read — but it removes the
  uninitialized-member gap cppcheck flagged.

- The Flight Tracker zip-code digit check uses `std::all_of`, keeping the
  explicit `'0'`–`'9'` range test rather than the locale-dependent
  `std::isdigit`, which is undefined for the negative `char` values a UTF-8
  lead byte produces.

## Releases and tooling

- Tagging `almanac-v*` now builds **and publishes** the release with its five
  assets attached; previously the workflow stopped at uploading CI artifacts
  and the release page was assembled by hand. The job also fails if the tag
  disagrees with `[almanac] version` in `platformio.ini`, since a release whose
  binary reports an older version than its tag would offer every device an
  update it can never satisfy.

- Release notes are kept in `docs/release-notes/<tag>.md` so they are reviewed
  alongside the change.

- `scripts/gen_compiledb.py` generates a working `compile_commands.json` for
  clangd. `pio run -t compiledb` does not produce a usable one in this project.

## Documentation

- The README's install section pointed at "no binaries yet"; it now points at
  the releases page, with flash offsets for both an update and a from-scratch
  flash.
- The README documents zip-code home-location entry for the Flight Tracker and
  the `simulator` build environment, both of which existed but were unlisted.

## Upgrading

**From 1.0.0, flash over USB.** Whether 1.0.0 offers this release is exactly
what was indeterminate — it may, it may not, and that is the bug being fixed.
Don't wait to find out: flash `firmware.bin` at offset `0x10000`. The update
check is trustworthy from 1.0.1 onward.

The firmware is otherwise functionally identical to 1.0.0 on device. Settings,
caches and reading progress in `/.crosspoint/` are unaffected.

## Assets

`firmware.bin` is the `gh_release` build for the Xteink X4/X3. For a device
coming from stock or CrossPoint, flash all three binaries:

```bash
esptool.py --chip esp32c3 write_flash 0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
```

`firmware.elf` and `firmware.map` are for symbolicating crash traces. There is
no Seeed Sticky binary — it is a different MCU family and has to be built from
source (`pio run -e sticky`).

## Lineage

Almanac is a hard fork of
[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) by
Dave Allie and contributors (MIT licence). It takes no further merges from
upstream and sends nothing back. If you want the reader without the aviation
and theming additions, use CrossPoint — it is the better-maintained, more
widely tested project.

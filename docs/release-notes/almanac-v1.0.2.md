A fix release. Both firmware-install paths now refuse an image built for a
different MCU, and the Home screen loses two visible rough edges.

This is also the first release a device can reach over the air: 1.0.1 fixed the
update check that 1.0.0 got wrong, so a 1.0.1 device will see and install this
one normally.

## Firmware installs now check the MCU

Almanac builds for two different chips — the Xteink X4/X3 on an ESP32-C3 and
the Seeed Sticky on an ESP32-S3 — and releases publish only the C3 binary. Two
separate paths could accept the wrong one. Neither could damage a device, but
both wasted the user's time and ended in a message that didn't explain itself.

- **Over the air, a Sticky was offered the X4's firmware.** The version
  comparison sees only the release's version number, which says nothing about
  the chip, so a release newer than the running build read as an available
  update on any device. A Sticky would download 5.5MB and then fail: the write
  is verified by `esp_ota_end()`, which checks the image header's chip id
  against the running build and refuses a mismatch before anything reboots, so
  this was a wasted transfer rather than a bad flash. The update check now
  knows no binary is published for its MCU and reports no update without
  bringing the radio up.

- **From the SD card, any wrong image was accepted.** This one is broader — it
  affects the X4 too, not just the Sticky, because the file is whatever the
  user copied onto the card. Validation checked the size, the magic byte, the
  segment table, the XOR checksum and the SHA256 trailer; an image for another
  chip passes every one of those, because it is perfectly well formed, just not
  runnable here. Nor did anything downstream catch it: this path writes with
  raw partition writes and switches boot by hand, so it never reaches the
  ESP-IDF verification the OTA path gets. The wrong image was written in full
  and only the bootloader refused it, one reboot later. Validation now reads
  the chip id out of the image header and stops with "Firmware is for another
  device".

The Sticky consequence of the first fix, stated plainly: **a Sticky has no
update path.** It will report no update indefinitely, which is correct while no
Sticky binary is published — keep it current by building and flashing from
source.

## Home screen

- The masthead mark read as visibly pixelated on device. The bezel ring's
  stroke scales with the mark, so at 64px it rounded to a single pixel, and a
  1px circle cannot render smoothly on a 1-bit panel — it stair-steps. The four
  inter-cardinal ticks had collapsed to specks for the same reason. The mark is
  now 96px, the largest whole-8 size that still fits the masthead, which lands
  the ring at 2px.

- A book whose EPUB metadata carries a whitespace-only title drew a Continue
  Reading tile with a selection frame and nothing inside it. Only a strictly
  empty title triggered the generic-label fallback; a blank one passed the
  guard. Blank titles now fall back too.

## Upgrading

**Settings → Check for Update** works from 1.0.1. If you are still on 1.0.0,
its update check is the bug 1.0.1 fixed and cannot be relied on — flash
`firmware.bin` over USB at offset `0x10000` once, and OTA is trustworthy from
there.

Settings, caches and reading progress in `/.crosspoint/` are unaffected.

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

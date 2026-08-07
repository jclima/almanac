# Almanac — Scope

Almanac is a personal, single-author fork. This document exists to keep it from
sprawling, not to govern a community.

## Mission

Do two things well on constrained hardware:

1. **Read.** EPUB rendering that is legible, fast, and does not run out of memory.
2. **Look up.** Show what is flying overhead, on demand.

Everything else is out of scope until it demonstrably serves one of those.

## The hardware sets the rules

ESP32-C3: single RISC-V core at 160 MHz, ~380 KB usable RAM, **no PSRAM**, one
48 KB framebuffer, and a display that takes 1–2 seconds to refresh fully. Flash
is 16 MB but the app partition is 6.4 MB and currently ~85% full.

A feature that is cheap on any other platform can be impossible here. Every
addition is judged on RAM first, flash second, and battery third.

## In scope

- **Reading.** Rendering, typography, hyphenation, spacing, margins, pagination,
  navigation, bookmarks, progress.
- **Flight tracking.** The list, radar and detail views, and the data behind
  them — provided fetches stay user-initiated and memory stays bounded.
- **Identity.** The Instrument theme, the mark, the boot and sleep screens.
- **Footprint and code quality.** Refactors that cut RAM or flash, or make the
  code easier to reason about, even with no user-visible change.
- **Correctness.** Anything that crashes, corrupts a cache, floods the log, or
  loses reading progress.

## Out of scope

- **Background connectivity.** Wi-Fi that stays up drains the battery and
  complicates a single-core CPU. Network access is user-initiated, does its
  work, and tears down.
- **PDF as a first-class format.** Fixed-layout pages mean pan-and-zoom on
  e-ink. Wrong medium.
- **Interactive apps** — games, calculators, notepads.
- **Authoring tools.** The input hardware and the RAM are both wrong for it.
- **General-purpose network features** — RSS, browsers, aggregators. The flight
  tracker is the one network feature, and it earns its place by being the
  reason this fork exists.

## Relationship to CrossPoint

Almanac is a hard fork. It takes no merges from
[upstream](https://github.com/crosspoint-reader/crosspoint-reader) and submits
nothing back.

That means upstream's policies do not bind this project — notably its freeze on
new in-firmware themes and on new network connectors, both of which Almanac
deliberately crosses. It also means **upstream's fixes do not arrive for free**.
That is the cost of the fork and it was chosen knowingly.

Where a change would clearly benefit CrossPoint and carries none of Almanac's
aviation scope, the polite thing is to offer it upstream as a patch rather than
let it diverge silently.

## The test for a new feature

1. Does it make reading better, or answer "what is that plane"?
2. What does it cost in RAM at peak, and in flash?
3. Does it work in all four orientations and across every theme?
4. Can it fail without taking the device or a book's progress with it?

If 1 is "no", stop. If 2 is unknown, measure before writing the feature.

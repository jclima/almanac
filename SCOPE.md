# Almanac — Scope

Almanac is a personal, single-author fork. This document exists to keep it from
sprawling, not to govern a community.

## Mission

Do three things well on constrained hardware:

1. **Read.** EPUB rendering that is legible, fast, and does not run out of memory.
2. **Look up.** Show what is flying overhead, on demand.
3. **Pass the time.** A small turn-based diversion for when you are waiting and
   not reading.

Everything else is out of scope until it demonstrably serves one of those.

The third pillar is the newest and the easiest to abuse, so it carries its own
bar — see [Diversions](#diversions).

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
- **Diversions.** Turn-based puzzles that clear the bar below.
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
- **Interactive apps** — calculators, notepads. Games are no longer blanket-out,
  but only under the [Diversions](#diversions) bar below.
- **Authoring tools.** The input hardware and the RAM are both wrong for it.
- **General-purpose network features** — RSS, browsers, aggregators. The flight
  tracker is the one network feature, and it earns its place by being the
  reason this fork exists.

## Diversions

Opening the door to games is the widest thing this document has ever done, so
the door has a frame. A diversion is in scope only if **all four** hold:

1. **Turn-based.** One user action, one screen update. Anything assuming a frame
   rate is out — the panel refreshes in 770–1720 ms and FAST is a ghosting
   differential waveform.
2. **Playable on the buttons we have.** Seven physical buttons, four of them
   directional. If it needs a keypad or typed input, it does not fit — that is
   what rules out text adventures despite their being a natural e-reader match.
3. **No network.** Ever. Not even optional.
4. **No steady-state RAM.** State measured in bytes, no heap allocation, nothing
   retained while you are reading.

This is why **emulators are permanently out**, and the reasoning is worth
keeping rather than re-deriving. NES- and Game Boy-class emulators need PSRAM
that the ESP32-C3 does not have and a frame rate the panel cannot produce.
Chip-8 is the near miss: it fits in 4 KB and is natively 1-bit monochrome, but
it wants a 16-key hex keypad and real-time play, so it fails bars 1 and 2.

Flash is not the limiting factor here and never was — the constraints are RAM,
display physics, and input.

Current diversions: 2048
([design](docs/superpowers/specs/2026-08-09-2048-game-design.md)).

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

1. Does it make reading better, answer "what is that plane", or clear the
   [Diversions](#diversions) bar?
2. What does it cost in RAM at peak, and in flash?
3. Does it work in all four orientations and across every theme?
4. Can it fail without taking the device or a book's progress with it?

If 1 is "no", stop. If 2 is unknown, measure before writing the feature.

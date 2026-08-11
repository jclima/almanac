# Mini Dashboard — Design Spec

Date: 2026-08-09
Status: Approved
Scope: Host-side tooling only. **No firmware change.**

## Goal

Produce a daily one-glance page — weather, news headlines, sun/moon times, and
aircraft overhead — and get it onto the device as an EPUB the reader already
knows how to render.

The whole feature lives in `scripts/`. The firmware is not touched: no new
activity, no new network client, no RAM cost, no flash cost.

## Why it is built this way

[SCOPE.md:46](../../../SCOPE.md) lists "General-purpose network features — RSS,
news, aggregators" as out of scope, and [SCOPE.md:38](../../../SCOPE.md) rules
out background connectivity. An in-firmware dashboard would contradict both, and
would spend RAM and flash (the app partition is already ~85% full) on something
that fails the first question in SCOPE.md's own test: it does not make reading
better, and it does not answer "what is that plane".

Generating the page on the host and delivering it as an EPUB satisfies the
request without any of that cost. It is also the answer to gate 4 of the
`scope-discipline` skill — *can it be done with no new code?* — since both halves
already exist:

- `scripts/generate_userguide_epub.py` builds an EPUB on the host with
  `ebooklib` and a PIL-generated cover.
- `POST /upload` ([docs/webserver-endpoints.md:102](../../webserver-endpoints.md))
  pushes a file to the SD card over Wi-Fi.

**No SCOPE.md amendment is required.** This adds host tooling, not a firmware
network feature.

## Context: what already ships

| Existing thing | How this reuses it |
|---|---|
| `scripts/generate_*_epub.py` family | Same `ebooklib` + PIL cover approach, same `scripts/` home |
| `POST /upload?path=…` | Delivery mechanism, unchanged |
| `clearBookCache` at [AlmanacWebServer.cpp:1148](../../../src/network/AlmanacWebServer.cpp) | Deleting the old copy forces a re-parse, so the render is never stale |
| `POST /delete` ([docs/webserver-endpoints.md:179](../../webserver-endpoints.md)) | Clears the previous copy, which the device would otherwise refuse to overwrite |
| `OpenSkyClient::fetchNearby` at [OpenSkyClient.cpp:16](../../../src/network/OpenSkyClient.cpp) | The script builds the same bounding-box URL, so the page and Nearby Flights agree |
| `.gitignore` `*.local*` rule ([.gitignore:19](../../../.gitignore)) | Personal config is gitignored automatically, same spirit as `platformio.local.ini` |

## Decisions

| Question | Decision |
|---|---|
| Where it runs | Host (macOS), invoked manually as `python3 scripts/generate_dashboard_epub.py` |
| Firmware change | None |
| File layout | A package, `scripts/dashboard/`, not a flat script — see below |
| Config | `scripts/dashboard.config.local.json`, gitignored; `dashboard.config.example.json` committed |
| Weather source | Open-Meteo `/v1/forecast` — free, no API key |
| News source | RSS/Atom feeds named in config, via `feedparser`. Titles and sources only |
| Sun times | Open-Meteo `daily` block (same request as weather) |
| Moon phase | Computed locally, synodic-month formula, no network |
| Flights | OpenSky `states/all`, anonymous, same bounding box as firmware |
| Output | One EPUB, four XHTML sections, TOC, PIL cover showing the date |
| Filename | Fixed `Dashboard.epub` every run |
| Delivery | `POST /upload`; falls back to a local save if the device is unreachable |
| Tests | `pytest` over the pure transforms, fixture payloads, no network |

### File layout

Every other generator in `scripts/` is a flat single file. This one is a package
because it has four independent network sources plus rendering plus delivery; as
one file it lands near 500 lines and each source's failure modes tangle with the
renderer.

| Module | Purpose | Depends on |
|---|---|---|
| `config.py` | Load and validate JSON config, apply defaults | stdlib |
| `geo.py` | Great-circle math mirroring `lib/Geo/GeoMath.cpp`. Pure | stdlib |
| `sources.py` | Four fetchers → four dataclasses, each failing independently | `urllib`, `feedparser` |
| `render.py` | Dataclasses → XHTML strings. Pure — no network, no I/O | stdlib |
| `deliver.py` | Package the EPUB, POST it, or save locally | `ebooklib`, PIL, `urllib` |
| `__main__.py` | Wire together, CLI flags, exit codes | the above |

The entry point is a flat `scripts/generate_dashboard_epub.py`, matching the
existing `generate_*_epub.py` family. It puts `scripts/` on `sys.path` and calls
into the package, so `scripts/` never becomes a Python package — `platformio.ini`
loads five build hooks from that directory, and this keeps them untouched.

`render.py` being pure is what makes the test suite possible without network
access or recorded HTTP traffic.

## Config file

```json
{
  "location": { "name": "Lisbon", "lat": 38.7223, "lon": -9.1393 },
  "units": { "temperature": "celsius", "wind": "kmh" },
  "news": {
    "max_per_feed": 5,
    "feeds": [{ "name": "BBC World", "url": "https://feeds.bbci.co.uk/news/world/rss.xml" }]
  },
  "flights": { "radius_miles": 25 },
  "device": { "host": "almanac.local", "path": "/Books" }
}
```

`location` is set once and feeds weather, sun times, and the flight bounding box.
`units` is passed through to Open-Meteo. `device.host` accepts an IP address for
networks where mDNS does not resolve `almanac.local`.

Missing keys take defaults; an unparseable config, or a `location` without
coordinates, is a hard error with the offending key named. If the config file is
absent entirely, the error says to copy `dashboard.config.example.json` and gives
the target path. There is no interactive setup — the example file is the
documentation.

## Data flow

```text
config.json ──┬─> weather()  ─> WeatherReport ─┐
              ├─> news()     ─> NewsDigest    ─┤
              ├─> sky()      ─> SkyReport     ─┼─> render() ─> 4 × XHTML
              └─> flights()  ─> FlightSnapshot ┘                   │
                                                                   v
                                                    deliver() ─> Dashboard.epub
                                                                   │
                                            POST /upload ──────────┤
                                            local save ────────────┘
```

Sources are independent. `render()` receives whatever succeeded plus whatever
failed, and always produces a complete page.

## Error handling

Each fetcher returns its dataclass **or** an error string. Nothing a source does
can abort the run.

| Failure | Behaviour |
|---|---|
| A source fails (timeout, HTTP error, bad payload) | Its section renders `unavailable — <reason>`; other sections are unaffected |
| A single RSS feed fails | That feed's block shows the error; other feeds still render |
| Every source fails | The EPUB still builds and still uploads, with four unavailable sections and the timestamp. Exit code 1 signals empty content, not a failed run |
| Device not in File Transfer mode | EPUB saved locally, path and exact `curl` line printed, **exit code 0** — not reachable is not an error |
| Config missing or invalid | Hard error before any network call, naming the offending key. Exit code 2 |
| Upload rejected by the device (non-2xx) | Error reported with the device's response, local copy kept. Exit code 3 |

Every page carries the generation timestamp in its header, so a stale page is
visibly stale rather than quietly wrong.

## EPUB structure

Four XHTML documents — Weather, News, Sky, Flights — with a TOC. One document
per section rather than one long scroll, so section boundaries land on page
breaks; that reads better on e-ink than a section starting mid-page.

A minimal PIL-generated cover carries the date, so the Home tile shows which day
is loaded.

The filename is fixed, and the tool **deletes the previous copy before uploading**.

This was originally specified as a plain overwrite, on the strength of
`clearBookCache` running after upload. That was wrong, and a code review caught
it before any hardware test: the device refuses an upload whose target already
exists ([AlmanacWebServer.cpp:714](../../../src/network/AlmanacWebServer.cpp)
sets an error that `handleUploadPost` turns into HTTP 400), so the first run
would have succeeded and every run after it failed. The device's own File
Manager sidesteps this by renaming client-side, which is why nothing in the
repository exercises the overwrite path.

Deleting first is host-side only and needs no firmware change. It also fires
`clearBookCache` ([AlmanacWebServer.cpp:1148](../../../src/network/AlmanacWebServer.cpp)),
so the re-parse and the reset to page 1 still happen — both correct for a page
replaced daily.

## Workflow

1. Put the device into File Transfer mode.
2. Run `python3 scripts/generate_dashboard_epub.py`.
3. Open `Dashboard.epub` on the device.

If step 1 is skipped, step 2 still produces the file and tells you how to push it
later.

### CLI flags

| Flag | Effect |
|---|---|
| *(none)* | Fetch, build, upload |
| `--no-upload` | Build only, save locally |
| `--out PATH` | Override the local output path |
| `--config PATH` | Override the config file location |

## Dependencies

Added to `scripts/requirements.txt`:

- `feedparser` — RSS/Atom parsing (new)
- `ebooklib` — EPUB packaging (already imported by `generate_userguide_epub.py`
  but undeclared; adding it fixes an existing gap)
- `markdown` — same undeclared-dependency situation

HTTP uses stdlib `urllib.request`, including the multipart body for `/upload`.
No HTTP client dependency is added.

## Testing

A `pytest` suite over the pure functions — feed parsing, moon phase, XHTML
rendering, config defaults and validation — against recorded fixture payloads.
No network access in tests.

This is the repository's first Python test suite; it does not touch the existing
`test/` gtest tree or the `pio run -t unit-tests` target.

Not covered by tests, and verified by hand:

- Actual upload to a device in File Transfer mode
- Rendering on the X4 across orientations and themes

## Out of scope

- Article bodies. Titles and sources only — bodies make it an aggregator and
  stop it being *mini*.
- Scheduling. No launchd job; the script is run when wanted.
- Any firmware change, including a Home-screen entry point for the file.
- Non-EPUB output formats.

# Mini Dashboard

A one-glance page — weather, news headlines, sun and moon times, and aircraft
overhead — generated on your computer and read on the device as an EPUB.

This is host-side tooling. No firmware feature, no RAM cost, no background
connectivity: see [SCOPE.md](../SCOPE.md).

## Setup

Install the dependencies once:

```bash
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
```

Copy the example config and edit it:

```bash
cp scripts/dashboard.config.example.json scripts/dashboard.config.local.json
```

| Key | Meaning |
|---|---|
| `location.name` | Shown on the page and the cover |
| `location.lat` / `location.lon` | Used for weather, sun times, and the flight search |
| `units.temperature` | `celsius` or `fahrenheit` |
| `units.wind` | `kmh`, `mph`, `ms`, or `kn` |
| `news.feeds` | RSS or Atom feeds — `name` and `url` per entry |
| `news.max_per_feed` | Headlines kept per feed (default 5) |
| `flights.radius_miles` | Search radius around your location (default 25) |
| `device.host` | `almanac.local`, or the IP shown on screen if mDNS does not resolve |
| `device.path` | Directory on the SD card (default `/Books`) |

The `.local.json` file is gitignored — your location and feeds stay out of the
repository.

## Daily use

1. On the device, open **File Transfer** mode.
2. Run:

   ```bash
   .venv/bin/python scripts/generate_dashboard_epub.py
   ```

3. Open **Dashboard.epub** on the device.

If the device is not reachable the EPUB is still built and the command prints
the exact `curl` line to push it later.

### Options

| Flag | Effect |
|---|---|
| `--no-upload` | Build only; leave the file in `build/` |
| `--out PATH` | Write the EPUB somewhere else |
| `--config PATH` | Use a different config file |

## Notes

- The filename is always `Dashboard.epub`. The device refuses an upload when the
  target already exists, so the tool deletes the previous copy first — which
  also clears that book's cache, so the new page renders instead of yesterday's,
  and you start back at page 1.
- Any section that fails to fetch renders as "unavailable" with the reason; the
  rest of the page is unaffected.
- Anonymous OpenSky access is rate-limited, so the Flights section is the one
  most likely to be empty. That is expected.
- Every page carries the generation time, so a stale page is visibly stale.

## Tests

```bash
.venv/bin/pytest -v
```

No network access is required — the suite runs against fixture payloads.

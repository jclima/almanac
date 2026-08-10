# Mini Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A host-side Python tool that fetches weather, news headlines, sun/moon times, and aircraft overhead, renders them as an EPUB, and uploads it to the device.

**Architecture:** A package at `scripts/dashboard/` with one responsibility per module — config loading, pure geo math, network sources, pure XHTML rendering, and EPUB packaging plus delivery. Sources fail independently and never abort the run; rendering is a pure function of whatever succeeded. **No firmware code is touched.**

**Tech Stack:** Python 3.10+, `ebooklib` (EPUB), `feedparser` (RSS/Atom), `Pillow` (cover), stdlib `urllib` (all HTTP), `pytest`.

**Spec:** [2026-08-09-mini-dashboard-design.md](../specs/2026-08-09-mini-dashboard-design.md)

## Global Constraints

- **No firmware change.** Nothing under `src/`, `lib/`, `freeink-sdk/`, or `platformio.ini` is modified by any task. If a task seems to need one, stop and raise it.
- **Python 3.10+** — `X | None` union syntax is used throughout.
- **No HTTP client dependency.** All HTTP uses stdlib `urllib.request`, including the multipart upload body.
- **Every network call passes `timeout=15`.** A hung source must not hang the run.
- **All fetchers return `Fetched[T]` and never raise.** A source failure is data, not an exception.
- **`render.py` and `geo.py` are pure** — no network, no file I/O, no clock reads. Every value they need is passed in. This is what makes the test suite runnable offline.
- **All text interpolated into XHTML goes through `html.escape()`.** Feed titles contain `&` and `<`.
- **Work happens on the current branch** (`claude/mini-dashboard-weather-news-ab4f45`). Never push to `origin` — that remote is upstream CrossPoint. See CLAUDE.md.
- **Commit messages** use the project's `<type>: <summary>` form (`feat:`, `test:`, `docs:`, `chore:`).

## Environment setup

macOS system Python is externally managed, so install into a venv. Do this once before Task 1:

```bash
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
```

`/.venv` is already gitignored ([.gitignore:20](../../../.gitignore)). **Every command in this plan uses `.venv/bin/python` and `.venv/bin/pytest`.**

## File structure

| File | Responsibility | Created in |
|---|---|---|
| `scripts/generate_dashboard_epub.py` | Flat entry point, matching the `generate_*_epub.py` family. Puts `scripts/` on `sys.path` and calls `dashboard.__main__.main()` | Task 1 |
| `pytest.ini` | Puts `scripts/` on the test path so tests import `dashboard.*` | Task 1 |
| `scripts/dashboard/__init__.py` | Package marker. Empty. | Task 1 |
| `scripts/dashboard/config.py` | Load, validate, and default the JSON config | Task 1 |
| `scripts/dashboard/geo.py` | Pure great-circle math, mirroring `lib/Geo/GeoMath.cpp` | Task 2 |
| `scripts/dashboard/sources.py` | `Fetched`, the four section dataclasses, four fetchers, moon phase | Tasks 3–5 |
| `scripts/dashboard/render.py` | Dataclasses → XHTML fragments. Pure. | Task 6 |
| `scripts/dashboard/deliver.py` | Cover PNG, EPUB packaging, upload | Tasks 7–8 |
| `scripts/dashboard/__main__.py` | CLI, wiring, exit codes | Task 8 |
| `scripts/dashboard/tests/` | pytest suite, fixtures inline as string/dict literals | Tasks 1–7 |
| `scripts/dashboard.config.example.json` | Committed example config | Task 1 |
| `scripts/requirements.txt` | Modified: add `ebooklib`, `markdown`, `feedparser`, `pytest` | Task 1 |
| `docs/mini-dashboard.md` | User-facing setup and usage | Task 8 |

**Deviation from the spec, applied in Task 2:** the spec's module table lists five modules. Implementation adds a sixth, `geo.py`, so the pure great-circle helpers are testable without importing the network module. Task 2 updates the spec table to match.

**Why a flat launcher instead of `python -m scripts.dashboard`:** running the package that way needs a `scripts/__init__.py`, which would make `scripts` a Python package. [platformio.ini:118](../../../platformio.ini) and `:225` load five build hooks from that same directory by path (`pre:scripts/build_html.py` and friends). SCons exec's those rather than importing them, so an `__init__.py` is almost certainly inert — but "almost certainly" is not worth a build-system regression on a plan whose first constraint is *no firmware change*. The flat launcher removes the question entirely and matches how every other script in `scripts/` is invoked.

---

### Task 1: Package skeleton, dependencies, and config loading

**Files:**
- Create: `pytest.ini`
- Create: `scripts/dashboard/__init__.py`
- Create: `scripts/dashboard/config.py`
- Create: `scripts/dashboard/tests/__init__.py`
- Create: `scripts/dashboard/tests/test_config.py`
- Create: `scripts/dashboard.config.example.json`
- Modify: `scripts/requirements.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: `Config` (frozen dataclass) with fields `place: str`, `lat: float`, `lon: float`, `temperature_unit: str`, `wind_unit: str`, `max_per_feed: int`, `feeds: list[Feed]`, `radius_miles: float`, `device_host: str`, `device_path: str`. Also `Feed(name: str, url: str)`, `ConfigError(Exception)`, and `load_config(path: Path) -> Config`.

- [ ] **Step 1: Add the dependencies**

Append to `scripts/requirements.txt`. `ebooklib` and `markdown` are already imported by `scripts/generate_userguide_epub.py` but were never declared — adding them fixes that gap.

```text
ebooklib>=0.18
markdown>=3.5
feedparser>=6.0.11
pytest>=8.0
```

Then install:

```bash
.venv/bin/pip install -r requirements.txt
```

- [ ] **Step 2: Create the package markers and the pytest config**

`scripts/dashboard/__init__.py` and `scripts/dashboard/tests/__init__.py` are empty files. **Do not create `scripts/__init__.py`** — see the note above the task list.

```bash
touch scripts/dashboard/__init__.py scripts/dashboard/tests/__init__.py
```

Create `pytest.ini` at the repository root. `pythonpath` is what lets tests write `from dashboard.config import …` without `scripts` being a package (pytest 7+; `pytest>=8` is pinned in Step 1):

```ini
[pytest]
pythonpath = scripts
testpaths = scripts/dashboard/tests
```

- [ ] **Step 3: Write the failing tests**

Create `scripts/dashboard/tests/test_config.py`:

```python
import json
from pathlib import Path

import pytest

from dashboard.config import ConfigError, load_config

MINIMAL = {"location": {"name": "Lisbon", "lat": 38.7223, "lon": -9.1393}}


def write(tmp_path: Path, payload) -> Path:
    path = tmp_path / "dashboard.config.local.json"
    path.write_text(json.dumps(payload), encoding="utf-8")
    return path


def test_minimal_config_applies_defaults(tmp_path):
    cfg = load_config(write(tmp_path, MINIMAL))
    assert cfg.place == "Lisbon"
    assert cfg.lat == 38.7223
    assert cfg.lon == -9.1393
    assert cfg.temperature_unit == "celsius"
    assert cfg.wind_unit == "kmh"
    assert cfg.max_per_feed == 5
    assert cfg.feeds == []
    assert cfg.radius_miles == 25.0
    assert cfg.device_host == "almanac.local"
    assert cfg.device_path == "/Books"


def test_feeds_are_parsed(tmp_path):
    payload = dict(MINIMAL)
    payload["news"] = {"max_per_feed": 3, "feeds": [{"name": "BBC", "url": "https://x/rss"}]}
    cfg = load_config(write(tmp_path, payload))
    assert cfg.max_per_feed == 3
    assert len(cfg.feeds) == 1
    assert cfg.feeds[0].name == "BBC"
    assert cfg.feeds[0].url == "https://x/rss"


def test_missing_file_names_the_example(tmp_path):
    with pytest.raises(ConfigError) as exc:
        load_config(tmp_path / "nope.json")
    assert "dashboard.config.example.json" in str(exc.value)


def test_invalid_json_is_a_config_error(tmp_path):
    path = tmp_path / "bad.json"
    path.write_text("{not json", encoding="utf-8")
    with pytest.raises(ConfigError):
        load_config(path)


def test_missing_coordinates_names_the_key(tmp_path):
    with pytest.raises(ConfigError) as exc:
        load_config(write(tmp_path, {"location": {"name": "Lisbon"}}))
    assert "location.lat" in str(exc.value)


def test_non_numeric_coordinate_is_rejected(tmp_path):
    payload = {"location": {"name": "L", "lat": "north", "lon": 0}}
    with pytest.raises(ConfigError) as exc:
        load_config(write(tmp_path, payload))
    assert "location.lat" in str(exc.value)


def test_feed_without_url_names_the_key(tmp_path):
    payload = dict(MINIMAL)
    payload["news"] = {"feeds": [{"name": "BBC"}]}
    with pytest.raises(ConfigError) as exc:
        load_config(write(tmp_path, payload))
    assert "news.feeds[0].url" in str(exc.value)


def test_non_object_section_is_rejected(tmp_path):
    payload = dict(MINIMAL)
    payload["units"] = "celsius"
    with pytest.raises(ConfigError) as exc:
        load_config(write(tmp_path, payload))
    assert "units" in str(exc.value)


def test_non_numeric_max_per_feed_is_rejected(tmp_path):
    payload = dict(MINIMAL)
    payload["news"] = {"max_per_feed": "five"}
    with pytest.raises(ConfigError) as exc:
        load_config(write(tmp_path, payload))
    assert "news.max_per_feed" in str(exc.value)


def test_zero_max_per_feed_is_honoured(tmp_path):
    payload = dict(MINIMAL)
    payload["news"] = {"max_per_feed": 0}
    assert load_config(write(tmp_path, payload)).max_per_feed == 0


def test_non_numeric_radius_is_rejected(tmp_path):
    payload = dict(MINIMAL)
    payload["flights"] = {"radius_miles": "25 miles"}
    with pytest.raises(ConfigError) as exc:
        load_config(write(tmp_path, payload))
    assert "flights.radius_miles" in str(exc.value)


def test_non_positive_radius_is_rejected(tmp_path):
    payload = dict(MINIMAL)
    payload["flights"] = {"radius_miles": 0}
    with pytest.raises(ConfigError) as exc:
        load_config(write(tmp_path, payload))
    assert "flights.radius_miles" in str(exc.value)


def test_non_list_feeds_is_rejected(tmp_path):
    payload = dict(MINIMAL)
    payload["news"] = {"feeds": {"name": "BBC", "url": "https://x/rss"}}
    with pytest.raises(ConfigError) as exc:
        load_config(write(tmp_path, payload))
    assert "news.feeds" in str(exc.value)
```

Every section that can appear in the config is now type-checked, so `load_config` raises `ConfigError` naming the offending key for any invalid input. Task 8's CLI catches exactly that and maps it to exit code 2.

- [ ] **Step 4: Run the tests to verify they fail**

Run: `.venv/bin/pytest scripts/dashboard/tests/test_config.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'dashboard.config'`

- [ ] **Step 5: Write the implementation**

Create `scripts/dashboard/config.py`:

```python
"""Load and validate the dashboard config file.

The config is the only place personal data lives (location, feed URLs, device
host). It is gitignored via the `*.local*` rule; the committed example file is
the documentation.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

EXAMPLE_NAME = "dashboard.config.example.json"

DEFAULT_TEMPERATURE_UNIT = "celsius"
DEFAULT_WIND_UNIT = "kmh"
DEFAULT_MAX_PER_FEED = 5
DEFAULT_RADIUS_MILES = 25.0
DEFAULT_DEVICE_HOST = "almanac.local"
DEFAULT_DEVICE_PATH = "/Books"


class ConfigError(Exception):
    """Raised for a missing, unparseable, or invalid config file."""


@dataclass(frozen=True)
class Feed:
    name: str
    url: str


@dataclass(frozen=True)
class Config:
    place: str
    lat: float
    lon: float
    temperature_unit: str = DEFAULT_TEMPERATURE_UNIT
    wind_unit: str = DEFAULT_WIND_UNIT
    max_per_feed: int = DEFAULT_MAX_PER_FEED
    feeds: list[Feed] = field(default_factory=list)
    radius_miles: float = DEFAULT_RADIUS_MILES
    device_host: str = DEFAULT_DEVICE_HOST
    device_path: str = DEFAULT_DEVICE_PATH


def _require_number(section: dict, key: str, path: str) -> float:
    value = section.get(key)
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise ConfigError(f"{path} must be a number (got {value!r})")
    return float(value)


def _require_object(raw: dict, key: str) -> dict:
    """Return raw[key] as a dict. Absent or null yields {}; a wrong type is an error."""
    value = raw.get(key)
    if value is None:
        return {}
    if not isinstance(value, dict):
        raise ConfigError(f"{key} must be an object (got {value!r})")
    return value


def load_config(path: Path) -> Config:
    """Read `path` and return a validated Config, or raise ConfigError."""
    if not path.is_file():
        raise ConfigError(
            f"no config at {path}. Copy scripts/{EXAMPLE_NAME} to {path} and edit it."
        )

    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ConfigError(f"{path} is not valid JSON: {exc}") from exc

    if not isinstance(raw, dict):
        raise ConfigError(f"{path} must contain a JSON object")

    location = raw.get("location")
    if not isinstance(location, dict):
        raise ConfigError("location is required and must be an object")

    lat = _require_number(location, "lat", "location.lat")
    lon = _require_number(location, "lon", "location.lon")
    place = str(location.get("name") or f"{lat:.4f}, {lon:.4f}")

    units = _require_object(raw, "units")
    news = _require_object(raw, "news")
    flights = _require_object(raw, "flights")
    device = _require_object(raw, "device")

    raw_feeds = news.get("feeds")
    if raw_feeds is not None and not isinstance(raw_feeds, list):
        raise ConfigError(f"news.feeds must be a list (got {raw_feeds!r})")

    feeds: list[Feed] = []
    for index, entry in enumerate(raw_feeds or []):
        if not isinstance(entry, dict) or not entry.get("url"):
            raise ConfigError(f"news.feeds[{index}].url is required")
        feeds.append(Feed(name=str(entry.get("name") or entry["url"]), url=str(entry["url"])))

    max_per_feed_raw = news.get("max_per_feed")
    if max_per_feed_raw is None:
        max_per_feed = DEFAULT_MAX_PER_FEED
    else:
        if (
            not isinstance(max_per_feed_raw, int)
            or isinstance(max_per_feed_raw, bool)
            or max_per_feed_raw < 0
        ):
            raise ConfigError(
                f"news.max_per_feed must be a non-negative integer (got {max_per_feed_raw!r})"
            )
        max_per_feed = max_per_feed_raw

    radius_miles_raw = flights.get("radius_miles")
    if radius_miles_raw is None:
        radius_miles = DEFAULT_RADIUS_MILES
    else:
        radius_miles = _require_number(flights, "radius_miles", "flights.radius_miles")
        if radius_miles <= 0:
            raise ConfigError(
                f"flights.radius_miles must be greater than 0 (got {radius_miles_raw!r})"
            )

    return Config(
        place=place,
        lat=lat,
        lon=lon,
        temperature_unit=str(units.get("temperature") or DEFAULT_TEMPERATURE_UNIT),
        wind_unit=str(units.get("wind") or DEFAULT_WIND_UNIT),
        max_per_feed=max_per_feed,
        feeds=feeds,
        radius_miles=radius_miles,
        device_host=str(device.get("host") or DEFAULT_DEVICE_HOST),
        device_path=str(device.get("path") or DEFAULT_DEVICE_PATH),
    )
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `.venv/bin/pytest scripts/dashboard/tests/test_config.py -v`
Expected: PASS, 13 tests

- [ ] **Step 7: Create the example config**

Create `scripts/dashboard.config.example.json`:

```json
{
  "location": { "name": "Lisbon", "lat": 38.7223, "lon": -9.1393 },
  "units": { "temperature": "celsius", "wind": "kmh" },
  "news": {
    "max_per_feed": 5,
    "feeds": [
      { "name": "BBC World", "url": "https://feeds.bbci.co.uk/news/world/rss.xml" }
    ]
  },
  "flights": { "radius_miles": 25 },
  "device": { "host": "almanac.local", "path": "/Books" }
}
```

- [ ] **Step 8: Verify the personal config would be gitignored**

Run:

```bash
cp scripts/dashboard.config.example.json scripts/dashboard.config.local.json && git status --short scripts/
```

Expected: `scripts/dashboard.config.local.json` does **not** appear (matched by the `*.local*` rule). Keep the copy — later tasks use it.

- [ ] **Step 9: Commit**

```bash
git add pytest.ini scripts/dashboard/ scripts/dashboard.config.example.json scripts/requirements.txt
git commit -m "feat: add dashboard package skeleton and config loading"
```

- [ ] **Step 10: Confirm the PlatformIO build hooks are untouched**

Nothing in this task creates `scripts/__init__.py`, so the five `pre:`/`post:` hooks at [platformio.ini:118](../../../platformio.ini) are unaffected. Verify no stray file appeared:

```bash
test ! -e scripts/__init__.py && echo "ok: scripts is not a package"
```

Expected: `ok: scripts is not a package`

---

### Task 2: Pure geo helpers

**Files:**
- Create: `scripts/dashboard/geo.py`
- Create: `scripts/dashboard/tests/test_geo.py`
- Modify: `docs/superpowers/specs/2026-08-09-mini-dashboard-design.md` (module table)

**Interfaces:**
- Consumes: nothing.
- Produces: `BoundingBox(lat_min, lon_min, lat_max, lon_max)` (frozen dataclass of floats), `distance_miles(lat1, lon1, lat2, lon2) -> float`, `initial_bearing_degrees(lat1, lon1, lat2, lon2) -> float`, `compass_point(bearing_deg: float) -> str`, `bounding_box(lat, lon, radius_miles) -> BoundingBox`.

These mirror `lib/Geo/GeoMath.cpp` exactly — same constants, same formulas — so the generated page and the firmware's Nearby Flights view agree about which aircraft are in range.

- [ ] **Step 1: Write the failing tests**

Create `scripts/dashboard/tests/test_geo.py`:

```python
import pytest

from dashboard.geo import (
    bounding_box,
    compass_point,
    distance_miles,
    initial_bearing_degrees,
)


def test_distance_to_self_is_zero():
    assert distance_miles(38.7223, -9.1393, 38.7223, -9.1393) == pytest.approx(0.0, abs=1e-9)


def test_london_to_paris():
    # Great-circle London -> Paris is ~213.7 miles.
    d = distance_miles(51.5074, -0.1278, 48.8566, 2.3522)
    assert d == pytest.approx(213.7, abs=2.0)


def test_bearing_due_north():
    assert initial_bearing_degrees(0.0, 0.0, 1.0, 0.0) == pytest.approx(0.0, abs=1e-6)


def test_bearing_due_east():
    assert initial_bearing_degrees(0.0, 0.0, 0.0, 1.0) == pytest.approx(90.0, abs=1e-6)


@pytest.mark.parametrize(
    "bearing,expected",
    [
        (0.0, "N"),
        (22.4, "N"),
        (22.6, "NE"),
        (45.0, "NE"),
        (90.0, "E"),
        (180.0, "S"),
        (270.0, "W"),
        (350.0, "N"),
        (-10.0, "N"),
        (720.0, "N"),
    ],
)
def test_compass_point(bearing, expected):
    assert compass_point(bearing) == expected


def test_bounding_box_at_the_equator():
    # 69 miles per degree of latitude, and cos(0) == 1, so both spans are 1 degree.
    box = bounding_box(0.0, 0.0, 69.0)
    assert box.lat_min == pytest.approx(-1.0)
    assert box.lat_max == pytest.approx(1.0)
    assert box.lon_min == pytest.approx(-1.0)
    assert box.lon_max == pytest.approx(1.0)


def test_bounding_box_longitude_span_widens_with_latitude():
    equator = bounding_box(0.0, 0.0, 25.0)
    north = bounding_box(60.0, 0.0, 25.0)
    assert (north.lon_max - north.lon_min) > (equator.lon_max - equator.lon_min)


def test_bounding_box_guards_near_the_pole():
    # The clamp fires only when 69*cos(lat) < 1, i.e. above ~89.17 degrees.
    # There, lon_span = 25/1.0, so the full span is 50 degrees.
    box = bounding_box(89.9999, 0.0, 25.0)
    assert box.lon_max - box.lon_min == pytest.approx(50.0)


def test_bounding_box_does_not_clamp_below_the_guard_latitude():
    # Proves the previous test is exercising the clamp rather than passing by
    # accident: at 80 degrees, 69*cos(80) is ~11.98, well above the 1.0 floor.
    box = bounding_box(80.0, 0.0, 25.0)
    assert box.lon_max - box.lon_min == pytest.approx(4.174, abs=0.01)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv/bin/pytest scripts/dashboard/tests/test_geo.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'dashboard.geo'`

- [ ] **Step 3: Write the implementation**

Create `scripts/dashboard/geo.py`:

```python
"""Great-circle helpers, mirroring lib/Geo/GeoMath.cpp.

Same constants and same formulas as the firmware, so the generated Flights
section and the on-device Nearby Flights view agree about what is in range.
Pure: no network, no I/O.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

EARTH_RADIUS_MILES = 3958.7613
MILES_PER_DEGREE_LAT = 69.0

_COMPASS_POINTS = ("N", "NE", "E", "SE", "S", "SW", "W", "NW")


@dataclass(frozen=True)
class BoundingBox:
    lat_min: float
    lon_min: float
    lat_max: float
    lon_max: float


def _normalize_degrees(deg: float) -> float:
    d = math.fmod(deg, 360.0)
    return d + 360.0 if d < 0 else d


def distance_miles(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Great-circle distance between two WGS84 points, in miles."""
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    d_phi = math.radians(lat2 - lat1)
    d_lambda = math.radians(lon2 - lon1)

    a = math.sin(d_phi / 2) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(d_lambda / 2) ** 2
    c = 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))
    return EARTH_RADIUS_MILES * c


def initial_bearing_degrees(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Initial bearing from point 1 to point 2, degrees clockwise from north."""
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    d_lambda = math.radians(lon2 - lon1)

    y = math.sin(d_lambda) * math.cos(phi2)
    x = math.cos(phi1) * math.sin(phi2) - math.sin(phi1) * math.cos(phi2) * math.cos(d_lambda)
    return _normalize_degrees(math.degrees(math.atan2(y, x)))


def compass_point(bearing_deg: float) -> str:
    """Nearest of the 8 compass points. Any finite input is normalized first."""
    normalized = _normalize_degrees(bearing_deg)
    return _COMPASS_POINTS[int((normalized + 22.5) / 45.0) % 8]


def bounding_box(lat: float, lon: float, radius_miles: float) -> BoundingBox:
    """Axis-aligned box enclosing a radius around a point."""
    lat_span = radius_miles / MILES_PER_DEGREE_LAT

    miles_per_degree_lon = MILES_PER_DEGREE_LAT * math.cos(math.radians(lat))
    if miles_per_degree_lon < 1.0:
        miles_per_degree_lon = 1.0  # guard near the poles
    lon_span = radius_miles / miles_per_degree_lon

    return BoundingBox(lat - lat_span, lon - lon_span, lat + lat_span, lon + lon_span)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv/bin/pytest scripts/dashboard/tests/test_geo.py -v`
Expected: PASS, 18 tests (8 plain plus 10 parametrized compass cases)

- [ ] **Step 5: Update the spec's module table**

In `docs/superpowers/specs/2026-08-09-mini-dashboard-design.md`, in the "File layout" table, insert a row directly above the `sources.py` row:

```markdown
| `geo.py` | Great-circle math mirroring `lib/Geo/GeoMath.cpp`. Pure | stdlib |
```

- [ ] **Step 6: Commit**

```bash
git add scripts/dashboard/geo.py scripts/dashboard/tests/test_geo.py docs/superpowers/specs/2026-08-09-mini-dashboard-design.md
git commit -m "feat: add pure geo helpers mirroring GeoMath"
```

---

### Task 3: Weather and sky source

**Files:**
- Create: `scripts/dashboard/sources.py`
- Create: `scripts/dashboard/tests/test_sources_forecast.py`

**Interfaces:**
- Consumes: `Config` from Task 1.
- Produces:
  - `Fetched[T]` — frozen generic dataclass, fields `value: T | None = None`, `error: str | None = None`, property `ok -> bool` (True when `error is None and value is not None`).
  - `CurrentConditions(temperature: float, apparent_temperature: float, description: str, wind_speed: float, temperature_unit: str, wind_unit: str)`
  - `DayForecast(date: datetime.date, description: str, high: float, low: float, precipitation_chance: int, sunrise: datetime.datetime, sunset: datetime.datetime)`
  - `Forecast(place: str, current: CurrentConditions, days: list[DayForecast])`
  - `MoonPhase(fraction: float, name: str)`
  - `describe_weather_code(code: int) -> str`
  - `parse_forecast(payload: dict, place: str) -> Forecast` — pure, raises `ValueError` on a malformed payload
  - `moon_phase(day: datetime.date) -> MoonPhase` — pure, no network
  - `fetch_forecast(config: Config) -> Fetched[Forecast]` — never raises
  - `_get_json(url: str) -> dict` — shared private helper used by Tasks 4 and 5

**Both the Weather and Sky sections come from this one Forecast.** Open-Meteo returns sunrise and sunset in the same `daily` block as the temperatures, so a single request serves both. If it fails, both sections show unavailable — that is intended, not a bug.

- [ ] **Step 1: Write the failing tests**

Create `scripts/dashboard/tests/test_sources_forecast.py`:

```python
import datetime as dt

import pytest

from dashboard.sources import (
    Fetched,
    describe_weather_code,
    moon_phase,
    parse_forecast,
)

PAYLOAD = {
    "current": {
        "time": "2026-08-09T10:00",
        "temperature_2m": 24.3,
        "apparent_temperature": 25.1,
        "weather_code": 2,
        "wind_speed_10m": 12.4,
    },
    "current_units": {"temperature_2m": "°C", "wind_speed_10m": "km/h"},
    "daily": {
        "time": ["2026-08-09", "2026-08-10"],
        "weather_code": [2, 61],
        "temperature_2m_max": [28.1, 22.0],
        "temperature_2m_min": [18.0, 15.5],
        "precipitation_probability_max": [10, 80],
        "sunrise": ["2026-08-09T06:52", "2026-08-10T06:53"],
        "sunset": ["2026-08-09T20:31", "2026-08-10T20:30"],
    },
}


def test_fetched_ok_is_false_when_errored():
    assert Fetched(error="boom").ok is False
    assert Fetched(error="boom").value is None


def test_fetched_ok_is_true_with_a_value():
    assert Fetched(value=1).ok is True


def test_parse_forecast_reads_current_conditions():
    forecast = parse_forecast(PAYLOAD, "Lisbon")
    assert forecast.place == "Lisbon"
    assert forecast.current.temperature == 24.3
    assert forecast.current.apparent_temperature == 25.1
    assert forecast.current.description == "Partly cloudy"
    assert forecast.current.wind_speed == 12.4
    assert forecast.current.temperature_unit == "°C"
    assert forecast.current.wind_unit == "km/h"


def test_parse_forecast_reads_days():
    forecast = parse_forecast(PAYLOAD, "Lisbon")
    assert len(forecast.days) == 2
    today = forecast.days[0]
    assert today.date == dt.date(2026, 8, 9)
    assert today.high == 28.1
    assert today.low == 18.0
    assert today.precipitation_chance == 10
    assert today.description == "Partly cloudy"
    assert today.sunrise == dt.datetime(2026, 8, 9, 6, 52)
    assert today.sunset == dt.datetime(2026, 8, 9, 20, 31)
    assert forecast.days[1].description == "Slight rain"


def test_parse_forecast_rejects_a_payload_without_daily():
    with pytest.raises(ValueError):
        parse_forecast({"current": PAYLOAD["current"]}, "Lisbon")


def test_parse_forecast_tolerates_a_null_precipitation_value():
    payload = {**PAYLOAD, "daily": {**PAYLOAD["daily"], "precipitation_probability_max": [None, 80]}}
    forecast = parse_forecast(payload, "Lisbon")
    assert forecast.days[0].precipitation_chance == 0


def test_describe_weather_code_known_and_unknown():
    assert describe_weather_code(0) == "Clear sky"
    assert describe_weather_code(95) == "Thunderstorm"
    assert describe_weather_code(1234) == "Unknown"


def test_moon_phase_new_moon():
    # 2000-01-06 is the reference new moon.
    phase = moon_phase(dt.date(2000, 1, 6))
    assert phase.name == "New Moon"


def test_moon_phase_full_moon():
    # ~14.8 days later.
    phase = moon_phase(dt.date(2000, 1, 21))
    assert phase.name == "Full Moon"
    assert phase.fraction == pytest.approx(0.5, abs=0.03)


def test_moon_phase_fraction_stays_in_range():
    for offset in range(0, 60):
        phase = moon_phase(dt.date(2026, 1, 1) + dt.timedelta(days=offset))
        assert 0.0 <= phase.fraction < 1.0
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv/bin/pytest scripts/dashboard/tests/test_sources_forecast.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'dashboard.sources'`

- [ ] **Step 3: Write the implementation**

Create `scripts/dashboard/sources.py`:

```python
"""Data sources for the dashboard.

Every fetcher returns Fetched[T] and never raises: a source failing is data the
renderer displays, not an exception that aborts the run. Parsing is split out
into pure functions so it can be tested without network access.
"""

from __future__ import annotations

import datetime as dt
import json
import math
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Generic, TypeVar

from .config import Config

T = TypeVar("T")

HTTP_TIMEOUT_SECONDS = 15
USER_AGENT = "almanac-dashboard/1.0"

OPEN_METEO_URL = "https://api.open-meteo.com/v1/forecast"
FORECAST_DAYS = 4

# WMO 4677 codes, as used by Open-Meteo.
_WEATHER_CODES = {
    0: "Clear sky",
    1: "Mainly clear",
    2: "Partly cloudy",
    3: "Overcast",
    45: "Fog",
    48: "Rime fog",
    51: "Light drizzle",
    53: "Drizzle",
    55: "Heavy drizzle",
    56: "Freezing drizzle",
    57: "Heavy freezing drizzle",
    61: "Slight rain",
    63: "Rain",
    65: "Heavy rain",
    66: "Freezing rain",
    67: "Heavy freezing rain",
    71: "Slight snow",
    73: "Snow",
    75: "Heavy snow",
    77: "Snow grains",
    80: "Rain showers",
    81: "Heavy rain showers",
    82: "Violent rain showers",
    85: "Snow showers",
    86: "Heavy snow showers",
    95: "Thunderstorm",
    96: "Thunderstorm with hail",
    99: "Thunderstorm with heavy hail",
}

_MOON_NAMES = (
    "New Moon",
    "Waxing Crescent",
    "First Quarter",
    "Waxing Gibbous",
    "Full Moon",
    "Waning Gibbous",
    "Last Quarter",
    "Waning Crescent",
)
_SYNODIC_MONTH_DAYS = 29.530588853
_NEW_MOON_EPOCH = dt.datetime(2000, 1, 6, 18, 14, tzinfo=dt.timezone.utc)


@dataclass(frozen=True)
class Fetched(Generic[T]):
    """A section's data, or the reason it is missing."""

    value: T | None = None
    error: str | None = None

    @property
    def ok(self) -> bool:
        return self.error is None and self.value is not None


@dataclass(frozen=True)
class CurrentConditions:
    temperature: float
    apparent_temperature: float
    description: str
    wind_speed: float
    temperature_unit: str
    wind_unit: str


@dataclass(frozen=True)
class DayForecast:
    date: dt.date
    description: str
    high: float
    low: float
    precipitation_chance: int
    sunrise: dt.datetime
    sunset: dt.datetime


@dataclass(frozen=True)
class Forecast:
    place: str
    current: CurrentConditions
    days: list[DayForecast]


@dataclass(frozen=True)
class MoonPhase:
    fraction: float
    name: str


def _get_json(url: str) -> dict:
    """GET a URL and decode JSON. Raises on transport or decode failure."""
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=HTTP_TIMEOUT_SECONDS) as response:
        return json.loads(response.read().decode("utf-8"))


def describe_weather_code(code: int) -> str:
    return _WEATHER_CODES.get(code, "Unknown")


def moon_phase(day: dt.date) -> MoonPhase:
    """Moon phase for a date, from the synodic month. Pure — no network."""
    noon = dt.datetime(day.year, day.month, day.day, 12, tzinfo=dt.timezone.utc)
    days_since = (noon - _NEW_MOON_EPOCH).total_seconds() / 86400.0
    fraction = math.fmod(days_since / _SYNODIC_MONTH_DAYS, 1.0)
    if fraction < 0:
        fraction += 1.0
    name = _MOON_NAMES[int(fraction * 8 + 0.5) % 8]
    return MoonPhase(fraction=fraction, name=name)


def parse_forecast(payload: dict, place: str) -> Forecast:
    """Turn an Open-Meteo response into a Forecast. Pure. Raises ValueError."""
    current = payload.get("current")
    daily = payload.get("daily")
    if not isinstance(current, dict) or not isinstance(daily, dict):
        raise ValueError("response is missing 'current' or 'daily'")

    units = payload.get("current_units") or {}
    conditions = CurrentConditions(
        temperature=float(current["temperature_2m"]),
        apparent_temperature=float(current.get("apparent_temperature", current["temperature_2m"])),
        description=describe_weather_code(int(current.get("weather_code", -1))),
        wind_speed=float(current.get("wind_speed_10m", 0.0)),
        temperature_unit=str(units.get("temperature_2m", "")),
        wind_unit=str(units.get("wind_speed_10m", "")),
    )

    days: list[DayForecast] = []
    for index, iso_date in enumerate(daily["time"]):
        precipitation = daily.get("precipitation_probability_max", [])
        chance = precipitation[index] if index < len(precipitation) else None
        days.append(
            DayForecast(
                date=dt.date.fromisoformat(iso_date),
                description=describe_weather_code(int(daily["weather_code"][index])),
                high=float(daily["temperature_2m_max"][index]),
                low=float(daily["temperature_2m_min"][index]),
                precipitation_chance=int(chance) if chance is not None else 0,
                sunrise=dt.datetime.fromisoformat(daily["sunrise"][index]),
                sunset=dt.datetime.fromisoformat(daily["sunset"][index]),
            )
        )

    return Forecast(place=place, current=conditions, days=days)


def fetch_forecast(config: Config) -> Fetched[Forecast]:
    """Fetch weather and sun times in one request. Never raises."""
    url = (
        f"{OPEN_METEO_URL}?latitude={config.lat:.4f}&longitude={config.lon:.4f}"
        "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
        "precipitation_probability_max,sunrise,sunset"
        f"&timezone=auto&forecast_days={FORECAST_DAYS}"
        f"&temperature_unit={config.temperature_unit}&wind_speed_unit={config.wind_unit}"
    )
    try:
        return Fetched(value=parse_forecast(_get_json(url), config.place))
    except (urllib.error.URLError, OSError) as exc:
        return Fetched(error=f"could not reach Open-Meteo ({exc})")
    except (ValueError, KeyError, IndexError, TypeError) as exc:
        return Fetched(error=f"unexpected Open-Meteo response ({exc})")
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv/bin/pytest scripts/dashboard/tests/test_sources_forecast.py -v`
Expected: PASS, 11 tests

- [ ] **Step 5: Smoke-test the live fetch**

Run:

```bash
PYTHONPATH=scripts .venv/bin/python -c "
from pathlib import Path
from dashboard.config import load_config
from dashboard.sources import fetch_forecast
cfg = load_config(Path('scripts/dashboard.config.local.json'))
r = fetch_forecast(cfg)
print(r.error or r.value.current)
"
```

Expected: a `CurrentConditions(...)` line. If the network is down you get an error string instead — that is the designed behaviour, not a failure.

- [ ] **Step 6: Commit**

```bash
git add scripts/dashboard/sources.py scripts/dashboard/tests/test_sources_forecast.py
git commit -m "feat: add the weather and sky forecast source"
```

---

### Task 4: News source

**Files:**
- Modify: `scripts/dashboard/sources.py` (append)
- Create: `scripts/dashboard/tests/test_sources_news.py`

**Interfaces:**
- Consumes: `Fetched`, `Config`.
- Produces:
  - `Headline(title: str, published: datetime.datetime | None)`
  - `FeedResult(name: str, headlines: list[Headline], error: str | None = None)`
  - `NewsDigest(feeds: list[FeedResult])`
  - `parse_feed(name: str, raw_xml: str, limit: int) -> FeedResult` — pure
  - `fetch_news(config: Config) -> Fetched[NewsDigest]` — never raises

A single feed failing does not fail the section: its `FeedResult` carries the error and the others still render. The section as a whole only fails when no feeds are configured.

- [ ] **Step 1: Write the failing tests**

Create `scripts/dashboard/tests/test_sources_news.py`:

```python
from dashboard.sources import parse_feed

RSS = """<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0"><channel>
  <title>Example</title>
  <item>
    <title>First &amp; foremost</title>
    <pubDate>Sun, 09 Aug 2026 08:00:00 GMT</pubDate>
  </item>
  <item>
    <title>Second story</title>
    <pubDate>Sun, 09 Aug 2026 07:00:00 GMT</pubDate>
  </item>
  <item>
    <title>Third story</title>
  </item>
</channel></rss>
"""


def test_parse_feed_reads_titles():
    result = parse_feed("Example", RSS, limit=5)
    assert result.error is None
    assert [h.title for h in result.headlines] == ["First & foremost", "Second story", "Third story"]


def test_parse_feed_respects_the_limit():
    result = parse_feed("Example", RSS, limit=2)
    assert len(result.headlines) == 2


def test_parse_feed_reads_publication_dates():
    result = parse_feed("Example", RSS, limit=5)
    assert result.headlines[0].published is not None
    assert result.headlines[0].published.year == 2026
    assert result.headlines[2].published is None


def test_parse_feed_keeps_the_name():
    assert parse_feed("Example", RSS, limit=5).name == "Example"


def test_parse_feed_reports_an_empty_document_as_an_error():
    result = parse_feed("Example", "not a feed at all", limit=5)
    assert result.error is not None
    assert result.headlines == []


def test_parse_feed_reports_a_feed_with_no_items():
    empty = '<?xml version="1.0"?><rss version="2.0"><channel><title>x</title></channel></rss>'
    result = parse_feed("Example", empty, limit=5)
    assert result.error is not None
    assert "no items" in result.error
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv/bin/pytest scripts/dashboard/tests/test_sources_news.py -v`
Expected: FAIL — `ImportError: cannot import name 'parse_feed'`

- [ ] **Step 3: Write the implementation**

Add to the imports at the top of `scripts/dashboard/sources.py`:

```python
import io

import feedparser
```

Append to `scripts/dashboard/sources.py`:

```python
@dataclass(frozen=True)
class Headline:
    title: str
    published: dt.datetime | None


@dataclass(frozen=True)
class FeedResult:
    name: str
    headlines: list[Headline]
    error: str | None = None


@dataclass(frozen=True)
class NewsDigest:
    feeds: list[FeedResult]


def _entry_published(entry) -> dt.datetime | None:
    parsed = getattr(entry, "published_parsed", None) or getattr(entry, "updated_parsed", None)
    if not parsed:
        return None
    return dt.datetime(*parsed[:6], tzinfo=dt.timezone.utc)


def parse_feed(name: str, raw_xml: str, limit: int) -> FeedResult:
    """Turn feed XML into a FeedResult. Pure — no network. Never raises."""
    # Encode to bytes: feedparser treats some bare strings as locations and
    # warns about string input in 6.x.
    parsed = feedparser.parse(io.BytesIO(raw_xml.encode("utf-8")))
    entries = getattr(parsed, "entries", [])
    if not entries:
        reason = "no items" if not getattr(parsed, "bozo", False) else "could not be parsed"
        return FeedResult(name=name, headlines=[], error=reason)

    headlines = [
        Headline(title=str(entry.get("title", "")).strip(), published=_entry_published(entry))
        for entry in entries[:limit]
    ]
    return FeedResult(name=name, headlines=[h for h in headlines if h.title])


def _fetch_text(url: str) -> str:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=HTTP_TIMEOUT_SECONDS) as response:
        return response.read().decode("utf-8", errors="replace")


def fetch_news(config: Config) -> Fetched[NewsDigest]:
    """Fetch every configured feed. One feed failing does not fail the rest."""
    if not config.feeds:
        return Fetched(error="no feeds configured")

    results: list[FeedResult] = []
    for feed in config.feeds:
        try:
            results.append(parse_feed(feed.name, _fetch_text(feed.url), config.max_per_feed))
        except (urllib.error.URLError, OSError) as exc:
            results.append(FeedResult(name=feed.name, headlines=[], error=f"unreachable ({exc})"))

    return Fetched(value=NewsDigest(feeds=results))
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv/bin/pytest scripts/dashboard/tests/test_sources_news.py -v`
Expected: PASS, 6 tests

- [ ] **Step 5: Commit**

```bash
git add scripts/dashboard/sources.py scripts/dashboard/tests/test_sources_news.py
git commit -m "feat: add the news headline source"
```

---

### Task 5: Flights source

**Files:**
- Modify: `scripts/dashboard/sources.py` (append)
- Create: `scripts/dashboard/tests/test_sources_flights.py`

**Interfaces:**
- Consumes: `Fetched`, `Config`, and `bounding_box`, `distance_miles`, `initial_bearing_degrees`, `compass_point` from `geo`.
- Produces:
  - `Aircraft(callsign: str, origin_country: str, altitude_ft: int | None, speed_kts: int | None, distance_miles: float, bearing: str)`
  - `FlightSnapshot(place: str, radius_miles: float, aircraft: list[Aircraft])`
  - `parse_states(payload: dict, lat: float, lon: float, radius_miles: float, place: str = "") -> FlightSnapshot` — pure. `place` is threaded through rather than patched in afterwards, so the returned dataclass is always fully initialized.
  - `fetch_flights(config: Config) -> Fetched[FlightSnapshot]` — never raises

OpenSky's `states/all` returns positional arrays. Index meanings used here: 1 callsign, 2 origin country, 5 longitude, 6 latitude, 7 barometric altitude (metres), 8 on-ground flag, 9 velocity (m/s). Entries with no position are skipped; aircraft outside the radius are dropped (the bounding box is square, the radius is round). Anonymous OpenSky access is rate-limited, so this source failing is routine.

- [ ] **Step 1: Write the failing tests**

Create `scripts/dashboard/tests/test_sources_flights.py`:

```python
import pytest

from dashboard.sources import MAX_AIRCRAFT, parse_states

LAT, LON = 38.7223, -9.1393


def state(icao, callsign, lon, lat, altitude_m=10000.0, on_ground=False, velocity=231.5):
    return [
        icao, callsign, "Ireland", 1723199990, 1723199995,
        lon, lat, altitude_m, on_ground, velocity,
        45.2, 0.0, None, altitude_m, "1234", False, 0,
    ]


def test_parse_states_handles_a_null_state_list():
    snapshot = parse_states({"time": 1, "states": None}, LAT, LON, 25.0)
    assert snapshot.aircraft == []


def test_parse_states_handles_a_missing_state_key():
    snapshot = parse_states({"time": 1}, LAT, LON, 25.0)
    assert snapshot.aircraft == []


def test_parse_states_reads_a_nearby_aircraft():
    payload = {"states": [state("4ca7b4", "RYR4TL  ", LON + 0.05, LAT + 0.05)]}
    snapshot = parse_states(payload, LAT, LON, 25.0)
    assert len(snapshot.aircraft) == 1
    craft = snapshot.aircraft[0]
    assert craft.callsign == "RYR4TL"
    assert craft.origin_country == "Ireland"
    assert craft.altitude_ft == pytest.approx(32808, abs=2)
    assert craft.speed_kts == pytest.approx(450, abs=2)
    assert craft.bearing == "NE"
    assert craft.distance_miles < 25.0


def test_parse_states_drops_aircraft_beyond_the_radius():
    payload = {"states": [state("x", "FAR1", LON + 5.0, LAT + 5.0)]}
    assert parse_states(payload, LAT, LON, 25.0).aircraft == []


def test_parse_states_skips_entries_without_a_position():
    entry = state("x", "NOPOS", None, None)
    assert parse_states({"states": [entry]}, LAT, LON, 25.0).aircraft == []


def test_parse_states_skips_grounded_aircraft():
    payload = {"states": [state("x", "TAXI1", LON, LAT, on_ground=True)]}
    assert parse_states(payload, LAT, LON, 25.0).aircraft == []


def test_parse_states_sorts_by_distance():
    payload = {
        "states": [
            state("far", "FAR1", LON + 0.20, LAT),
            state("near", "NEAR1", LON + 0.02, LAT),
        ]
    }
    snapshot = parse_states(payload, LAT, LON, 25.0)
    assert [c.callsign for c in snapshot.aircraft] == ["NEAR1", "FAR1"]


def test_parse_states_caps_the_list():
    payload = {"states": [state(f"i{n}", f"C{n}", LON + n * 0.001, LAT) for n in range(MAX_AIRCRAFT + 5)]}
    assert len(parse_states(payload, LAT, LON, 25.0).aircraft) == MAX_AIRCRAFT


def test_parse_states_tolerates_a_missing_altitude():
    entry = state("x", "NOALT", LON + 0.01, LAT, altitude_m=None)
    craft = parse_states({"states": [entry]}, LAT, LON, 25.0).aircraft[0]
    assert craft.altitude_ft is None


def test_parse_states_falls_back_to_the_icao_when_the_callsign_is_blank():
    entry = state("4ca7b4", "   ", LON + 0.01, LAT)
    assert parse_states({"states": [entry]}, LAT, LON, 25.0).aircraft[0].callsign == "4ca7b4"


def test_parse_states_threads_the_place_through():
    snapshot = parse_states({"states": []}, LAT, LON, 25.0, "Lisbon")
    assert snapshot.place == "Lisbon"
    assert snapshot.radius_miles == 25.0
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv/bin/pytest scripts/dashboard/tests/test_sources_flights.py -v`
Expected: FAIL — `ImportError: cannot import name 'parse_states'`

- [ ] **Step 3: Write the implementation**

Add to the imports at the top of `scripts/dashboard/sources.py`:

```python
from .geo import bounding_box, compass_point, distance_miles, initial_bearing_degrees
```

Append to `scripts/dashboard/sources.py`:

```python
OPENSKY_URL = "https://opensky-network.org/api/states/all"
MAX_AIRCRAFT = 12
METRES_TO_FEET = 3.280839895
MPS_TO_KNOTS = 1.943844


@dataclass(frozen=True)
class Aircraft:
    callsign: str
    origin_country: str
    altitude_ft: int | None
    speed_kts: int | None
    distance_miles: float
    bearing: str


@dataclass(frozen=True)
class FlightSnapshot:
    place: str
    radius_miles: float
    aircraft: list[Aircraft]


def parse_states(
    payload: dict, lat: float, lon: float, radius_miles: float, place: str = ""
) -> FlightSnapshot:
    """Turn an OpenSky states/all response into a FlightSnapshot. Pure."""
    states = payload.get("states") or []

    found: list[Aircraft] = []
    for entry in states:
        if len(entry) < 10:
            continue
        craft_lon, craft_lat = entry[5], entry[6]
        if craft_lon is None or craft_lat is None or entry[8]:
            continue  # no position, or on the ground

        distance = distance_miles(lat, lon, craft_lat, craft_lon)
        if distance > radius_miles:
            continue  # the bounding box is square; the radius is not

        altitude_m = entry[7] if entry[7] is not None else entry[13]
        velocity = entry[9]
        found.append(
            Aircraft(
                callsign=str(entry[1] or "").strip() or str(entry[0] or "").strip(),
                origin_country=str(entry[2] or "").strip(),
                altitude_ft=int(altitude_m * METRES_TO_FEET) if altitude_m is not None else None,
                speed_kts=int(velocity * MPS_TO_KNOTS) if velocity is not None else None,
                distance_miles=distance,
                bearing=compass_point(initial_bearing_degrees(lat, lon, craft_lat, craft_lon)),
            )
        )

    found.sort(key=lambda craft: craft.distance_miles)
    return FlightSnapshot(place=place, radius_miles=radius_miles, aircraft=found[:MAX_AIRCRAFT])


def fetch_flights(config: Config) -> Fetched[FlightSnapshot]:
    """Fetch aircraft in range. Anonymous OpenSky is rate-limited; failure is routine."""
    box = bounding_box(config.lat, config.lon, config.radius_miles)
    url = (
        f"{OPENSKY_URL}?lamin={box.lat_min:.4f}&lomin={box.lon_min:.4f}"
        f"&lamax={box.lat_max:.4f}&lomax={box.lon_max:.4f}"
    )
    try:
        snapshot = parse_states(
            _get_json(url), config.lat, config.lon, config.radius_miles, config.place
        )
    except urllib.error.HTTPError as exc:
        if exc.code == 429:
            return Fetched(error="OpenSky rate limit reached")
        return Fetched(error=f"OpenSky returned HTTP {exc.code}")
    except (urllib.error.URLError, OSError) as exc:
        return Fetched(error=f"could not reach OpenSky ({exc})")
    except (ValueError, KeyError, IndexError, TypeError) as exc:
        return Fetched(error=f"unexpected OpenSky response ({exc})")

    return Fetched(value=snapshot)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv/bin/pytest scripts/dashboard/tests/test_sources_flights.py -v`
Expected: PASS, 11 tests

- [ ] **Step 5: Run the whole suite**

Run: `.venv/bin/pytest scripts/dashboard/tests -v`
Expected: PASS, all tests from Tasks 1–5

- [ ] **Step 6: Commit**

```bash
git add scripts/dashboard/sources.py scripts/dashboard/tests/test_sources_flights.py
git commit -m "feat: add the flights-overhead source"
```

---

### Task 6: Rendering

**Files:**
- Create: `scripts/dashboard/render.py`
- Create: `scripts/dashboard/tests/test_render.py`

**Interfaces:**
- Consumes: every dataclass from Tasks 3–5, plus `Config`.
- Produces:
  - `CSS: str` — the stylesheet, embedded by `deliver.py`
  - `render_weather(fetched: Fetched[Forecast], generated: datetime.datetime) -> str`
  - `render_news(fetched: Fetched[NewsDigest], generated: datetime.datetime) -> str`
  - `render_sky(fetched: Fetched[Forecast], generated: datetime.datetime) -> str`
  - `render_flights(fetched: Fetched[FlightSnapshot], generated: datetime.datetime) -> str`

Each returns an XHTML **body fragment**, not a whole document — `deliver.py` wraps it. Pure: the timestamp is passed in, never read from the clock.

- [ ] **Step 1: Write the failing tests**

Create `scripts/dashboard/tests/test_render.py`:

```python
import datetime as dt

from dashboard.render import render_flights, render_news, render_sky, render_weather
from dashboard.sources import (
    Aircraft,
    CurrentConditions,
    DayForecast,
    Fetched,
    FeedResult,
    FlightSnapshot,
    Forecast,
    Headline,
    NewsDigest,
)

GENERATED = dt.datetime(2026, 8, 9, 7, 30)

FORECAST = Fetched(
    value=Forecast(
        place="Lisbon",
        current=CurrentConditions(24.3, 25.1, "Partly cloudy", 12.4, "°C", "km/h"),
        days=[
            DayForecast(
                date=dt.date(2026, 8, 9),
                description="Partly cloudy",
                high=28.1,
                low=18.0,
                precipitation_chance=10,
                sunrise=dt.datetime(2026, 8, 9, 6, 52),
                sunset=dt.datetime(2026, 8, 9, 20, 31),
            )
        ],
    )
)


def test_weather_shows_the_current_temperature_and_place():
    html = render_weather(FORECAST, GENERATED)
    assert "Lisbon" in html
    assert "24.3" in html
    assert "Partly cloudy" in html


def test_weather_shows_the_generation_timestamp():
    assert "2026-08-09 07:30" in render_weather(FORECAST, GENERATED)


def test_weather_shows_the_daily_high_and_low():
    html = render_weather(FORECAST, GENERATED)
    assert "28.1" in html
    assert "18.0" in html


def test_a_failed_section_renders_the_reason():
    # Assert on the visible text, not the CSS class name — renaming the class
    # must not silently gut this test.
    html = render_weather(Fetched(error="could not reach Open-Meteo"), GENERATED)
    assert "Unavailable —" in html
    assert "could not reach Open-Meteo" in html


def test_sky_shows_sun_times_and_the_moon_phase():
    html = render_sky(FORECAST, GENERATED)
    assert "06:52" in html
    assert "20:31" in html
    # 2026-08-09 falls in a known phase; just assert a phase name is present.
    assert "Moon" in html or "Quarter" in html


def test_news_escapes_html_in_titles():
    digest = Fetched(
        value=NewsDigest(feeds=[FeedResult(name="BBC", headlines=[Headline("A & <b>B</b>", None)])])
    )
    html = render_news(digest, GENERATED)
    assert "A &amp; &lt;b&gt;B&lt;/b&gt;" in html
    assert "<b>B</b>" not in html


def test_news_shows_a_per_feed_error_without_losing_other_feeds():
    digest = Fetched(
        value=NewsDigest(
            feeds=[
                FeedResult(name="Dead", headlines=[], error="unreachable"),
                FeedResult(name="Live", headlines=[Headline("Working", None)]),
            ]
        )
    )
    html = render_news(digest, GENERATED)
    assert "unreachable" in html
    assert "Working" in html


def test_flights_lists_aircraft_with_distance_and_bearing():
    snapshot = Fetched(
        value=FlightSnapshot(
            place="Lisbon",
            radius_miles=25.0,
            aircraft=[Aircraft("RYR4TL", "Ireland", 32808, 450, 4.2, "NE")],
        )
    )
    html = render_flights(snapshot, GENERATED)
    assert "RYR4TL" in html
    assert "NE" in html
    assert "4.2" in html


def test_flights_with_nothing_overhead_says_so():
    snapshot = Fetched(value=FlightSnapshot(place="Lisbon", radius_miles=25.0, aircraft=[]))
    html = render_flights(snapshot, GENERATED)
    assert "Nothing overhead" in html


def test_every_renderer_returns_a_fragment_not_a_document():
    for html in (
        render_weather(FORECAST, GENERATED),
        render_news(Fetched(error="x"), GENERATED),
        render_sky(FORECAST, GENERATED),
        render_flights(Fetched(error="x"), GENERATED),
    ):
        assert "<html" not in html
        assert "<body" not in html
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv/bin/pytest scripts/dashboard/tests/test_render.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'dashboard.render'`

- [ ] **Step 3: Write the implementation**

Create `scripts/dashboard/render.py`:

```python
"""Dataclasses to XHTML fragments.

Pure: no network, no file I/O, no clock reads — the generation timestamp is
passed in. Every interpolated string goes through html.escape().
"""

from __future__ import annotations

import datetime as dt
from html import escape

from .sources import (
    Fetched,
    FlightSnapshot,
    Forecast,
    NewsDigest,
    moon_phase,
)

CSS = """
body { font-family: Georgia, serif; font-size: 1em; line-height: 1.5; margin: 1em 1.2em; }
h1 { font-size: 1.5em; margin: 0 0 0.1em 0; }
.stamp { font-size: 0.75em; color: #555; margin: 0 0 1em 0; }
.now { font-size: 2.2em; margin: 0.2em 0 0 0; }
.now-sub { font-size: 0.95em; color: #333; margin: 0 0 1em 0; }
table { width: 100%; border-collapse: collapse; margin: 0.5em 0 1em 0; }
th, td { text-align: left; padding: 0.25em 0.4em; border-bottom: 1px solid #ddd; font-size: 0.9em; }
th { font-size: 0.75em; text-transform: uppercase; color: #555; }
td.num { text-align: right; }
h2 { font-size: 1.05em; margin: 1.1em 0 0.3em 0; }
ul { margin: 0.2em 0 0.8em 1.1em; padding: 0; }
li { margin: 0.35em 0; font-size: 0.92em; }
.unavailable { font-style: italic; color: #666; margin: 1em 0; }
.feed-error { font-style: italic; color: #666; font-size: 0.85em; }
.empty { font-style: italic; color: #666; }
.pair { margin: 0.3em 0; font-size: 0.95em; }
"""


def _header(title: str, generated: dt.datetime) -> str:
    stamp = generated.strftime("%Y-%m-%d %H:%M")
    return f"<h1>{escape(title)}</h1>\n<p class=\"stamp\">Generated {escape(stamp)}</p>"


def _unavailable(reason: str) -> str:
    return f'<p class="unavailable">Unavailable — {escape(reason)}</p>'


def render_weather(fetched: Fetched[Forecast], generated: dt.datetime) -> str:
    parts = [_header("Weather", generated)]
    if not fetched.ok:
        parts.append(_unavailable(fetched.error or "no data"))
        return "\n".join(parts)

    forecast = fetched.value
    current = forecast.current
    parts.append(f'<p class="now">{escape(f"{current.temperature:.1f}{current.temperature_unit}")}</p>')
    parts.append(
        '<p class="now-sub">'
        + escape(
            f"{current.description} in {forecast.place} · "
            f"feels like {current.apparent_temperature:.1f}{current.temperature_unit} · "
            f"wind {current.wind_speed:.0f} {current.wind_unit}"
        )
        + "</p>"
    )

    rows = [
        "<table>",
        "<tr><th>Day</th><th>Conditions</th><th>High</th><th>Low</th><th>Rain</th></tr>",
    ]
    for day in forecast.days:
        rows.append(
            "<tr>"
            f"<td>{escape(day.date.strftime('%a %d'))}</td>"
            f"<td>{escape(day.description)}</td>"
            f'<td class="num">{escape(f"{day.high:.1f}")}</td>'
            f'<td class="num">{escape(f"{day.low:.1f}")}</td>'
            f'<td class="num">{day.precipitation_chance}%</td>'
            "</tr>"
        )
    rows.append("</table>")
    parts.append("\n".join(rows))
    return "\n".join(parts)


def render_news(fetched: Fetched[NewsDigest], generated: dt.datetime) -> str:
    parts = [_header("News", generated)]
    if not fetched.ok:
        parts.append(_unavailable(fetched.error or "no data"))
        return "\n".join(parts)

    for feed in fetched.value.feeds:
        parts.append(f"<h2>{escape(feed.name)}</h2>")
        if feed.error:
            parts.append(f'<p class="feed-error">{escape(feed.error)}</p>')
            continue
        items = "\n".join(f"<li>{escape(headline.title)}</li>" for headline in feed.headlines)
        parts.append(f"<ul>\n{items}\n</ul>")
    return "\n".join(parts)


def render_sky(fetched: Fetched[Forecast], generated: dt.datetime) -> str:
    parts = [_header("Sky", generated)]
    if not fetched.ok or not fetched.value.days:
        parts.append(_unavailable(fetched.error or "no data"))
        return "\n".join(parts)

    today = fetched.value.days[0]
    phase = moon_phase(today.date)
    parts.append(f'<p class="pair">{escape(today.date.strftime("%A, %d %B %Y"))}</p>')
    parts.append(f'<p class="pair">Sunrise {escape(today.sunrise.strftime("%H:%M"))}</p>')
    parts.append(f'<p class="pair">Sunset {escape(today.sunset.strftime("%H:%M"))}</p>')
    daylight = today.sunset - today.sunrise
    hours, minutes = divmod(int(daylight.total_seconds()) // 60, 60)
    parts.append(f'<p class="pair">Daylight {hours}h {minutes:02d}m</p>')
    parts.append(f'<p class="pair">Moon {escape(phase.name)} ({phase.fraction * 100:.0f}% through cycle)</p>')
    return "\n".join(parts)


def render_flights(fetched: Fetched[FlightSnapshot], generated: dt.datetime) -> str:
    parts = [_header("Flights", generated)]
    if not fetched.ok:
        parts.append(_unavailable(fetched.error or "no data"))
        return "\n".join(parts)

    snapshot = fetched.value
    parts.append(
        f'<p class="stamp">Within {escape(f"{snapshot.radius_miles:.0f}")} miles of '
        f"{escape(snapshot.place)}</p>"
    )
    if not snapshot.aircraft:
        parts.append('<p class="empty">Nothing overhead.</p>')
        return "\n".join(parts)

    rows = [
        "<table>",
        "<tr><th>Callsign</th><th>From</th><th>Alt ft</th><th>Kts</th><th>Dist</th><th>Dir</th></tr>",
    ]
    for craft in snapshot.aircraft:
        altitude = f"{craft.altitude_ft:,}" if craft.altitude_ft is not None else "—"
        speed = str(craft.speed_kts) if craft.speed_kts is not None else "—"
        rows.append(
            "<tr>"
            f"<td>{escape(craft.callsign)}</td>"
            f"<td>{escape(craft.origin_country)}</td>"
            f'<td class="num">{escape(altitude)}</td>'
            f'<td class="num">{escape(speed)}</td>'
            f'<td class="num">{escape(f"{craft.distance_miles:.1f}")}</td>'
            f"<td>{escape(craft.bearing)}</td>"
            "</tr>"
        )
    rows.append("</table>")
    parts.append("\n".join(rows))
    return "\n".join(parts)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv/bin/pytest scripts/dashboard/tests/test_render.py -v`
Expected: PASS, 11 tests

- [ ] **Step 5: Commit**

```bash
git add scripts/dashboard/render.py scripts/dashboard/tests/test_render.py
git commit -m "feat: add dashboard section rendering"
```

---

### Task 7: Cover image and EPUB packaging

**Files:**
- Create: `scripts/dashboard/deliver.py`
- Create: `scripts/dashboard/tests/test_deliver.py`

**Interfaces:**
- Consumes: `CSS` from `render`.
- Produces:
  - `make_cover_png(day: datetime.date, place: str) -> bytes`
  - `build_epub(sections: list[tuple[str, str]], cover_png: bytes, out_path: Path, generated: datetime.datetime) -> Path`

`sections` is a list of `(title, body_fragment)` pairs — one XHTML document each, so section boundaries land on page breaks rather than mid-page.

The EPUB structure copies `scripts/generate_userguide_epub.py` exactly, including three details the firmware depends on: `uid='cover-image'` on the cover item, the EPUB 2 `<meta name="cover">` declaration that the X4's OPF lookup reads, and **excluding the nav document from the spine** (the firmware ignores `linear="no"`, so an included nav shows up as a readable page).

- [ ] **Step 1: Write the failing tests**

Create `scripts/dashboard/tests/test_deliver.py`:

```python
import datetime as dt
import zipfile

from dashboard.deliver import build_epub, make_cover_png

GENERATED = dt.datetime(2026, 8, 9, 7, 30)
SECTIONS = [
    ("Weather", "<h1>Weather</h1><p>24.3</p>"),
    ("News", "<h1>News</h1><ul><li>A story</li></ul>"),
]


def test_cover_is_a_480x800_png():
    data = make_cover_png(dt.date(2026, 8, 9), "Lisbon")
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    # PNG IHDR: width and height are big-endian uint32 at offsets 16 and 20.
    assert int.from_bytes(data[16:20], "big") == 480
    assert int.from_bytes(data[20:24], "big") == 800


def test_build_epub_writes_a_zip(tmp_path):
    out = build_epub(SECTIONS, make_cover_png(dt.date(2026, 8, 9), "Lisbon"), tmp_path / "D.epub", GENERATED)
    assert out.is_file()
    assert zipfile.is_zipfile(out)


def test_build_epub_contains_one_document_per_section(tmp_path):
    out = build_epub(SECTIONS, make_cover_png(dt.date(2026, 8, 9), "Lisbon"), tmp_path / "D.epub", GENERATED)
    with zipfile.ZipFile(out) as archive:
        names = archive.namelist()
    assert any(n.endswith("sec_weather.xhtml") for n in names)
    assert any(n.endswith("sec_news.xhtml") for n in names)


def test_build_epub_embeds_the_cover(tmp_path):
    out = build_epub(SECTIONS, make_cover_png(dt.date(2026, 8, 9), "Lisbon"), tmp_path / "D.epub", GENERATED)
    with zipfile.ZipFile(out) as archive:
        names = archive.namelist()
        opf = next(n for n in names if n.endswith(".opf"))
        manifest = archive.read(opf).decode("utf-8")
    assert any(n.endswith("images/cover.png") for n in names)
    assert 'name="cover"' in manifest
    assert 'content="cover-image"' in manifest


def test_build_epub_keeps_the_nav_out_of_the_spine(tmp_path):
    out = build_epub(SECTIONS, make_cover_png(dt.date(2026, 8, 9), "Lisbon"), tmp_path / "D.epub", GENERATED)
    with zipfile.ZipFile(out) as archive:
        opf = next(n for n in archive.namelist() if n.endswith(".opf"))
        manifest = archive.read(opf).decode("utf-8")
    spine = manifest.split("<spine")[1].split("</spine>")[0]
    assert "nav" not in spine


def test_build_epub_body_content_survives(tmp_path):
    out = build_epub(SECTIONS, make_cover_png(dt.date(2026, 8, 9), "Lisbon"), tmp_path / "D.epub", GENERATED)
    with zipfile.ZipFile(out) as archive:
        page = next(n for n in archive.namelist() if n.endswith("sec_weather.xhtml"))
        content = archive.read(page).decode("utf-8")
    assert "24.3" in content


def test_build_epub_creates_missing_parent_directories(tmp_path):
    target = tmp_path / "build" / "nested" / "D.epub"
    out = build_epub(SECTIONS, make_cover_png(dt.date(2026, 8, 9), "Lisbon"), target, GENERATED)
    assert out.is_file()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv/bin/pytest scripts/dashboard/tests/test_deliver.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'dashboard.deliver'`

- [ ] **Step 3: Write the implementation**

Create `scripts/dashboard/deliver.py`:

```python
"""Cover generation, EPUB packaging, and delivery to the device."""

from __future__ import annotations

import datetime as dt
import io
import re
from pathlib import Path

from ebooklib import epub
from PIL import Image, ImageDraw, ImageFont

from .render import CSS

# Portrait cover matching the X4 display.
COVER_W, COVER_H = 480, 800

REPO_ROOT = Path(__file__).resolve().parents[2]
LOGO_PNG = REPO_ROOT / "src/images/Logo120.png"

# Tried in order; the first that loads wins. macOS first, then Linux, so the
# script works on either host without a bundled font.
_FONT_CANDIDATES = (
    "/System/Library/Fonts/Supplemental/Georgia Bold.ttf",
    "/System/Library/Fonts/Supplemental/Times New Roman Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
)


def _load_font(size: int) -> ImageFont.ImageFont:
    for candidate in _FONT_CANDIDATES:
        try:
            return ImageFont.truetype(candidate, size)
        except OSError:
            continue
    return ImageFont.load_default(size=size)


def _draw_centred(draw: ImageDraw.ImageDraw, y: int, text: str, font, fill) -> int:
    """Draw `text` centred horizontally at `y`. Returns the y below the text."""
    box = draw.textbbox((0, 0), text, font=font)
    draw.text(((COVER_W - (box[2] - box[0])) // 2, y), text, fill=fill, font=font)
    return y + (box[3] - box[1])


def make_cover_png(day: dt.date, place: str) -> bytes:
    """A 480x800 cover carrying the date, so the Home tile shows which day is loaded."""
    cover = Image.new("RGB", (COVER_W, COVER_H), color=(255, 255, 255))
    draw = ImageDraw.Draw(cover)

    if LOGO_PNG.is_file():
        with Image.open(LOGO_PNG) as raw:
            if raw.mode in ("RGBA", "LA", "P"):
                background = Image.new("RGB", raw.size, (255, 255, 255))
                background.paste(raw, mask=raw.convert("RGBA").split()[3])
                logo = background
            else:
                logo = raw.convert("RGB")
        logo = logo.resize((160, 160), Image.LANCZOS)
        cover.paste(logo, ((COVER_W - 160) // 2, 120))

    y = 340
    draw.line([(60, y - 24), (COVER_W - 60, y - 24)], fill=(180, 180, 180), width=1)
    y = _draw_centred(draw, y, day.strftime("%A"), _load_font(40), (0, 0, 0)) + 18
    y = _draw_centred(draw, y, day.strftime("%d %B %Y"), _load_font(34), (0, 0, 0)) + 22
    y = _draw_centred(draw, y, place, _load_font(26), (80, 80, 80)) + 24
    draw.line([(60, y), (COVER_W - 60, y)], fill=(180, 180, 180), width=1)
    _draw_centred(draw, y + 30, "Dashboard", _load_font(22), (120, 120, 120))

    buffer = io.BytesIO()
    cover.save(buffer, format="PNG")
    return buffer.getvalue()


def _make_xhtml(title: str, body: str) -> bytes:
    return (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" '
        '"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">\n'
        '<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en">\n'
        f"<head><title>{title}</title>\n"
        '<link rel="stylesheet" type="text/css" href="../style/main.css"/>\n'
        f"</head>\n<body>\n{body}\n</body>\n</html>"
    ).encode("utf-8")


def _slug(title: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-") or "section"


def build_epub(
    sections: list[tuple[str, str]],
    cover_png: bytes,
    out_path: Path,
    generated: dt.datetime,
) -> Path:
    """Package sections into an EPUB at out_path. One document per section."""
    out_path.parent.mkdir(parents=True, exist_ok=True)

    book = epub.EpubBook()
    book.set_identifier(f"almanac-dashboard-{generated:%Y%m%d%H%M}")
    book.set_title(f"Dashboard — {generated:%d %B %Y}")
    book.set_language("en")
    book.add_author("Almanac")

    style = epub.EpubItem(uid="style", file_name="style/main.css", media_type="text/css", content=CSS)
    book.add_item(style)

    # uid='cover-image' matches the EPUB 2 <meta name="cover"> value the X4 reads.
    cover_item = epub.EpubItem(
        uid="cover-image", file_name="images/cover.png", media_type="image/png", content=cover_png
    )
    cover_item.properties = ["cover-image"]
    book.add_item(cover_item)
    book.add_metadata("OPF", "meta", "", {"name": "cover", "content": "cover-image"})

    cover_page = epub.EpubHtml(title="Dashboard", file_name="cover.xhtml", lang="en")
    cover_page.content = _make_xhtml(
        "Dashboard", '<div style="text-align:center"><img src="images/cover.png" alt="Dashboard cover"/></div>'
    )
    cover_page.add_item(style)
    cover_page.add_item(cover_item)
    book.add_item(cover_page)

    pages = []
    for title, fragment in sections:
        page = epub.EpubHtml(title=title, file_name=f"sec_{_slug(title)}.xhtml", lang="en")
        page.content = _make_xhtml(title, fragment)
        page.add_item(style)
        book.add_item(page)
        pages.append(page)

    book.toc = (epub.Link("cover.xhtml", "Cover", "cover"),) + tuple(pages)
    book.add_item(epub.EpubNcx())
    book.add_item(epub.EpubNav())

    # The nav is deliberately out of the spine: the firmware locates it via the
    # manifest's properties="nav" and does not respect linear="no", so including
    # it would surface the TOC as a readable page.
    book.spine = [cover_page] + pages

    epub.write_epub(str(out_path), book)
    return out_path
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv/bin/pytest scripts/dashboard/tests/test_deliver.py -v`
Expected: PASS, 7 tests

- [ ] **Step 5: Commit**

```bash
git add scripts/dashboard/deliver.py scripts/dashboard/tests/test_deliver.py
git commit -m "feat: add dashboard cover and EPUB packaging"
```

---

### Task 8: Upload, CLI, and documentation

**Files:**
- Modify: `scripts/dashboard/deliver.py` (append)
- Create: `scripts/dashboard/__main__.py`
- Create: `scripts/generate_dashboard_epub.py`
- Create: `scripts/dashboard/tests/test_upload.py`
- Create: `docs/mini-dashboard.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: everything above.
- Produces:
  - `UploadStatus` — `str` enum with members `UPLOADED`, `UNREACHABLE`, `REJECTED`
  - `UploadResult(status: UploadStatus, detail: str)`
  - `encode_multipart(field_name: str, filename: str, payload: bytes, boundary: str) -> bytes`
  - `upload(epub_path: Path, host: str, remote_dir: str) -> UploadResult`
  - `curl_hint(epub_path: Path, host: str, remote_dir: str) -> str`
  - `main(argv: list[str] | None = None) -> int`

The device's HTTP form field is `file` and the target directory is the `path` query parameter, matching the documented `curl -X POST -F "file=@mybook.epub" "http://almanac.local/upload?path=/Books"`.

**Exit codes:** 0 success, 1 every source failed (the EPUB is still built and still uploaded), 2 config error, 3 upload rejected by the device.

- [ ] **Step 1: Write the failing tests**

Create `scripts/dashboard/tests/test_upload.py`:

```python
from pathlib import Path

from dashboard.deliver import curl_hint, encode_multipart


def test_multipart_body_has_the_boundary_markers():
    body = encode_multipart("file", "Dashboard.epub", b"PK\x03\x04data", "XBOUNDARY")
    assert body.startswith(b"--XBOUNDARY\r\n")
    assert body.endswith(b"--XBOUNDARY--\r\n")


def test_multipart_body_declares_the_field_and_filename():
    body = encode_multipart("file", "Dashboard.epub", b"PK", "XBOUNDARY")
    assert b'name="file"' in body
    assert b'filename="Dashboard.epub"' in body


def test_multipart_body_carries_the_payload_verbatim():
    payload = b"PK\x03\x04\x00binary\xff"
    assert payload in encode_multipart("file", "D.epub", payload, "XBOUNDARY")


def test_multipart_body_sets_a_binary_content_type():
    body = encode_multipart("file", "D.epub", b"PK", "XBOUNDARY")
    assert b"Content-Type: application/epub+zip" in body


def test_curl_hint_quotes_the_url_and_names_the_file():
    hint = curl_hint(Path("/tmp/Dashboard.epub"), "almanac.local", "/Books")
    assert 'curl -X POST -F "file=@/tmp/Dashboard.epub"' in hint
    assert '"http://almanac.local/upload?path=/Books"' in hint
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv/bin/pytest scripts/dashboard/tests/test_upload.py -v`
Expected: FAIL — `ImportError: cannot import name 'encode_multipart'`

- [ ] **Step 3: Write the upload implementation**

Add to the imports at the top of `scripts/dashboard/deliver.py`:

```python
import urllib.error
import urllib.parse
import urllib.request
import uuid
from dataclasses import dataclass
from enum import Enum
```

Append to `scripts/dashboard/deliver.py`:

```python
UPLOAD_TIMEOUT_SECONDS = 60
UPLOAD_FIELD_NAME = "file"


class UploadStatus(str, Enum):
    UPLOADED = "uploaded"
    UNREACHABLE = "unreachable"
    REJECTED = "rejected"


@dataclass(frozen=True)
class UploadResult:
    status: UploadStatus
    detail: str


def encode_multipart(field_name: str, filename: str, payload: bytes, boundary: str) -> bytes:
    """Build a multipart/form-data body. Stdlib only — no HTTP client dependency."""
    return b"".join(
        [
            f"--{boundary}\r\n".encode(),
            f'Content-Disposition: form-data; name="{field_name}"; filename="{filename}"\r\n'.encode(),
            b"Content-Type: application/epub+zip\r\n\r\n",
            payload,
            f"\r\n--{boundary}--\r\n".encode(),
        ]
    )


def curl_hint(epub_path: Path, host: str, remote_dir: str) -> str:
    """The exact command to push the file later, for when the device is offline."""
    return (
        f'curl -X POST -F "file=@{epub_path}" '
        f'"http://{host}/upload?path={remote_dir}"'
    )


def upload(epub_path: Path, host: str, remote_dir: str) -> UploadResult:
    """POST the EPUB to the device. Never raises."""
    boundary = uuid.uuid4().hex
    body = encode_multipart(UPLOAD_FIELD_NAME, epub_path.name, epub_path.read_bytes(), boundary)
    url = f"http://{host}/upload?path={urllib.parse.quote(remote_dir)}"

    request = urllib.request.Request(
        url,
        data=body,
        method="POST",
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
    )

    try:
        with urllib.request.urlopen(request, timeout=UPLOAD_TIMEOUT_SECONDS) as response:
            return UploadResult(
                status=UploadStatus.UPLOADED,
                detail=response.read().decode("utf-8", errors="replace").strip(),
            )
    except urllib.error.HTTPError as exc:
        return UploadResult(
            status=UploadStatus.REJECTED,
            detail=f"HTTP {exc.code}: {exc.read().decode('utf-8', errors='replace').strip()}",
        )
    except (urllib.error.URLError, OSError) as exc:
        return UploadResult(status=UploadStatus.UNREACHABLE, detail=str(exc))
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv/bin/pytest scripts/dashboard/tests/test_upload.py -v`
Expected: PASS, 5 tests

- [ ] **Step 5: Write the CLI**

Create `scripts/dashboard/__main__.py`:

```python
"""Build the dashboard EPUB and push it to the device.

Invoked through scripts/generate_dashboard_epub.py.

Exit codes: 0 success, 1 every source failed, 2 config error, 3 upload rejected.
"""

from __future__ import annotations

import argparse
import datetime as dt
import sys
from pathlib import Path

from .config import ConfigError, load_config
from .deliver import UploadStatus, build_epub, curl_hint, make_cover_png, upload
from .render import render_flights, render_news, render_sky, render_weather
from .sources import fetch_flights, fetch_forecast, fetch_news

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CONFIG = REPO_ROOT / "scripts/dashboard.config.local.json"
DEFAULT_OUT = REPO_ROOT / "build/Dashboard.epub"  # build/ is gitignored

EXIT_OK = 0
EXIT_NO_DATA = 1
EXIT_CONFIG = 2
EXIT_REJECTED = 3


def parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="generate_dashboard_epub.py", description=__doc__)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG, help="config file path")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="where to write the EPUB")
    parser.add_argument("--no-upload", action="store_true", help="build only, do not upload")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)

    try:
        config = load_config(args.config)
    except ConfigError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return EXIT_CONFIG

    generated = dt.datetime.now()

    forecast = fetch_forecast(config)
    news = fetch_news(config)
    flights = fetch_flights(config)

    for label, fetched in (("weather", forecast), ("news", news), ("flights", flights)):
        if not fetched.ok:
            print(f"warning: {label} unavailable — {fetched.error}", file=sys.stderr)

    sections = [
        ("Weather", render_weather(forecast, generated)),
        ("News", render_news(news, generated)),
        ("Sky", render_sky(forecast, generated)),
        ("Flights", render_flights(flights, generated)),
    ]

    out_path = build_epub(
        sections, make_cover_png(generated.date(), config.place), args.out, generated
    )
    print(f"built {out_path}")

    content_code = EXIT_OK if (forecast.ok or news.ok or flights.ok) else EXIT_NO_DATA

    if args.no_upload:
        return content_code

    result = upload(out_path, config.device_host, config.device_path)
    if result.status is UploadStatus.UPLOADED:
        print(f"uploaded to {config.device_host}:{config.device_path}/{out_path.name}")
        return content_code

    if result.status is UploadStatus.REJECTED:
        print(f"error: device rejected the upload — {result.detail}", file=sys.stderr)
        print(f"the file is still at {out_path}", file=sys.stderr)
        return EXIT_REJECTED

    print(f"device not reachable ({result.detail})")
    print("put it into File Transfer mode, then run:")
    print(f"  {curl_hint(out_path, config.device_host, config.device_path)}")
    return content_code


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 6: Create the flat launcher**

Create `scripts/generate_dashboard_epub.py`. This is the entry point users run, named to match the existing `generate_*_epub.py` family. It puts `scripts/` on `sys.path` so `dashboard` imports as a top-level package — which is why no `scripts/__init__.py` is needed.

```python
#!/usr/bin/env python3
"""Generate the dashboard EPUB and push it to the device.

See docs/mini-dashboard.md. The implementation lives in scripts/dashboard/.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from dashboard.__main__ import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 7: Run the full suite and an end-to-end build**

Run:

```bash
.venv/bin/pytest -v
.venv/bin/python scripts/generate_dashboard_epub.py --no-upload
```

Expected: all tests pass; the second command prints `built …/build/Dashboard.epub`. Warnings about individual unavailable sources are normal (anonymous OpenSky is rate-limited). Confirm the file exists and is a zip:

```bash
.venv/bin/python -c "import zipfile;print(zipfile.is_zipfile('build/Dashboard.epub'))"
```

Expected: `True`

- [ ] **Step 8: Write the user documentation**

Create `docs/mini-dashboard.md`. The `~~~~` fence below delimits the file body and is **not** part of it — the content contains its own triple-backtick blocks, which is why the outer fence uses tildes. Write everything between the tilde markers, and nothing else.

~~~~
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

- The filename is always `Dashboard.epub`. The firmware clears that book's cache
  on upload, so overwriting re-renders it and returns you to page 1.
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
~~~~

- [ ] **Step 9: Link it from the README**

`README.md` has no bulleted doc list — it points at `docs/` in a sentence near the end ([README.md:238](../../../README.md)). Extend that sentence rather than inventing a list.

Replace exactly this text:

```markdown
See [docs/](docs/) for the file formats, activity manager, i18n and webserver
documentation, and [CLAUDE.md](CLAUDE.md) for the engineering constraints any
change has to respect.
```

with:

```markdown
See [docs/](docs/) for the file formats, activity manager, i18n and webserver
documentation, [docs/mini-dashboard.md](docs/mini-dashboard.md) for the
host-side weather/news/sky/flights page generator, and [CLAUDE.md](CLAUDE.md)
for the engineering constraints any change has to respect.
```

- [ ] **Step 10: Verify the docs are accurate**

Run each command block from `docs/mini-dashboard.md` in order and confirm it behaves as written. Fix the doc, not your memory of it, if anything differs.

- [ ] **Step 11: Commit**

```bash
git add scripts/dashboard/deliver.py scripts/dashboard/__main__.py scripts/generate_dashboard_epub.py scripts/dashboard/tests/test_upload.py docs/mini-dashboard.md README.md
git commit -m "feat: add dashboard upload, CLI, and documentation"
```

---

## Manual verification

These cannot be automated and are the user's to run:

- [ ] Put the X4 into File Transfer mode and run `.venv/bin/python scripts/generate_dashboard_epub.py` with no flags. Confirm it reports `uploaded`.
- [ ] Open `Dashboard.epub` on the device. Confirm the cover shows today's date and the Home tile picks it up.
- [ ] Page through all four sections. Confirm each starts on its own page and the TOC lists them.
- [ ] Check the four orientations (Portrait, Inverted, Landscape CW, Landscape CCW).
- [ ] Re-run the command and confirm the device shows the new content rather than the cached previous day.
- [ ] Run once with Wi-Fi off on the host and confirm the EPUB still builds with four "unavailable" sections and prints the curl hint.

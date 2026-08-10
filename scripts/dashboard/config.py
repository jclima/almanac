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

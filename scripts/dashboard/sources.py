"""Data sources for the dashboard.

Every fetcher returns Fetched[T] and never raises: a source failing is data the
renderer displays, not an exception that aborts the run. Parsing is split out
into pure functions so it can be tested without network access.
"""

from __future__ import annotations

import datetime as dt
import http.client
import io
import json
import math
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Generic, TypeVar

import feedparser

from .config import Config
from .geo import bounding_box, compass_point, distance_miles, initial_bearing_degrees

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
    """GET a URL and decode a JSON object. Raises on transport, decode, or shape failure."""
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=HTTP_TIMEOUT_SECONDS) as response:
        payload = json.loads(response.read().decode("utf-8"))
    if not isinstance(payload, dict):
        raise ValueError(f"expected a JSON object, got {type(payload).__name__}")
    return payload


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
    """Turn an Open-Meteo response into a Forecast. Pure.

    Raises ValueError, KeyError, IndexError or TypeError on a malformed payload.
    """
    if not isinstance(payload, dict):
        raise ValueError(f"expected a JSON object, got {type(payload).__name__}")
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
    temperature_unit = urllib.parse.quote(config.temperature_unit)
    wind_unit = urllib.parse.quote(config.wind_unit)
    url = (
        f"{OPEN_METEO_URL}?latitude={config.lat:.4f}&longitude={config.lon:.4f}"
        "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
        "precipitation_probability_max,sunrise,sunset"
        f"&timezone=auto&forecast_days={FORECAST_DAYS}"
        f"&temperature_unit={temperature_unit}&wind_speed_unit={wind_unit}"
    )
    try:
        return Fetched(value=parse_forecast(_get_json(url), config.place))
    except (urllib.error.URLError, OSError, http.client.HTTPException) as exc:
        return Fetched(error=f"could not reach Open-Meteo ({exc})")
    except (ValueError, KeyError, IndexError, TypeError, AttributeError) as exc:
        return Fetched(error=f"unexpected Open-Meteo response ({exc})")


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
        except (urllib.error.URLError, OSError, http.client.HTTPException, ValueError) as exc:
            results.append(FeedResult(name=feed.name, headlines=[], error=f"unreachable ({exc})"))

    return Fetched(value=NewsDigest(feeds=results))


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
    """Turn an OpenSky states/all response into a FlightSnapshot.

    Pure. Malformed entries are skipped; a non-dict payload raises ValueError.
    """
    if not isinstance(payload, dict):
        raise ValueError(f"expected a JSON object, got {type(payload).__name__}")
    states = payload.get("states") or []

    found: list[Aircraft] = []
    for entry in states:
        try:
            if len(entry) < 10:
                continue
            craft_lon, craft_lat = entry[5], entry[6]
            if craft_lon is None or craft_lat is None or entry[8]:
                continue  # no position, or on the ground

            distance = distance_miles(lat, lon, craft_lat, craft_lon)
            if distance > radius_miles:
                continue  # the bounding box is square; the radius is not

            altitude_m = entry[7]
            if altitude_m is None and len(entry) > 13:
                altitude_m = entry[13]
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
        except (TypeError, ValueError, IndexError):
            continue  # one malformed record must not blank the whole section

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
    except (urllib.error.URLError, OSError, http.client.HTTPException) as exc:
        return Fetched(error=f"could not reach OpenSky ({exc})")
    except (ValueError, KeyError, IndexError, TypeError) as exc:
        return Fetched(error=f"unexpected OpenSky response ({exc})")

    return Fetched(value=snapshot)

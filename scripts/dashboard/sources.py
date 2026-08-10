"""Data sources for the dashboard.

Every fetcher returns Fetched[T] and never raises: a source failing is data the
renderer displays, not an exception that aborts the run. Parsing is split out
into pure functions so it can be tested without network access.
"""

from __future__ import annotations

import datetime as dt
import http.client
import json
import math
import urllib.error
import urllib.parse
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

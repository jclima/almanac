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

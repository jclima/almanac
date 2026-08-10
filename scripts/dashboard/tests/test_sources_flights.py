import http.client
import urllib.error

import pytest

from dashboard import sources
from dashboard.config import Config
from dashboard.sources import MAX_AIRCRAFT, parse_states

LAT, LON = 38.7223, -9.1393
CONFIG = Config(place="Lisbon", lat=LAT, lon=LON)


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


def test_fetch_flights_reports_the_opensky_rate_limit(monkeypatch):
    def rate_limited(url):
        raise urllib.error.HTTPError(url, 429, "Too Many Requests", None, None)

    monkeypatch.setattr(sources, "_get_json", rate_limited)
    result = sources.fetch_flights(CONFIG)
    assert not result.ok
    assert result.error == "OpenSky rate limit reached"


def test_fetch_flights_reports_other_http_errors(monkeypatch):
    def server_error(url):
        raise urllib.error.HTTPError(url, 503, "Service Unavailable", None, None)

    monkeypatch.setattr(sources, "_get_json", server_error)
    result = sources.fetch_flights(CONFIG)
    assert not result.ok
    assert result.error == "OpenSky returned HTTP 503"


def test_fetch_flights_survives_a_truncated_response(monkeypatch):
    # http.client.HTTPException does not subclass OSError (see fetch_forecast's
    # and fetch_news's equivalent regression tests), so a raw except (URLError,
    # OSError) would let this propagate. fetch_flights must catch it too.
    def truncated(url):
        raise http.client.IncompleteRead(b"partial")

    monkeypatch.setattr(sources, "_get_json", truncated)
    result = sources.fetch_flights(CONFIG)
    assert not result.ok
    assert "could not reach OpenSky" in result.error


def test_fetch_flights_survives_a_transport_error(monkeypatch):
    def unreachable(url):
        raise urllib.error.URLError("no route to host")

    monkeypatch.setattr(sources, "_get_json", unreachable)
    result = sources.fetch_flights(CONFIG)
    assert not result.ok
    assert "could not reach OpenSky" in result.error


def test_fetch_flights_survives_a_malformed_response(monkeypatch):
    # A state entry that is not a sequence makes parse_states's len(entry)
    # raise TypeError; fetch_flights must convert that into a Fetched error.
    monkeypatch.setattr(sources, "_get_json", lambda url: {"states": [123]})
    result = sources.fetch_flights(CONFIG)
    assert not result.ok
    assert "unexpected OpenSky response" in result.error


def test_fetch_flights_returns_a_snapshot_on_success(monkeypatch):
    payload = {"states": [state("4ca7b4", "RYR4TL", LON + 0.05, LAT + 0.05)]}
    monkeypatch.setattr(sources, "_get_json", lambda url: payload)
    result = sources.fetch_flights(CONFIG)
    assert result.ok
    assert result.value.place == "Lisbon"
    assert len(result.value.aircraft) == 1

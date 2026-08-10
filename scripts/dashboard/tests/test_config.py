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

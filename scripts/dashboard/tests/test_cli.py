"""Exit-code and branching coverage for dashboard.__main__.main().

No network and no hardware: fetch_forecast/fetch_news/fetch_flights and
upload are monkeypatched on the dashboard.__main__ module (the names main()
actually calls, since they were imported with `from .sources import ...` /
`from .deliver import ...`). build_epub runs for real against tmp_path so
nothing lands in build/.
"""

import datetime as dt
import json
from pathlib import Path

from dashboard import __main__ as cli
from dashboard.deliver import UploadResult, UploadStatus
from dashboard.sources import (
    CurrentConditions,
    DayForecast,
    Fetched,
    Forecast,
)

FAILED = Fetched(error="boom")

OK_FORECAST = Fetched(
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


def _write_config(tmp_path: Path) -> Path:
    path = tmp_path / "dashboard.config.local.json"
    path.write_text(
        json.dumps({"location": {"name": "Lisbon", "lat": 38.7223, "lon": -9.1393}}),
        encoding="utf-8",
    )
    return path


def _patch_sources(monkeypatch, *, forecast=FAILED, news=FAILED, flights=FAILED):
    calls = []
    monkeypatch.setattr(cli, "fetch_forecast", lambda config: (calls.append("weather"), forecast)[1])
    monkeypatch.setattr(cli, "fetch_news", lambda config: (calls.append("news"), news)[1])
    monkeypatch.setattr(cli, "fetch_flights", lambda config: (calls.append("flights"), flights)[1])
    return calls


def _args(tmp_path: Path, *extra: str) -> list[str]:
    return ["--config", str(_write_config(tmp_path)), "--out", str(tmp_path / "Dashboard.epub"), *extra]


def test_every_source_failing_with_a_successful_upload_exits_no_data(monkeypatch, tmp_path, capsys):
    _patch_sources(monkeypatch)
    monkeypatch.setattr(cli, "upload", lambda *a, **k: UploadResult(UploadStatus.UPLOADED, "ok"))

    code = cli.main(_args(tmp_path))

    assert code == cli.EXIT_NO_DATA == 1
    captured = capsys.readouterr()
    # Distinguishing side effect: all three sources warned as unavailable,
    # even though the upload itself succeeded.
    assert "warning: weather unavailable — boom" in captured.err
    assert "warning: news unavailable — boom" in captured.err
    assert "warning: flights unavailable — boom" in captured.err
    assert "uploaded to" in captured.out


def test_every_source_failing_with_no_upload_exits_no_data(monkeypatch, tmp_path, capsys):
    _patch_sources(monkeypatch)
    upload_calls = []
    monkeypatch.setattr(cli, "upload", lambda *a, **k: upload_calls.append(a) or UploadResult(UploadStatus.UPLOADED, "ok"))

    code = cli.main(_args(tmp_path, "--no-upload"))

    assert code == cli.EXIT_NO_DATA == 1
    assert upload_calls == []  # --no-upload must short-circuit before upload() is ever called
    captured = capsys.readouterr()
    assert "built" in captured.out


def test_a_successful_source_with_a_rejected_upload_exits_rejected(monkeypatch, tmp_path, capsys):
    _patch_sources(monkeypatch, forecast=OK_FORECAST)
    monkeypatch.setattr(
        cli, "upload", lambda *a, **k: UploadResult(UploadStatus.REJECTED, "HTTP 400: File already exists")
    )

    code = cli.main(_args(tmp_path))

    assert code == cli.EXIT_REJECTED == 3
    captured = capsys.readouterr()
    assert "error: device rejected the upload — HTTP 400: File already exists" in captured.err
    assert "the file is still at" in captured.err


def test_a_successful_source_with_an_unreachable_device_exits_ok_and_prints_the_curl_hint(
    monkeypatch, tmp_path, capsys
):
    _patch_sources(monkeypatch, forecast=OK_FORECAST)
    monkeypatch.setattr(
        cli, "upload", lambda *a, **k: UploadResult(UploadStatus.UNREACHABLE, "connection refused")
    )

    code = cli.main(_args(tmp_path))

    assert code == cli.EXIT_OK == 0
    captured = capsys.readouterr()
    assert "device not reachable (connection refused)" in captured.out
    expected_out = tmp_path / "Dashboard.epub"
    assert cli.curl_hint(expected_out, "almanac.local", "/Books") in captured.out


def test_a_successful_source_with_an_uploaded_result_exits_ok(monkeypatch, tmp_path, capsys):
    _patch_sources(monkeypatch, forecast=OK_FORECAST)
    monkeypatch.setattr(cli, "upload", lambda *a, **k: UploadResult(UploadStatus.UPLOADED, "ok"))

    code = cli.main(_args(tmp_path))

    assert code == cli.EXIT_OK == 0
    captured = capsys.readouterr()
    assert "uploaded to almanac.local:/Books/Dashboard.epub" in captured.out


def test_missing_config_exits_config_error_and_fetches_nothing(monkeypatch, tmp_path, capsys):
    calls = _patch_sources(monkeypatch)
    monkeypatch.setattr(cli, "upload", lambda *a, **k: (_ for _ in ()).throw(AssertionError("upload must not run")))

    code = cli.main(["--config", str(tmp_path / "does-not-exist.json"), "--out", str(tmp_path / "D.epub")])

    assert code == cli.EXIT_CONFIG == 2
    assert calls == []  # none of the three sources were ever fetched
    captured = capsys.readouterr()
    assert "error: no config at" in captured.err


def test_a_write_failure_exits_write_failed_and_does_not_upload(monkeypatch, tmp_path, capsys):
    _patch_sources(monkeypatch, forecast=OK_FORECAST)
    monkeypatch.setattr(cli, "upload", lambda *a, **k: (_ for _ in ()).throw(AssertionError("upload must not run")))

    def _raise(*_args, **_kwargs):
        raise OSError("No space left on device")

    monkeypatch.setattr(cli, "build_epub", _raise)

    code = cli.main(_args(tmp_path))

    assert code == cli.EXIT_WRITE_FAILED == 4
    captured = capsys.readouterr()
    out_path = tmp_path / "Dashboard.epub"
    assert f"error: could not write {out_path}: No space left on device" in captured.err

"""Build the dashboard EPUB and push it to the device.

Invoked through scripts/generate_dashboard_epub.py.

Exit codes: 0 success, 1 every source failed, 2 config error, 3 upload
rejected, 4 could not write the EPUB.
"""

from __future__ import annotations

import argparse
import datetime as dt
import sys
from pathlib import Path

from .config import ConfigError, load_config
from .deliver import UploadStatus, build_epub, curl_hint, make_cover_png, upload
from .render import render_flights, render_news, render_sky, render_weather
from .sources import Fetched, NewsDigest, fetch_flights, fetch_forecast, fetch_news

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CONFIG = REPO_ROOT / "scripts/dashboard.config.local.json"
DEFAULT_OUT = REPO_ROOT / "build/Dashboard.epub"  # build/ is gitignored

EXIT_OK = 0
EXIT_NO_DATA = 1
EXIT_CONFIG = 2
EXIT_REJECTED = 3
EXIT_WRITE_FAILED = 4


def parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="generate_dashboard_epub.py", description=__doc__)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG, help="config file path")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="where to write the EPUB")
    parser.add_argument("--no-upload", action="store_true", help="build only, do not upload")
    return parser.parse_args(argv)


def _news_has_content(fetched: Fetched[NewsDigest]) -> bool:
    """True when at least one feed came back without an error.

    fetch_news returns a successful NewsDigest whenever feeds are configured,
    even if every one of them failed, so `fetched.ok` alone cannot tell an
    outage from a good run.
    """
    if not fetched.ok:
        return False
    return any(feed.error is None for feed in fetched.value.feeds)


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

    news_ok = _news_has_content(news)
    news_reason = news.error
    if news.ok and not news_ok:
        # Every feed errored despite fetch_news() reporting overall success;
        # feeds is non-empty whenever news.ok is True (see _news_has_content),
        # but guard anyway rather than assume that invariant holds forever.
        news_reason = news.value.feeds[0].error if news.value.feeds else news.error

    for label, ok, reason in (
        ("weather", forecast.ok, forecast.error),
        ("news", news_ok, news_reason),
        ("flights", flights.ok, flights.error),
    ):
        if not ok:
            print(f"warning: {label} unavailable — {reason}", file=sys.stderr)

    sections = [
        ("Weather", render_weather(forecast, generated)),
        ("News", render_news(news, generated)),
        ("Sky", render_sky(forecast, generated)),
        ("Flights", render_flights(flights, generated)),
    ]

    try:
        out_path = build_epub(
            sections, make_cover_png(generated.date(), config.place), args.out, generated
        )
    except OSError as exc:
        print(f"error: could not write {args.out}: {exc}", file=sys.stderr)
        return EXIT_WRITE_FAILED
    print(f"built {out_path}")

    content_code = (
        EXIT_OK if (forecast.ok or _news_has_content(news) or flights.ok) else EXIT_NO_DATA
    )

    if args.no_upload:
        return content_code

    result = upload(out_path, config.device_host, config.device_path)
    if result.status is UploadStatus.UPLOADED:
        print(f"uploaded to {config.device_host}:{config.device_path}/{out_path.name}")
        return content_code

    if result.status is UploadStatus.REJECTED:
        print(f"error: device rejected the upload — {result.detail}", file=sys.stderr)
        print(
            f"the previous copy on the device was already removed; "
            f"your local copy is at {out_path}",
            file=sys.stderr,
        )
        return EXIT_REJECTED

    print(f"device not reachable ({result.detail})")
    print("put it into File Transfer mode, then run:")
    print(f"  {curl_hint(out_path, config.device_host, config.device_path)}")
    return content_code


if __name__ == "__main__":
    sys.exit(main())

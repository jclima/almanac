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
    moon_phase,
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
    # render_sky computes the phase for FORECAST.days[0].date (2026-08-09), not
    # for GENERATED. Compute the same value independently and assert on the
    # actual name, rather than "Moon" (a hardcoded literal in render.py's
    # f-string) or "Quarter" (a substring of half the phase names) — either of
    # which would pass no matter what moon_phase() actually returned.
    expected_phase = moon_phase(dt.date(2026, 8, 9)).name
    assert expected_phase in html


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


def test_news_renders_an_empty_feed_as_empty_not_as_failure():
    # max_per_feed=0, or a feed whose entries all lack titles, yields
    # headlines=[] with error=None. That is not a failure.
    digest = Fetched(value=NewsDigest(feeds=[FeedResult(name="Quiet", headlines=[])]))
    html = render_news(digest, GENERATED)
    assert "Quiet" in html
    assert "Unavailable" not in html
    # Assert on visible content, not the "feed-error" CSS class name — the
    # comment on test_a_failed_section_renders_the_reason above warns against
    # exactly that. A quiet feed renders as a bare, empty list: no error
    # paragraph and no headline items after its heading.
    tail = html.split("<h2>Quiet</h2>", 1)[1]
    assert "<p" not in tail
    assert "<li>" not in tail


def test_flights_renders_unknown_altitude_and_speed_as_dashes():
    # An aircraft with a known position but unknown altitude is deliberately
    # kept by parse_states rather than dropped.
    snapshot = Fetched(
        value=FlightSnapshot(
            place="Lisbon",
            radius_miles=25.0,
            aircraft=[Aircraft("NOALT1", "Ireland", None, None, 4.2, "NE")],
        )
    )
    html = render_flights(snapshot, GENERATED)
    assert "NOALT1" in html
    assert "None" not in html
    assert html.count("—") >= 2


def test_every_renderer_returns_a_fragment_not_a_document():
    news = Fetched(value=NewsDigest(feeds=[FeedResult(name="BBC", headlines=[Headline("A story", None)])]))
    flights = Fetched(
        value=FlightSnapshot(
            place="Lisbon",
            radius_miles=25.0,
            aircraft=[Aircraft("RYR4TL", "Ireland", 32808, 450, 4.2, "NE")],
        )
    )
    for html in (
        render_weather(FORECAST, GENERATED),
        render_weather(Fetched(error="x"), GENERATED),
        render_news(news, GENERATED),
        render_news(Fetched(error="x"), GENERATED),
        render_sky(FORECAST, GENERATED),
        render_sky(Fetched(error="x"), GENERATED),
        render_flights(flights, GENERATED),
        render_flights(Fetched(error="x"), GENERATED),
    ):
        assert "<html" not in html
        assert "<head" not in html
        assert "<body" not in html

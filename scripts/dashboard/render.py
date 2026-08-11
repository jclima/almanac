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

import http.client
import urllib.error

from dashboard import sources
from dashboard.config import Config, Feed
from dashboard.sources import parse_feed

RSS = """<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0"><channel>
  <title>Example</title>
  <item>
    <title>First &amp; foremost</title>
    <pubDate>Sun, 09 Aug 2026 08:00:00 GMT</pubDate>
  </item>
  <item>
    <title>Second story</title>
    <pubDate>Sun, 09 Aug 2026 07:00:00 GMT</pubDate>
  </item>
  <item>
    <title>Third story</title>
  </item>
</channel></rss>
"""


def test_parse_feed_reads_titles():
    result = parse_feed("Example", RSS, limit=5)
    assert result.error is None
    assert [h.title for h in result.headlines] == ["First & foremost", "Second story", "Third story"]


def test_parse_feed_respects_the_limit():
    result = parse_feed("Example", RSS, limit=2)
    assert len(result.headlines) == 2


def test_parse_feed_reads_publication_dates():
    result = parse_feed("Example", RSS, limit=5)
    assert result.headlines[0].published is not None
    assert result.headlines[0].published.year == 2026
    assert result.headlines[2].published is None


def test_parse_feed_keeps_the_name():
    assert parse_feed("Example", RSS, limit=5).name == "Example"


def test_parse_feed_reports_an_empty_document_as_an_error():
    result = parse_feed("Example", "not a feed at all", limit=5)
    assert result.error is not None
    assert result.headlines == []


def test_parse_feed_reports_a_feed_with_no_items():
    empty = '<?xml version="1.0"?><rss version="2.0"><channel><title>x</title></channel></rss>'
    result = parse_feed("Example", empty, limit=5)
    assert result.error is not None
    assert "no items" in result.error


def test_fetch_news_survives_a_malformed_feed_url():
    # config.py only requires feeds[].url to be truthy — a scheme-less URL like
    # "example.com/rss" passes validation but urllib.request.Request() raises
    # ValueError (not URLError/OSError) when it tries to parse it. fetch_news
    # must convert that into a FeedResult error, not propagate the exception.
    config = Config(place="x", lat=0.0, lon=0.0, feeds=[Feed(name="Bad", url="example.com/rss")])
    result = sources.fetch_news(config)
    assert result.ok
    assert len(result.value.feeds) == 1
    assert result.value.feeds[0].error is not None


def test_fetch_news_survives_a_truncated_response(monkeypatch):
    def truncated(url):
        raise http.client.IncompleteRead(b"partial")

    monkeypatch.setattr(sources, "_fetch_text", truncated)
    config = Config(place="x", lat=0.0, lon=0.0, feeds=[Feed(name="Example", url="https://example.invalid/rss")])
    result = sources.fetch_news(config)
    assert result.ok
    assert result.value.feeds[0].error is not None


def test_fetch_news_survives_a_transport_error(monkeypatch):
    def unreachable(url):
        raise urllib.error.URLError("no route to host")

    monkeypatch.setattr(sources, "_fetch_text", unreachable)
    config = Config(place="x", lat=0.0, lon=0.0, feeds=[Feed(name="Example", url="https://example.invalid/rss")])
    result = sources.fetch_news(config)
    assert result.ok
    assert "unreachable" in result.value.feeds[0].error


def test_fetch_news_reports_no_feeds_configured():
    config = Config(place="x", lat=0.0, lon=0.0, feeds=[])
    result = sources.fetch_news(config)
    assert not result.ok
    assert result.error == "no feeds configured"

import io
import json

import pytest

from dashboard import sources


class _FakeResponse(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False


def _respond_with(monkeypatch, body: str):
    monkeypatch.setattr(
        sources.urllib.request,
        "urlopen",
        lambda request, timeout=None: _FakeResponse(body.encode("utf-8")),
    )


def test_get_json_returns_a_decoded_object(monkeypatch):
    _respond_with(monkeypatch, json.dumps({"hello": "world"}))
    assert sources._get_json("https://example.invalid/x") == {"hello": "world"}


def test_get_json_rejects_a_json_array(monkeypatch):
    _respond_with(monkeypatch, json.dumps([1, 2, 3]))
    with pytest.raises(ValueError) as exc:
        sources._get_json("https://example.invalid/x")
    assert "expected a JSON object" in str(exc.value)


def test_get_json_rejects_a_non_json_body(monkeypatch):
    _respond_with(monkeypatch, "<html>captive portal</html>")
    with pytest.raises(ValueError):
        sources._get_json("https://example.invalid/x")


def test_get_json_passes_the_timeout(monkeypatch):
    seen = {}

    def capture(request, timeout=None):
        seen["timeout"] = timeout
        return _FakeResponse(b'{"ok": true}')

    monkeypatch.setattr(sources.urllib.request, "urlopen", capture)
    sources._get_json("https://example.invalid/x")
    assert seen["timeout"] == 15

import io
import urllib.error
from pathlib import Path

from dashboard.deliver import UploadStatus, curl_hint, encode_multipart, upload


def test_multipart_body_has_the_boundary_markers():
    body = encode_multipart("file", "Dashboard.epub", b"PK\x03\x04data", "XBOUNDARY")
    assert body.startswith(b"--XBOUNDARY\r\n")
    assert body.endswith(b"--XBOUNDARY--\r\n")


def test_multipart_body_declares_the_field_and_filename():
    body = encode_multipart("file", "Dashboard.epub", b"PK", "XBOUNDARY")
    assert b'name="file"' in body
    assert b'filename="Dashboard.epub"' in body


def test_multipart_body_carries_the_payload_verbatim():
    payload = b"PK\x03\x04\x00binary\xff"
    assert payload in encode_multipart("file", "D.epub", payload, "XBOUNDARY")


def test_multipart_body_sets_a_binary_content_type():
    body = encode_multipart("file", "D.epub", b"PK", "XBOUNDARY")
    assert b"Content-Type: application/epub+zip" in body


def test_curl_hint_quotes_the_url_and_names_the_file():
    hint = curl_hint(Path("/tmp/Dashboard.epub"), "almanac.local", "/Books")
    assert 'curl -X POST -F "file=@/tmp/Dashboard.epub"' in hint
    assert '"http://almanac.local/upload?path=/Books"' in hint


class _FakeResponse(io.BytesIO):
    """A urlopen()-shaped context manager wrapping an in-memory body."""

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False


def _write_fake_epub(tmp_path: Path) -> Path:
    epub = tmp_path / "Dashboard.epub"
    epub.write_bytes(b"PK\x03\x04fake-epub-bytes")
    return epub


def test_upload_deletes_the_previous_copy_before_posting_the_new_one(monkeypatch, tmp_path):
    # The device rejects an upload whose target already exists
    # (AlmanacWebServer.cpp:714), so a fixed filename like Dashboard.epub must
    # be cleared first or every run after the first would return REJECTED.
    calls = []

    def fake_urlopen(request, timeout=None):
        calls.append((request.full_url, request.get_method(), request.data))
        return _FakeResponse(b"ok")

    monkeypatch.setattr("dashboard.deliver.urllib.request.urlopen", fake_urlopen)

    result = upload(_write_fake_epub(tmp_path), "almanac.local", "/Books")

    assert result.status is UploadStatus.UPLOADED
    assert calls[0][:2] == ("http://almanac.local/delete", "POST")
    # WebServer::urlDecode (Parsing.cpp:591-611) reverses %2F back to '/'
    # before the device compares the path, so the percent-encoded body is
    # equivalent on the wire to the raw-slash form docs/webserver-endpoints.md
    # documents (`-d "path=/Books/mybook.epub"`) — confirmed by reading that
    # decoder rather than assuming it.
    assert calls[0][2] == b"path=%2FBooks%2FDashboard.epub"
    assert calls[1][:2] == ("http://almanac.local/upload?path=/Books", "POST")


def test_upload_ignores_a_delete_failure_and_still_attempts_the_upload(monkeypatch, tmp_path):
    # On the very first run there is nothing to delete yet; that must not
    # block the upload that follows.
    def fake_urlopen(request, timeout=None):
        if request.full_url.endswith("/delete"):
            raise urllib.error.URLError("nothing to delete")
        return _FakeResponse(b"stored")

    monkeypatch.setattr("dashboard.deliver.urllib.request.urlopen", fake_urlopen)

    result = upload(_write_fake_epub(tmp_path), "almanac.local", "/Books")

    assert result.status is UploadStatus.UPLOADED
    assert result.detail == "stored"


def test_upload_maps_a_device_rejection_to_rejected(monkeypatch, tmp_path):
    def fake_urlopen(request, timeout=None):
        if request.full_url.endswith("/delete"):
            return _FakeResponse(b"")
        raise urllib.error.HTTPError(
            request.full_url, 400, "Bad Request", None, io.BytesIO(b"File already exists")
        )

    monkeypatch.setattr("dashboard.deliver.urllib.request.urlopen", fake_urlopen)

    result = upload(_write_fake_epub(tmp_path), "almanac.local", "/Books")

    assert result.status is UploadStatus.REJECTED
    assert "HTTP 400" in result.detail
    assert "File already exists" in result.detail


def test_upload_maps_a_transport_error_to_unreachable(monkeypatch, tmp_path):
    def fake_urlopen(request, timeout=None):
        if request.full_url.endswith("/delete"):
            return _FakeResponse(b"")
        raise urllib.error.URLError("connection refused")

    monkeypatch.setattr("dashboard.deliver.urllib.request.urlopen", fake_urlopen)

    result = upload(_write_fake_epub(tmp_path), "almanac.local", "/Books")

    assert result.status is UploadStatus.UNREACHABLE
    assert "connection refused" in result.detail

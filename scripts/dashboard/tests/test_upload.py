from pathlib import Path

from dashboard.deliver import curl_hint, encode_multipart


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

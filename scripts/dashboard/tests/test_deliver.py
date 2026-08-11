import datetime as dt
import zipfile

from dashboard.deliver import build_epub, make_cover_png

GENERATED = dt.datetime(2026, 8, 9, 7, 30)
SECTIONS = [
    ("Weather", "<h1>Weather</h1><p>24.3</p>"),
    ("News", "<h1>News</h1><ul><li>A story</li></ul>"),
]


def test_cover_is_a_480x800_png():
    data = make_cover_png(dt.date(2026, 8, 9), "Lisbon")
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    # PNG IHDR: width and height are big-endian uint32 at offsets 16 and 20.
    assert int.from_bytes(data[16:20], "big") == 480
    assert int.from_bytes(data[20:24], "big") == 800


def test_build_epub_writes_a_zip(tmp_path):
    out = build_epub(SECTIONS, make_cover_png(dt.date(2026, 8, 9), "Lisbon"), tmp_path / "D.epub", GENERATED)
    assert out.is_file()
    assert zipfile.is_zipfile(out)


def test_build_epub_contains_one_document_per_section(tmp_path):
    out = build_epub(SECTIONS, make_cover_png(dt.date(2026, 8, 9), "Lisbon"), tmp_path / "D.epub", GENERATED)
    with zipfile.ZipFile(out) as archive:
        names = archive.namelist()
    assert any(n.endswith("sec_weather.xhtml") for n in names)
    assert any(n.endswith("sec_news.xhtml") for n in names)


def test_build_epub_embeds_the_cover(tmp_path):
    out = build_epub(SECTIONS, make_cover_png(dt.date(2026, 8, 9), "Lisbon"), tmp_path / "D.epub", GENERATED)
    with zipfile.ZipFile(out) as archive:
        names = archive.namelist()
        opf = next(n for n in names if n.endswith(".opf"))
        manifest = archive.read(opf).decode("utf-8")
    assert any(n.endswith("images/cover.png") for n in names)
    assert 'name="cover"' in manifest
    assert 'content="cover-image"' in manifest


def test_build_epub_keeps_the_nav_out_of_the_spine(tmp_path):
    out = build_epub(SECTIONS, make_cover_png(dt.date(2026, 8, 9), "Lisbon"), tmp_path / "D.epub", GENERATED)
    with zipfile.ZipFile(out) as archive:
        opf = next(n for n in archive.namelist() if n.endswith(".opf"))
        manifest = archive.read(opf).decode("utf-8")
    spine = manifest.split("<spine")[1].split("</spine>")[0]
    assert "nav" not in spine


def test_build_epub_body_content_survives(tmp_path):
    out = build_epub(SECTIONS, make_cover_png(dt.date(2026, 8, 9), "Lisbon"), tmp_path / "D.epub", GENERATED)
    with zipfile.ZipFile(out) as archive:
        page = next(n for n in archive.namelist() if n.endswith("sec_weather.xhtml"))
        content = archive.read(page).decode("utf-8")
    assert "24.3" in content


def test_build_epub_creates_missing_parent_directories(tmp_path):
    target = tmp_path / "build" / "nested" / "D.epub"
    out = build_epub(SECTIONS, make_cover_png(dt.date(2026, 8, 9), "Lisbon"), target, GENERATED)
    assert out.is_file()


def test_build_epub_links_the_cover_item_to_the_epub2_metadata(tmp_path):
    # The X4 resolves the cover by matching <meta name="cover" content="X"/>
    # against a manifest item whose id is X. Asserting only that the meta tag
    # exists cannot catch the two drifting apart, so pin both halves and the
    # EPUB 3 manifest property alongside them.
    out = build_epub(SECTIONS, make_cover_png(dt.date(2026, 8, 9), "Lisbon"), tmp_path / "D.epub", GENERATED)
    with zipfile.ZipFile(out) as archive:
        opf = next(n for n in archive.namelist() if n.endswith(".opf"))
        manifest = archive.read(opf).decode("utf-8")
    assert 'id="cover-image"' in manifest
    assert 'content="cover-image"' in manifest
    assert 'properties="cover-image"' in manifest

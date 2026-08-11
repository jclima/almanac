"""Cover generation, EPUB packaging, and delivery to the device."""

from __future__ import annotations

import datetime as dt
import http.client
import io
import re
import urllib.error
import urllib.parse
import urllib.request
import uuid
from dataclasses import dataclass
from enum import Enum
from pathlib import Path

from ebooklib import epub
from PIL import Image, ImageDraw, ImageFont

from .render import CSS

# Portrait cover matching the X4 display.
COVER_W, COVER_H = 480, 800

REPO_ROOT = Path(__file__).resolve().parents[2]
LOGO_PNG = REPO_ROOT / "src/images/Logo120.png"

# Tried in order; the first that loads wins. macOS first, then Linux, so the
# script works on either host without a bundled font.
_FONT_CANDIDATES = (
    "/System/Library/Fonts/Supplemental/Georgia Bold.ttf",
    "/System/Library/Fonts/Supplemental/Times New Roman Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
)


def _load_font(size: int) -> ImageFont.ImageFont:
    for candidate in _FONT_CANDIDATES:
        try:
            return ImageFont.truetype(candidate, size)
        except OSError:
            continue
    return ImageFont.load_default(size=size)


def _draw_centred(draw: ImageDraw.ImageDraw, y: int, text: str, font, fill) -> int:
    """Draw `text` centred horizontally at `y`. Returns the y below the text."""
    box = draw.textbbox((0, 0), text, font=font)
    draw.text(((COVER_W - (box[2] - box[0])) // 2, y), text, fill=fill, font=font)
    return y + (box[3] - box[1])


def make_cover_png(day: dt.date, place: str) -> bytes:
    """A 480x800 cover carrying the date, so the Home tile shows which day is loaded."""
    cover = Image.new("RGB", (COVER_W, COVER_H), color=(255, 255, 255))
    draw = ImageDraw.Draw(cover)

    if LOGO_PNG.is_file():
        with Image.open(LOGO_PNG) as raw:
            if raw.mode in ("RGBA", "LA", "P"):
                background = Image.new("RGB", raw.size, (255, 255, 255))
                background.paste(raw, mask=raw.convert("RGBA").split()[3])
                logo = background
            else:
                logo = raw.convert("RGB")
        logo = logo.resize((160, 160), Image.LANCZOS)
        cover.paste(logo, ((COVER_W - 160) // 2, 120))

    y = 340
    draw.line([(60, y - 24), (COVER_W - 60, y - 24)], fill=(180, 180, 180), width=1)
    y = _draw_centred(draw, y, day.strftime("%A"), _load_font(40), (0, 0, 0)) + 18
    y = _draw_centred(draw, y, day.strftime("%d %B %Y"), _load_font(34), (0, 0, 0)) + 22
    y = _draw_centred(draw, y, place, _load_font(26), (80, 80, 80)) + 24
    draw.line([(60, y), (COVER_W - 60, y)], fill=(180, 180, 180), width=1)
    _draw_centred(draw, y + 30, "Dashboard", _load_font(22), (120, 120, 120))

    buffer = io.BytesIO()
    cover.save(buffer, format="PNG")
    return buffer.getvalue()


def _make_xhtml(title: str, body: str) -> bytes:
    return (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" '
        '"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">\n'
        '<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en">\n'
        f"<head><title>{title}</title>\n"
        '<link rel="stylesheet" type="text/css" href="../style/main.css"/>\n'
        f"</head>\n<body>\n{body}\n</body>\n</html>"
    ).encode("utf-8")


def _slug(title: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-") or "section"


def build_epub(
    sections: list[tuple[str, str]],
    cover_png: bytes,
    out_path: Path,
    generated: dt.datetime,
) -> Path:
    """Package sections into an EPUB at out_path. One document per section."""
    out_path.parent.mkdir(parents=True, exist_ok=True)

    book = epub.EpubBook()
    book.set_identifier(f"almanac-dashboard-{generated:%Y%m%d%H%M}")
    book.set_title(f"Dashboard — {generated:%d %B %Y}")
    book.set_language("en")
    book.add_author("Almanac")

    style = epub.EpubItem(uid="style", file_name="style/main.css", media_type="text/css", content=CSS)
    book.add_item(style)

    # uid='cover-image' matches the EPUB 2 <meta name="cover"> value the X4 reads.
    cover_item = epub.EpubItem(
        uid="cover-image", file_name="images/cover.png", media_type="image/png", content=cover_png
    )
    cover_item.properties = ["cover-image"]
    book.add_item(cover_item)
    book.add_metadata("OPF", "meta", "", {"name": "cover", "content": "cover-image"})

    cover_page = epub.EpubHtml(title="Dashboard", file_name="cover.xhtml", lang="en")
    cover_page.content = _make_xhtml(
        "Dashboard", '<div style="text-align:center"><img src="images/cover.png" alt="Dashboard cover"/></div>'
    )
    cover_page.add_item(style)
    cover_page.add_item(cover_item)
    book.add_item(cover_page)

    pages = []
    for title, fragment in sections:
        page = epub.EpubHtml(title=title, file_name=f"sec_{_slug(title)}.xhtml", lang="en")
        page.content = _make_xhtml(title, fragment)
        page.add_item(style)
        book.add_item(page)
        pages.append(page)

    book.toc = (epub.Link("cover.xhtml", "Cover", "cover"),) + tuple(pages)
    book.add_item(epub.EpubNcx())
    book.add_item(epub.EpubNav())

    # The nav is deliberately out of the spine: the firmware locates it via the
    # manifest's properties="nav" and does not respect linear="no", so including
    # it would surface the TOC as a readable page.
    book.spine = [cover_page] + pages

    epub.write_epub(str(out_path), book)
    return out_path


UPLOAD_TIMEOUT_SECONDS = 60
UPLOAD_FIELD_NAME = "file"


class UploadStatus(str, Enum):
    UPLOADED = "uploaded"
    UNREACHABLE = "unreachable"
    REJECTED = "rejected"


@dataclass(frozen=True)
class UploadResult:
    status: UploadStatus
    detail: str


def encode_multipart(field_name: str, filename: str, payload: bytes, boundary: str) -> bytes:
    """Build a multipart/form-data body. Stdlib only — no HTTP client dependency."""
    return b"".join(
        [
            f"--{boundary}\r\n".encode(),
            f'Content-Disposition: form-data; name="{field_name}"; filename="{filename}"\r\n'.encode(),
            b"Content-Type: application/epub+zip\r\n\r\n",
            payload,
            f"\r\n--{boundary}--\r\n".encode(),
        ]
    )


def curl_hint(epub_path: Path, host: str, remote_dir: str) -> str:
    """The exact command to push the file later, for when the device is offline."""
    return (
        f'curl -X POST -F "file=@{epub_path}" '
        f'"http://{host}/upload?path={remote_dir}"'
    )


def _delete_existing(host: str, remote_path: str) -> None:
    """Remove a previous copy so the upload is not rejected as a collision.

    The device refuses an upload when the target already exists
    (AlmanacWebServer.cpp:714), so a fixed filename needs the old one cleared
    first. Deleting also clears that book's cache, which is what makes the new
    copy re-render instead of showing yesterday's page. Any failure here is
    ignored: on the first run there is simply nothing to delete.
    """
    body = urllib.parse.urlencode({"path": remote_path}).encode("utf-8")
    request = urllib.request.Request(
        f"http://{host}/delete",
        data=body,
        method="POST",
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )
    try:
        with urllib.request.urlopen(request, timeout=UPLOAD_TIMEOUT_SECONDS):
            pass
    except (urllib.error.URLError, OSError, http.client.HTTPException):
        pass  # nothing to delete, or the device will reject the upload and we report that


def upload(epub_path: Path, host: str, remote_dir: str) -> UploadResult:
    """POST the EPUB to the device. Never raises."""
    remote_path = f"{remote_dir.rstrip('/')}/{epub_path.name}"
    _delete_existing(host, remote_path)

    boundary = uuid.uuid4().hex
    body = encode_multipart(UPLOAD_FIELD_NAME, epub_path.name, epub_path.read_bytes(), boundary)
    url = f"http://{host}/upload?path={urllib.parse.quote(remote_dir)}"

    request = urllib.request.Request(
        url,
        data=body,
        method="POST",
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
    )

    try:
        with urllib.request.urlopen(request, timeout=UPLOAD_TIMEOUT_SECONDS) as response:
            return UploadResult(
                status=UploadStatus.UPLOADED,
                detail=response.read().decode("utf-8", errors="replace").strip(),
            )
    except urllib.error.HTTPError as exc:
        return UploadResult(
            status=UploadStatus.REJECTED,
            detail=f"HTTP {exc.code}: {exc.read().decode('utf-8', errors='replace').strip()}",
        )
    except (urllib.error.URLError, OSError, http.client.HTTPException) as exc:
        return UploadResult(status=UploadStatus.UNREACHABLE, detail=str(exc))

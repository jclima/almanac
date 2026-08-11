# Mini Dashboard — carried follow-ups

Date: 2026-08-10
Source: the eight task reviews and the final whole-branch review of
[2026-08-09-mini-dashboard.md](./2026-08-09-mini-dashboard.md).

Every item below was found by review, judged **Minor**, and deliberately carried
rather than fixed. The final whole-branch review triaged the list explicitly and
promoted two to must-fix; those are done and are not repeated here. Nothing in
this file blocks anything — it exists so the list does not evaporate with the
scratch workspace it was recorded in.

Ordered roughly by how likely each is to bite.

## Behaviour

| Item | Where | Why it was carried |
|---|---|---|
| `_delete_existing` reuses `UPLOAD_TIMEOUT_SECONDS` (60s), so a routable host that blackholes packets costs two 60s waits (~120s) before the `curl` hint prints, not one | [deliver.py:208](../../../scripts/dashboard/deliver.py) | Invisible to the tests (all monkeypatch `urlopen`) and to the live run (`almanac.local` fails DNS almost instantly). A shorter delete timeout fixes it |
| A failed upload leaves the device with no dashboard, since the delete runs first | [deliver.py:217](../../../scripts/dashboard/deliver.py) | Net risk reduction versus the old guaranteed-rejection behaviour; the local copy always survives. Now documented in `docs/mini-dashboard.md` |
| `curl_hint` does not percent-encode `remote_dir` while `upload()` does | [deliver.py](../../../scripts/dashboard/deliver.py) | Identical for the default `/Books`; diverges only for a `device.path` containing spaces |
| `news.max_per_feed` rejects a whole JSON float such as `3.0` | [config.py](../../../scripts/dashboard/config.py) | Literal reading of "non-negative integer". Only reachable by hand-editing the config |
| A wholly-empty feed renders `<ul></ul>`, which XHTML 1.1 says needs at least one `<li>` | [render.py:98](../../../scripts/dashboard/render.py) | Validity only, no device impact: the firmware's `ChapterHtmlSlimParser` tracks `li` and never references `ul`, so it renders as nothing |
| Three numeric interpolations bypass `html.escape()` | [render.py](../../../scripts/dashboard/render.py) | Typed `int`/`float` with no feed-reachable path, so no injection risk — a literal gap against the stated rule, not a live defect |
| `_entry_published` can raise on an out-of-range time struct | [sources.py](../../../scripts/dashboard/sources.py) | Unreachable through `feedparser`'s real parse path (every date handler caps the year or is wrapped in a guarded `except`), and `fetch_news` catches it regardless. The never-raise docstring currently leans on an undocumented `feedparser` invariant |
| `ebooklib.epub.write_epub` writes straight to the destination — no temp-file-and-rename | [deliver.py](../../../scripts/dashboard/deliver.py) | Inherited from `ebooklib`. An interrupted write leaves a truncated file at the output path |
| `_draw_centred` clips a long `location.name` off the left edge | [deliver.py](../../../scripts/dashboard/deliver.py) | Cosmetic; the cover stays exactly 480×800 |
| `_slug()` has no collision guard | [deliver.py](../../../scripts/dashboard/deliver.py) | Cannot collide with the fixed Weather/News/Sky/Flights set. Only matters if section titles ever become dynamic |
| The `deliver.py` module docstring says "and delivery to the device", written before the delivery code existed | [deliver.py](../../../scripts/dashboard/deliver.py) | Now accurate — kept here only because it was logged before Task 8 landed |

## Test coverage

| Item | Where | Why it was carried |
|---|---|---|
| Both bearing tests use `lat1=0`, which zeroes the `sin(phi1)` cross-term, so a bug in that term would ship undetected | [test_geo.py](../../../scripts/dashboard/tests/test_geo.py) | No oblique or non-equatorial bearing case exists. The distance and bounding-box constants **are** now tightly pinned |

## Still unverified on hardware

The plan's own "Manual verification" section is the authority. Nothing in this
branch has been run against a real X4 — in particular the `POST /delete` →
`POST /upload` round-trip that fixes the upload-collision bug was verified by
reading the firmware and by mocked tests, not by uploading to a device.

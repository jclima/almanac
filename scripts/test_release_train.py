#!/usr/bin/env python3
"""Plain-python tests for release_train.py. No framework: run with python3."""

import sys

from release_train import (
    bump_platformio,
    bump_readme,
    group_commits,
    latest_tag,
    next_version,
    parse_version,
    render_notes,
)

FAILURES = []


def check(label, got, want):
    if got != want:
        FAILURES.append(f"{label}: got {got!r}, want {want!r}")


def main():
    check("parse almanac tag", parse_version("almanac-v1.2.3"), (1, 2, 3))
    check("parse bare version", parse_version("1.2.3"), (1, 2, 3))
    check("reject inherited tag", parse_version("v1.5.0"), None)
    check("reject junk", parse_version("almanac-vX.Y.Z"), None)

    # The bug this test exists for: lexical sorting puts 1.0.10 before 1.0.9.
    tags = ["almanac-v1.0.9", "almanac-v1.0.10", "almanac-v1.0.2", "v1.5.0", "0.4.0"]
    check("latest is numeric not lexical", latest_tag(tags), "almanac-v1.0.10")
    check("ignores inherited tags", latest_tag(["v1.5.0", "almanac-v1.0.0"]), "almanac-v1.0.0")
    check("no almanac tags", latest_tag(["v1.5.0"]), None)

    check("patch bump", next_version((1, 0, 2), "patch"), "1.0.3")
    check("minor bump resets patch", next_version((1, 0, 2), "minor"), "1.1.0")
    check("major bump resets both", next_version((1, 2, 3), "major"), "2.0.0")
    check("patch across ten", next_version((1, 0, 9), "patch"), "1.0.10")

    ini = "[almanac]\nversion = 1.0.2\n\n[base]\nversion = keep-me\n"
    check(
        "bumps only the almanac version",
        bump_platformio(ini, "1.0.3"),
        "[almanac]\nversion = 1.0.3\n\n[base]\nversion = keep-me\n",
    )

    readme = "# Almanac\n\n**Version 1.0.2** — Almanac's own numbering,\nrestarted at 1.0.0 rather than\n"
    check(
        "bumps the readme marker only",
        bump_readme(readme, "1.0.3"),
        "# Almanac\n\n**Version 1.0.3** — Almanac's own numbering,\nrestarted at 1.0.0 rather than\n",
    )

    commits = [
        "feat: add radar view",
        "fix: stop the list drawing off-screen",
        "perf: cut a redraw",
        "chore: bump a pin",
        "Merge pull request #13 from somewhere",
        "docs: fix a typo",
        "release: 1.0.2",
        "Tidy up the loader",
    ]
    groups = group_commits(commits)
    check("feat grouped", groups["feat"], ["add radar view"])
    check("fix grouped", groups["fix"], ["stop the list drawing off-screen"])
    check("perf grouped", groups["perf"], ["cut a redraw"])
    # Non-conventional subjects are KEPT, not dropped: the real log contains
    # them, and dropping them makes the notes under-report what shipped.
    check("other keeps chore, docs and non-conventional",
          groups["other"], ["bump a pin", "fix a typo", "Tidy up the loader"])
    check("merges excluded", any("Merge pull" in c for group in groups.values() for c in group), False)
    # The previous release's own bump commit is not news in this release.
    check("release commits excluded", any("1.0.2" in c for group in groups.values() for c in group), False)

    notes = render_notes("1.0.3", groups)
    check("notes name the version", notes.splitlines()[0], "# Almanac v1.0.3")
    check("notes surface feat", "add radar view" in notes, True)
    check("notes flag they are generated", "generated from the commit log" in notes, True)

    for line in FAILURES:
        print("FAIL", line)
    print(f"{'FAILED' if FAILURES else 'ok'}: {len(FAILURES)} failure(s)")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())

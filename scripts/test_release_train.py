#!/usr/bin/env python3
"""Plain-python tests for release_train.py. No framework: run with python3."""

import sys

from release_train import latest_tag, next_version, parse_version

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

    for line in FAILURES:
        print("FAIL", line)
    print(f"{'FAILED' if FAILURES else 'ok'}: {len(FAILURES)} failure(s)")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())

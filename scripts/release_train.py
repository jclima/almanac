#!/usr/bin/env python3
"""Release train: compute the next version, bump it, and draft release notes.

Decision logic lives here rather than in the workflow YAML so it can be tested
on the host -- see test_release_train.py. The workflow is a thin caller.
"""

import argparse
import os
import re
import subprocess
import sys

TAG_PREFIX = "almanac-v"
# Ours only. The repository also carries CrossPoint's inherited tags
# (0.4.0 - 1.5.0), which must never be treated as a release of this fork.
TAG_RE = re.compile(rf"^(?:{re.escape(TAG_PREFIX)})?(\d+)\.(\d+)\.(\d+)$")


def parse_version(tag):
    """(major, minor, patch) for one of our tags, else None."""
    match = TAG_RE.match(tag.strip())
    if not match:
        return None
    return tuple(int(part) for part in match.groups())


def latest_tag(tags):
    """Newest almanac-v* tag by NUMERIC semver, or None.

    Numeric, not lexical: sorted() on the strings puts almanac-v1.0.10 before
    almanac-v1.0.9, which would silently re-release an older version.
    """
    ours = [(parse_version(t), t) for t in tags if t.startswith(TAG_PREFIX)]
    ours = [(v, t) for v, t in ours if v is not None]
    if not ours:
        return None
    return max(ours)[1]


def next_version(current, bump):
    major, minor, patch = current
    if bump == "major":
        return f"{major + 1}.0.0"
    if bump == "minor":
        return f"{major}.{minor + 1}.0"
    if bump == "patch":
        return f"{major}.{minor}.{patch + 1}"
    raise ValueError(f"unknown bump: {bump!r}")

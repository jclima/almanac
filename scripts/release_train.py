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


# Only the [almanac] section's version. platformio.ini has other `version =`
# keys (platform pins), and rewriting one of those would change the build.
PIO_VERSION_RE = re.compile(r"(?m)^(\[almanac\]\n(?:(?!\[).*\n)*?version[ \t]*=[ \t]*)(\S+)")
README_VERSION_RE = re.compile(r"(?m)^\*\*Version \d+\.\d+\.\d+\*\*")

CONVENTIONAL_RE = re.compile(r"^(feat|fix|perf|refactor|docs|test|chore|style|ci|build)(?:\([^)]*\))?!?:\s*(.+)$")

HEADINGS = [("feat", "New"), ("fix", "Fixed"), ("perf", "Performance")]


def bump_platformio(text, version):
    new, count = PIO_VERSION_RE.subn(lambda m: m.group(1) + version, text, count=1)
    if count != 1:
        raise ValueError("could not find [almanac] version in platformio.ini")
    return new


def bump_readme(text, version):
    new, count = README_VERSION_RE.subn(f"**Version {version}**", text, count=1)
    if count != 1:
        raise ValueError("could not find a '**Version X.Y.Z**' marker in README.md")
    return new


def group_commits(lines):
    groups = {"feat": [], "fix": [], "perf": [], "other": []}
    for line in lines:
        subject = line.strip()
        # Merge commits describe nothing of their own and would duplicate the
        # branch commits underneath them.
        if not subject or subject.startswith("Merge "):
            continue
        # A previous release's own bump commit is not news in this release.
        if subject.startswith("release:"):
            continue
        match = CONVENTIONAL_RE.match(subject)
        if match:
            kind, description = match.group(1), match.group(2)
            groups[kind if kind in groups else "other"].append(description)
        else:
            # Keep it rather than drop it. A subject that does not follow the
            # convention still describes something that shipped, and silently
            # omitting it makes the notes quietly under-report the release.
            groups["other"].append(subject)
    return groups


def render_notes(version, groups):
    out = [f"# Almanac v{version}", ""]
    out.append("These notes are generated from the commit log. Edit this file to")
    out.append("say what the release means; the published release keeps whatever")
    out.append("was committed at tag time.")
    out.append("")
    for key, heading in HEADINGS:
        if groups.get(key):
            out.append(f"## {heading}")
            out.append("")
            out.extend(f"- {item}" for item in groups[key])
            out.append("")
    if groups.get("other"):
        out.append("## Also")
        out.append("")
        out.extend(f"- {item}" for item in groups["other"])
        out.append("")
    return "\n".join(out).rstrip() + "\n"

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


def git(*args):
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True).stdout


def main(argv=None):
    parser = argparse.ArgumentParser(description="Cut a release: bump, draft notes, stage.")
    parser.add_argument("--bump", choices=["major", "minor", "patch"], required=True)
    parser.add_argument("--dry-run", action="store_true", help="print the plan, write nothing")
    args = parser.parse_args(argv)

    tags = git("tag", "--list", f"{TAG_PREFIX}*").split()
    previous = latest_tag(tags)
    if previous is None:
        print("error: no almanac-v* tag found; cannot compute the next version", file=sys.stderr)
        return 1

    version = next_version(parse_version(previous), args.bump)
    tag = f"{TAG_PREFIX}{version}"
    if tag in tags:
        print(f"error: {tag} already exists", file=sys.stderr)
        return 1

    # platformio.ini must already agree with the last tag. If it does not, a
    # previous release was left half-done and bumping from the tag would
    # produce a version that skips or repeats one.
    ini_text = open("platformio.ini").read()
    ini_version = PIO_VERSION_RE.search(ini_text)
    if ini_version is None or ini_version.group(2) != previous[len(TAG_PREFIX):]:
        found = ini_version.group(2) if ini_version else "<none>"
        print(
            f"error: platformio.ini says {found} but the last tag is {previous}; "
            "resolve that before releasing",
            file=sys.stderr,
        )
        return 1

    subjects = git("log", "--no-merges", "--format=%s", f"{previous}..HEAD").splitlines()
    notes_path = os.path.join("docs", "release-notes", f"{tag}.md")
    notes_exist = os.path.exists(notes_path)

    print(f"previous: {previous}")
    print(f"next:     {tag}")
    print(f"commits:  {len(subjects)}")
    print(f"notes:    {'keeping existing ' + notes_path if notes_exist else 'drafting ' + notes_path}")

    if args.dry_run:
        print("dry run: nothing written")
        return 0

    open("platformio.ini", "w").write(bump_platformio(ini_text, version))
    readme_text = open("README.md").read()
    open("README.md", "w").write(bump_readme(readme_text, version))
    # Hand-written notes always win. This is what lets a release that deserves
    # real prose get it, without the button needing a second step.
    if not notes_exist:
        os.makedirs(os.path.dirname(notes_path), exist_ok=True)
        open(notes_path, "w").write(render_notes(version, group_commits(subjects)))

    github_output = os.environ.get("GITHUB_OUTPUT")
    if github_output:
        with open(github_output, "a") as handle:
            handle.write(f"version={version}\ntag={tag}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())

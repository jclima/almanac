# Release Train Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One `workflow_dispatch` button that turns a green `develop` into a published release, by computing the next version, bumping it, drafting notes, committing, tagging and pushing.

**Architecture:** All decision logic lives in `scripts/release_train.py` — pure functions, testable on the host with no framework. `.github/workflows/release-train.yml` is a thin shell: it enforces two gates, calls the script, then commits, tags and pushes. The existing `release.yml` publishes from the tag and is not touched.

**Tech Stack:** Python 3 (standard library only), GitHub Actions, `gh` CLI (preinstalled on `ubuntu-latest`).

**Spec:** [docs/superpowers/specs/2026-08-09-release-train-design.md](../specs/2026-08-09-release-train-design.md)

## Global Constraints

- Work in the worktree `/Users/jclima/opt/flightreader/.claude/worktrees/release-train` on branch `feature/release-train`. Do **not** touch `/Users/jclima/opt/flightreader` — other sessions are active in it.
- **No new Python dependencies.** `scripts/release_train.py` and its test use the standard library only. The repo has no pytest; the test is a plain script run with `python3`.
- **Do not modify** `.github/workflows/release.yml`, `release_candidate.yml`, or `ci.yml`.
- **Do not rename or move** the `firmware.bin` release asset or anything else `OtaUpdater` parses — `release.yml` owns that and stays untouched.
- Tags are `almanac-v<major>.<minor>.<patch>`. The repo also carries CrossPoint's inherited tags (`0.4.0`–`1.5.0`); only `almanac-v*` tags are ours.
- `[almanac] version` in `platformio.ini` must always equal the tag being released, minus the `almanac-v` prefix. `release.yml` fails the release if they disagree.
- Commit messages: `<type>: <summary>` with types `feat`/`fix`/`refactor`/`docs`/`test`/`chore`/`perf`.
- Python: run as `python3`. The formatter (`bin/clang-format-fix`) covers C/C++ only and does not apply to these files.
- Push only to the `fork` remote, never `origin` (upstream CrossPoint). Do not push without asking.

---

### Task 1: Preflight — confirm the one thing that cannot be automated

**Files:** none modified.

**Interfaces:**
- Consumes: nothing.
- Produces: a verified prerequisite. Every later task is inert without it.

- [ ] **Step 1: Confirm the worktree and branch**

```bash
cd /Users/jclima/opt/flightreader/.claude/worktrees/release-train
git status --short --branch
```

Expected: `## feature/release-train` and a clean tree.

- [ ] **Step 2: Check whether the release PAT secret exists**

```bash
gh secret list --repo jclima/almanac
```

Expected: a secret named `RELEASE_TRAIN_TOKEN`.

**If it is absent, that is not a blocker for building — it is a blocker for
running.** Every later task can be built and reviewed without it. Record its
absence in your report and continue; do NOT attempt to create it, and do NOT
invent a workaround using `GITHUB_TOKEN`.

The reason it is required is in the spec: GitHub suppresses workflow triggers
for events created with the default `GITHUB_TOKEN`, so a tag pushed with it
would never start `release.yml` — the tag would exist, nothing would publish,
and the failure would look like success.

For the human, the one-time setup is: create a fine-grained PAT scoped to
`jclima/almanac` with **Contents: Read and write**, then
`gh secret set RELEASE_TRAIN_TOKEN --repo jclima/almanac`.

- [ ] **Step 3: Record the current state the workflow will act on**

```bash
git describe --tags --match 'almanac-v*' --abbrev=0
sed -n 's/^version[[:space:]]*=[[:space:]]*//p' platformio.ini | head -1
grep -n '^\*\*Version' README.md
```

Expected: tag `almanac-v1.0.2`, `platformio.ini` version `1.0.2`, and a README
line beginning `**Version 1.0.2** —`. These three agreeing is the invariant the
script maintains. Report them; later tasks assert against these exact shapes.

---

### Task 2: Version resolution

**Files:**
- Create: `scripts/release_train.py`
- Create: `scripts/test_release_train.py`

**Interfaces:**
- Consumes: nothing.
- Produces, in `scripts/release_train.py`:
  - `parse_version(tag: str) -> tuple[int, int, int] | None` — accepts `almanac-v1.2.3` or `1.2.3`, returns `None` for anything else
  - `latest_tag(tags: list[str]) -> str | None` — newest `almanac-v*` by numeric semver
  - `next_version(current: tuple[int, int, int], bump: str) -> str` — `bump` is `major`/`minor`/`patch`; returns `"X.Y.Z"`

- [ ] **Step 1: Write the failing test**

Create `scripts/test_release_train.py`:

```python
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
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cd /Users/jclima/opt/flightreader/.claude/worktrees/release-train/scripts
python3 test_release_train.py
```

Expected: `ModuleNotFoundError: No module named 'release_train'`. That is the
correct failure — it proves the test reaches the not-yet-written module.

- [ ] **Step 3: Write the implementation**

Create `scripts/release_train.py`:

```python
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
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cd /Users/jclima/opt/flightreader/.claude/worktrees/release-train/scripts
python3 test_release_train.py
```

Expected: `ok: 0 failure(s)`, exit status 0.

- [ ] **Step 5: Commit**

```bash
cd /Users/jclima/opt/flightreader/.claude/worktrees/release-train
git add scripts/release_train.py scripts/test_release_train.py
git commit -m "feat: add release train version resolution

Numeric semver ordering, not lexical: sorted() on tag strings puts
almanac-v1.0.10 before almanac-v1.0.9, which would silently re-release
an older version. Tested explicitly.

Inherited CrossPoint tags (0.4.0 - 1.5.0) are rejected, so only this
fork's own releases can ever be treated as the previous version."
```

---

### Task 3: File bumping and release-notes drafting

**Files:**
- Modify: `scripts/release_train.py`
- Modify: `scripts/test_release_train.py`

**Interfaces:**
- Consumes: `parse_version`, `latest_tag`, `next_version` from Task 2.
- Produces, in `scripts/release_train.py`:
  - `bump_platformio(text: str, version: str) -> str` — rewrites the `version = X.Y.Z` line under `[almanac]`
  - `bump_readme(text: str, version: str) -> str` — rewrites the leading `**Version X.Y.Z**` marker only
  - `group_commits(lines: list[str]) -> dict[str, list[str]]` — keys `feat`/`fix`/`perf`/`other`
  - `render_notes(version: str, groups: dict) -> str` — the markdown body

- [ ] **Step 1: Write the failing tests**

Append to `scripts/test_release_train.py`, above `main`'s `for line in FAILURES:` loop — add these calls inside `main()` after the existing `check(...)` calls, and extend the import at the top to:

```python
from release_train import (
    bump_platformio,
    bump_readme,
    group_commits,
    latest_tag,
    next_version,
    parse_version,
    render_notes,
)
```

New checks inside `main()`:

```python
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
```

- [ ] **Step 2: Run to verify it fails**

```bash
cd /Users/jclima/opt/flightreader/.claude/worktrees/release-train/scripts
python3 test_release_train.py
```

Expected: `ImportError: cannot import name 'bump_platformio'`.

- [ ] **Step 3: Implement**

Append to `scripts/release_train.py`:

```python
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
```

- [ ] **Step 4: Run to verify it passes**

```bash
cd /Users/jclima/opt/flightreader/.claude/worktrees/release-train/scripts
python3 test_release_train.py
```

Expected: `ok: 0 failure(s)`.

- [ ] **Step 5: Verify the regexes against the REAL files, not just fixtures**

```bash
cd /Users/jclima/opt/flightreader/.claude/worktrees/release-train
python3 - <<'PY'
import sys
sys.path.insert(0, "scripts")
from release_train import bump_platformio, bump_readme
ini = open("platformio.ini").read()
readme = open("README.md").read()
out_ini = bump_platformio(ini, "9.9.9")
out_readme = bump_readme(readme, "9.9.9")
assert "version = 9.9.9" in out_ini, "platformio bump did not apply"
assert out_ini.count("9.9.9") == 1, f"bumped too much: {out_ini.count('9.9.9')} occurrences"
assert "**Version 9.9.9**" in out_readme, "readme bump did not apply"
assert out_readme.count("9.9.9") == 1, "readme bumped too much"
print("ok: both regexes match the real files and change exactly one thing each")
PY
```

Expected: the `ok:` line. This catches a fixture that agrees with the code but
not with the repository — the failure mode a unit test alone cannot see.

- [ ] **Step 6: Commit**

```bash
git add scripts/release_train.py scripts/test_release_train.py
git commit -m "feat: add release train file bumping and notes drafting

The platformio.ini regex is anchored to the [almanac] section on
purpose: the file has other 'version =' keys for platform pins, and
rewriting one of those would silently change what the firmware builds
against.

Verified both regexes against the real platformio.ini and README, not
just fixtures, asserting each changes exactly one occurrence."
```

---

### Task 4: The CLI entry point

**Files:**
- Modify: `scripts/release_train.py`

**Interfaces:**
- Consumes: everything from Tasks 2 and 3.
- Produces: a CLI. `python3 scripts/release_train.py --bump patch [--dry-run]` run from the repo root. Writes `version=<X.Y.Z>` and `tag=almanac-v<X.Y.Z>` to `$GITHUB_OUTPUT` when that variable is set.

- [ ] **Step 1: Implement `main`**

Append to `scripts/release_train.py`:

```python
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
```

- [ ] **Step 2: Dry-run it against the real repository**

```bash
cd /Users/jclima/opt/flightreader/.claude/worktrees/release-train
python3 scripts/release_train.py --bump patch --dry-run
```

Expected: `previous: almanac-v1.0.2`, `next: almanac-v1.0.3`, a commit count
greater than zero, a `notes: drafting docs/release-notes/almanac-v1.0.3.md`
line, and `dry run: nothing written`.

- [ ] **Step 3: Confirm the dry run really wrote nothing**

```bash
git status --porcelain
```

Expected: empty. A dry run that dirties the tree is a failed dry run.

- [ ] **Step 4: Confirm the half-done-release guard fires**

```bash
python3 - <<'PY'
import pathlib, subprocess
p = pathlib.Path("platformio.ini")
original = p.read_text()
p.write_text(original.replace("version = 1.0.2", "version = 1.0.7", 1))
try:
    r = subprocess.run(["python3", "scripts/release_train.py", "--bump", "patch", "--dry-run"],
                       capture_output=True, text=True)
    print("exit:", r.returncode)
    print(r.stderr.strip())
    assert r.returncode == 1, "guard did not fire"
    assert "resolve that before releasing" in r.stderr
    print("ok: mismatch between platformio.ini and the last tag is refused")
finally:
    p.write_text(original)
PY
git status --porcelain
```

Expected: `exit: 1`, the explanatory error, `ok:` line, and a clean tree
afterwards.

- [ ] **Step 5: Commit**

```bash
git add scripts/release_train.py
git commit -m "feat: add the release train CLI

Refuses to run when platformio.ini disagrees with the last tag: that
means a previous release was left half-done, and bumping from the tag
would skip or repeat a version.

An existing docs/release-notes/<tag>.md is never overwritten, so a
release that deserves hand-written prose can have it without the button
needing a second step."
```

---

### Task 5: The workflow

**Files:**
- Create: `.github/workflows/release-train.yml`

**Interfaces:**
- Consumes: `scripts/release_train.py` (Task 4), which writes `version` and `tag` to `$GITHUB_OUTPUT`.
- Produces: a pushed tag `almanac-v<X.Y.Z>`, which starts the existing `release.yml`.

- [ ] **Step 1: Write the workflow**

Create `.github/workflows/release-train.yml`:

```yaml
name: Release Train

# Manual only, by design. A tag here is not just a published page: release.yml
# attaches firmware.bin and OtaUpdater offers it to every device in the field.
on:
  workflow_dispatch:
    inputs:
      bump:
        description: Which version component to increment
        type: choice
        options: [patch, minor, major]
        default: patch
      allow_docs_only:
        description: Release even if only docs changed since the last tag
        type: boolean
        default: false
      dry_run:
        description: Run the gates and print the plan, but push nothing
        type: boolean
        default: false

permissions:
  contents: write # read the repo and the check runs for the gate
  checks: read

jobs:
  cut-release:
    runs-on: ubuntu-latest
    steps:
      # The push must NOT use GITHUB_TOKEN: GitHub suppresses workflow
      # triggers for events it creates, so release.yml would never fire and
      # the tag would sit there publishing nothing. Fail here rather than
      # discover that after tagging.
      - name: Require the release token
        env:
          RELEASE_TRAIN_TOKEN: ${{ secrets.RELEASE_TRAIN_TOKEN }}
        run: |
          if [ -z "${RELEASE_TRAIN_TOKEN}" ]; then
            echo "::error::RELEASE_TRAIN_TOKEN is not set. A tag pushed with GITHUB_TOKEN does not start release.yml."
            exit 1
          fi

      - uses: actions/checkout@v6
        with:
          fetch-depth: 0 # tags and full history: the script needs both
          token: ${{ secrets.RELEASE_TRAIN_TOKEN }}

      - name: Gate A - CI is green on this exact commit
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          set -euo pipefail
          sha="$(git rev-parse HEAD)"
          runs="$(gh api "repos/${GITHUB_REPOSITORY}/commits/${sha}/check-runs" --jq '.check_runs[] | "\(.name)=\(.status):\(.conclusion)"')"
          if [ -z "${runs}" ]; then
            echo "::error::No check runs found for ${sha}. CI has probably not started; refusing to release."
            exit 1
          fi
          echo "${runs}"
          if echo "${runs}" | grep -qv '=completed:success$'; then
            echo "::error::Not every check on ${sha} completed successfully."
            exit 1
          fi

      - name: Gate B - something other than docs changed
        run: |
          set -euo pipefail
          previous="$(git tag --list 'almanac-v*' | sort -V | tail -1)"
          changed="$(git diff --name-only "${previous}..HEAD")"
          echo "${changed}"
          if [ -z "${changed}" ]; then
            echo "::error::Nothing changed since ${previous}."
            exit 1
          fi
          if ! echo "${changed}" | grep -qvE '^(docs/|.*\.md$)'; then
            if [ "${{ inputs.allow_docs_only }}" != "true" ]; then
              echo "::error::Only docs changed since ${previous}. Re-run with allow_docs_only to release anyway."
              exit 1
            fi
            echo "::warning::Docs-only release, allowed by input."
          fi

      - name: Bump, draft notes, and stage
        id: train
        run: |
          python3 scripts/release_train.py --bump "${{ inputs.bump }}" ${{ inputs.dry_run && '--dry-run' || '' }}

      - name: Commit, tag and push
        if: ${{ inputs.dry_run != true }}
        run: |
          set -euo pipefail
          git config user.name "github-actions[bot]"
          git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
          git add platformio.ini README.md docs/release-notes
          git commit -m "release: ${{ steps.train.outputs.version }}"
          git tag "${{ steps.train.outputs.tag }}"
          # Commit and tag together: if this fails because another push landed
          # first, no tag reaches the remote and re-running is safe.
          git push origin HEAD "${{ steps.train.outputs.tag }}"

      - name: Remind about the README prose
        if: ${{ inputs.dry_run != true }}
        run: |
          {
            echo "### Released ${{ steps.train.outputs.tag }}"
            echo
            echo "The README's version line was bumped automatically."
            echo "Its prose paragraph still describes the previous release - edit it if this one needs different upgrade guidance."
          } >> "$GITHUB_STEP_SUMMARY"
```

- [ ] **Step 2: Validate the YAML parses**

```bash
cd /Users/jclima/opt/flightreader/.claude/worktrees/release-train
python3 -c "
import sys
try:
    import yaml
except ImportError:
    sys.exit('PyYAML unavailable; skip and rely on GitHub validation')
d = yaml.safe_load(open('.github/workflows/release-train.yml'))
assert 'workflow_dispatch' in d[True], 'trigger missing'
steps = d['jobs']['cut-release']['steps']
print('steps:', [s.get('name', s.get('uses')) for s in steps])
assert d['permissions'] == {'contents': 'write', 'checks': 'read'}
print('ok: workflow parses and declares the expected permissions')
"
```

Expected: the step list and the `ok:` line. If PyYAML is unavailable, say so in
your report rather than skipping silently.

- [ ] **Step 3: Confirm the untouched workflows really are untouched**

```bash
git status --porcelain .github/workflows/
```

Expected: only `?? .github/workflows/release-train.yml`. If `release.yml`,
`ci.yml` or `release_candidate.yml` appear, revert them — they are explicitly
out of scope.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/release-train.yml
git commit -m "feat: add the release train workflow

Manual workflow_dispatch only. Two gates before anything is written:
every check run on the exact commit must have completed successfully
(no check runs at all is treated as failure, not success, because the
usual reason is that CI has not started), and something other than docs
must have changed since the last tag.

Pushes with RELEASE_TRAIN_TOKEN rather than GITHUB_TOKEN, and refuses to
start without it: GitHub suppresses workflow triggers for events made
with GITHUB_TOKEN, so a tag pushed with it would never start
release.yml and the release would silently never publish."
```

---

### Task 6: Documentation and handoff

**Files:**
- Modify: `docs/superpowers/specs/2026-08-09-release-train-design.md` (status line only)
- Create or modify: the repo's release documentation — find it first (Step 1)

**Interfaces:**
- Consumes: Tasks 1-5.
- Produces: no code. A written record of how to run the train and what to do when a gate refuses.

- [ ] **Step 1: Find where release process is already documented**

```bash
cd /Users/jclima/opt/flightreader/.claude/worktrees/release-train
grep -rln "almanac-v\|release notes\|Cutting a release" --include="*.md" docs/ README.md CONTRIBUTING.md 2>/dev/null | head
```

Document the train wherever releases are already described. If nothing covers
it, add a short `## Cutting a release` section to `README.md` immediately after
the version block. Do NOT create a new top-level doc for this.

- [ ] **Step 2: Write the section**

It must state, in this order: run the **Release Train** workflow from the
Actions tab and pick `patch`/`minor`/`major`; `dry_run` first if unsure; the
two gates and what each refusal means; that `allow_docs_only` exists and why it
usually should not be used; that hand-writing
`docs/release-notes/almanac-vX.Y.Z.md` before running keeps your prose instead
of a generated draft; and that the README's prose paragraph is not automated.

Also state the one-time prerequisite: a fine-grained PAT with Contents:
Read and write, stored as `RELEASE_TRAIN_TOKEN`, and that the workflow refuses
to run without it.

- [ ] **Step 3: Mark the spec implemented**

Change the spec's `Status: Approved` line to `Status: Implemented`.

- [ ] **Step 4: Commit**

```bash
git add -A docs README.md
git status --porcelain
git commit -m "docs: document the release train

Records the two gates and what a refusal means, the allow_docs_only
escape hatch, that a hand-written notes file is preferred over the
generated draft, and the RELEASE_TRAIN_TOKEN prerequisite without which
the workflow refuses to start."
```

---

### Task 7: Verification

**Files:** none modified.

**Interfaces:**
- Consumes: Tasks 1-6.
- Produces: the evidence and the list of what only a real run can prove.

- [ ] **Step 1: Unit tests pass**

```bash
cd /Users/jclima/opt/flightreader/.claude/worktrees/release-train/scripts
python3 test_release_train.py
```

Expected: `ok: 0 failure(s)`.

- [ ] **Step 2: Dry run leaves the tree clean**

```bash
cd /Users/jclima/opt/flightreader/.claude/worktrees/release-train
python3 scripts/release_train.py --bump minor --dry-run
git status --porcelain
```

Expected: a plan naming `almanac-v1.1.0`, then an empty status.

- [ ] **Step 3: The firmware is untouched**

```bash
git diff --name-only develop..HEAD
```

Expected: only `scripts/`, `.github/workflows/release-train.yml`, and `docs/`
(plus `README.md` if Step 1 of Task 6 put the section there). **No file under
`src/` or `lib/` may appear** — this feature changes no firmware, so the
firmware build is not a gate for it.

- [ ] **Step 4: Hand off what cannot be verified here**

Report these as unverified; do not claim them:

- 🔲 The workflow has never run. Gate A's `gh api` call, the `$GITHUB_OUTPUT`
  handoff between steps, and the `inputs.dry_run && '--dry-run' || ''`
  expression are only exercised on GitHub.
- 🔲 `RELEASE_TRAIN_TOKEN` may not exist yet (Task 1). Without it the workflow
  refuses to start — by design, but it means the first run may fail at step one.
- 🔲 The first real cut is the true end-to-end test: it must produce a tag,
  and `release.yml` must then publish a release with `firmware.bin` attached.
  Watch that the release actually appears; a tag with no release is the exact
  symptom of the token problem.

---

## Self-Review

**Spec coverage.** Manual `workflow_dispatch` with `bump`/`allow_docs_only`/`dry_run`: Task 5. Semver tag resolution rejecting inherited CrossPoint tags: Task 2. Version bump of `platformio.ini` and the README version line: Task 3. Notes drafted from conventional commits with merges excluded: Task 3. Gate A with "no checks = failure": Task 5. Gate B with override: Task 5. Tag-already-exists check: Task 4. The token problem and failing loudly without the secret: Tasks 1 and 5. README prose left manual with a reminder: Task 5's summary step. `release.yml`/`ci.yml`/`release_candidate.yml` untouched: enforced by Task 5 Step 3 and Task 7 Step 3.

**Two additions beyond the spec,** both flagged rather than silent: the CLI refuses when `platformio.ini` disagrees with the last tag (Task 4), which catches a half-finished manual release the spec did not consider; and an existing notes file is never overwritten (Task 4), which preserves the hand-written prose standard the existing 1.0.1 and 1.0.2 notes set.

**Placeholder scan.** One step is deliberately open: Task 6 Step 1 requires finding where releases are already documented rather than assuming a path, with an explicit fallback and an explicit prohibition on creating a new top-level doc. Every code step carries real code.

**Type consistency.** `parse_version` returns a tuple consumed by `next_version` in Tasks 2 and 4. `PIO_VERSION_RE`'s group 2 is the version string, read in both `bump_platformio` (Task 3) and `main`'s guard (Task 4). The script writes `version` and `tag` to `$GITHUB_OUTPUT`; the workflow reads exactly `steps.train.outputs.version` and `steps.train.outputs.tag` (Task 5).

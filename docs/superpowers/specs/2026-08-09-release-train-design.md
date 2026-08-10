# Release Train — Design Spec

Date: 2026-08-09
Status: Implemented
Scope: Personal fork of CrossPoint Reader for the Xteink **X4**.

## Goal

Turn cutting a release from a five-step hand ritual into one button, so
changes can accumulate on `develop` and ship whenever they are judged ready.

The button does everything between "develop is good" and "the tag exists".
Publishing is already automated and is not touched.

## What already exists

Half the pipeline is built, and this design deliberately builds only the other
half.

- **`.github/workflows/release.yml`** fires on an `almanac-v*` tag. It verifies
  the tag matches `[almanac] version` in `platformio.ini` and **fails the
  release if they disagree**, builds `gh_release`, looks for
  `docs/release-notes/<tag>.md` (falling back to GitHub-generated notes), and
  publishes a GitHub Release. The `firmware.bin` asset name is load-bearing:
  `OtaUpdater` finds the download URL by matching it, so renaming it silently
  breaks over-the-air updates.
- **`.github/workflows/release_candidate.yml`** is a `workflow_dispatch` that
  builds RCs from `release/*` branches. Untouched by this design.
- **`.github/workflows/ci.yml`** runs clang-format, cppcheck, unit-tests and
  the firmware build on every push.

The manual part, visible in how 1.0.0, 1.0.1 and 1.0.2 were cut: decide what
ships, bump `platformio.ini`, bump the README, write the release-notes file,
commit `release: X.Y.Z`, create and push the tag.

## Why a tag is not a small action here

A tag does not merely publish a page. `release.yml` attaches `firmware.bin`,
and `OtaUpdater` on every device in the field discovers releases by that exact
shape. Pressing this button offers an update to every user. That is the reason
for the gates below, and the reason nothing about this design is scheduled or
automatic.

## Decisions

| Question | Decision |
|---|---|
| Departure | **Manual.** One `workflow_dispatch`, no schedule |
| Version | **`patch`/`minor`/`major` dropdown**, next number computed from the last tag |
| Release notes | **Auto-drafted from commits**, committed by the button |
| Gates | **Green CI on the exact commit**, and **refuse a docs-only release** |
| README prose | **Stays manual** (see below) |
| RC stage | Not built. `release_candidate.yml` already covers it |

## The workflow

One new file, `.github/workflows/release-train.yml`, `workflow_dispatch`, with
three inputs:

| Input | Type | Default | Purpose |
|---|---|---|---|
| `bump` | choice: `patch`/`minor`/`major` | `patch` | Which component to increment |
| `allow_docs_only` | boolean | `false` | Override for the docs-only gate |
| `dry_run` | boolean | `false` | Compute and print everything; push nothing |

Sequence:

1. Check out the dispatched ref with full history **and tags** (`fetch-depth: 0`).
2. Resolve the newest `almanac-v*` tag by semver. The repository also carries
   CrossPoint's inherited tags (`0.4.0`–`1.5.0`), so the pattern must be
   `almanac-v*` and the sort must be semver, not lexical — lexical ordering
   puts `almanac-v1.0.10` before `almanac-v1.0.9`.
3. Compute the next version from `bump`.
4. **Gate A — green CI** (below).
5. **Gate B — not docs-only** (below).
6. Fail if `almanac-v<next>` already exists locally or on the remote.
7. Bump `[almanac] version` in `platformio.ini` and the `**Version X.Y.Z**`
   line in `README.md`.
8. Write `docs/release-notes/almanac-v<next>.md`.
9. Commit `release: <next>`, tag `almanac-v<next>`, push the commit and the tag.
10. `release.yml` fires on the tag and publishes.

Under `dry_run`, steps 7–9 print what they would do and change nothing. Steps
1–6 run for real, so a dry run is a genuine rehearsal of the gates rather than
a syntax check.

## Gate A — green CI on the exact commit

Query the check runs for the released SHA and refuse unless every one
concluded `success`.

**"No checks found" must be treated as failure, not success.** That is the
difference between a gate and decoration: the most likely reason a commit has
no checks is that CI has not started yet, which is exactly when a release
should not be cut.

Grounding, from the session that produced this spec: `develop` went red on a
cppcheck failure originating in an unrelated workstream and stayed red for
roughly three hours. A release cut in that window would have shipped and
offered itself over the air.

## Gate B — refuse a docs-only release

Compare `git diff --name-only <lastTag>..HEAD`. If every changed path is under
`docs/` or matches `*.md`, refuse unless `allow_docs_only` is set.

Grounding: at the moment this was designed, the only unreleased commit on
`develop` touched `USER_GUIDE.md`. Cutting a release would have produced a
firmware binary functionally identical to 1.0.2 and offered every device an
update that changes nothing on it.

The override exists because a docs-only release is occasionally legitimate —
correcting release notes or upgrade instructions that ship with the firmware —
and a gate with no override becomes a reason to bypass the button entirely.

## The token problem, and why it decides the design

**A tag pushed with the default `GITHUB_TOKEN` will not start `release.yml`.**
GitHub suppresses workflow triggers for events created by that token — it is
the guard against a workflow endlessly re-triggering itself. The handoff this
entire design rests on would silently do nothing: the tag would exist, no
release would be published, and the failure would look like success.

This is not a detail to discover during implementation. It decides the design.

The fix is to push the commit and tag with a **fine-grained personal access
token stored as a repository secret** (`RELEASE_TRAIN_TOKEN`), scoped to this
repository with `contents: write`. A push made with a PAT is attributed to a
real account, so it triggers workflows normally and `release.yml` fires.

Two consequences follow:

- If the secret is absent or expired, the workflow must **fail loudly at the
  start**, before writing anything, rather than push a tag that goes nowhere.
- The token is the one piece of this design that expires. Its expiry is a
  future silent failure, and the loud check above is what converts it into an
  obvious one.

The alternative — having `release-train.yml` build and publish the release
itself instead of handing off — was rejected: it would duplicate `release.yml`,
and two paths that both publish `firmware.bin` is exactly how the asset name
`OtaUpdater` depends on gets broken.

## Permissions and the ref

The workflow declares, matching the convention `release.yml` and `ci.yml`
already follow (the repository's default workflow permission is `read`):

```yaml
permissions:
  contents: write   # commit the bump and notes, push the tag
  checks: read      # Gate A reads check runs for the SHA
```

`contents: write` covers the `GITHUB_TOKEN` used for API reads; the push
itself uses `RELEASE_TRAIN_TOKEN` for the reason above.

**There is deliberately no branch restriction.** The workflow releases whatever
ref it was dispatched against — normally `develop`, chosen in the Actions UI.
A branch gate was considered and declined; `workflow_dispatch` already makes
the ref an explicit, visible choice at press time.

## Release notes

Group non-merge commits since the last tag by conventional-commit type, which
this repository already uses consistently (`feat`, `fix`, `refactor`, `docs`,
`test`, `chore`, `perf`):

- **`feat`** → "New"
- **`fix`** → "Fixed"
- **`perf`** → "Performance"
- everything else → a short "Also" tail

Merge commits are excluded; they carry no description of their own and would
duplicate the branch commits underneath them.

Generated notes are a floor, not a ceiling. The file is committed to the
repository, so it can be edited afterwards — and because `release.yml` reads
the file at tag-push time, an edit after the fact updates the repository's
record but not the published release. That is a deliberate accepted limitation,
not an oversight: it keeps the button to one step.

## The README stays half-manual, deliberately

The `release: 1.0.2` commit changed the README in two distinct ways. One is
mechanical — a `**Version 1.0.2**` line. The other is editorial: a prose
paragraph explaining what the release does, plus upgrade guidance ("Devices on
1.0.0 must be flashed over USB once: that version's update check compared
uninitialized values").

A commit-log generator cannot write the second kind. So the workflow bumps the
version line and leaves the paragraph alone, printing a reminder in the job
summary that the prose still describes the previous release.

The alternative — generating that paragraph — was rejected because a
plausible-sounding but wrong upgrade instruction is worse than a stale one.

## Failure modes

| Situation | Behaviour |
|---|---|
| CI red, running, or absent on the SHA | Gate A fails before anything is written |
| Only docs changed since the last tag | Gate B fails unless overridden |
| Target tag already exists | Fails at step 6, before any commit |
| Another session pushes mid-run | The push fails; **no tag was created**, so re-running is safe |
| `platformio.ini` and tag disagree | Impossible by construction — one step writes both — and `release.yml` still checks |
| `RELEASE_TRAIN_TOKEN` missing or expired | Fails at the first step, before any file is written (see the token section) |
| Tag pushed but `release.yml` never ran | Should be unreachable once the token is right. Recovery is to delete the tag and re-run, **not** to re-push the same tag — `release.yml` publishes from the tag, and a deleted-and-recreated tag is the only way to retry cleanly |

The push-race case is the one worth designing for rather than hoping about:
several parallel sessions pushed to `develop` during this spec's own authoring.
Ordering matters — the commit and tag are pushed together, and the tag is
created locally only after the gates pass, so a failed push leaves no residue.

## The release commit re-triggers CI

Pushing `release: X.Y.Z` to `develop` starts another `ci.yml` run, roughly
fifteen minutes. `[skip ci]` was considered and rejected: it would mean the
exact commit that gets tagged and shipped is the one commit nobody ever
checked. The wasted minutes buy a verified release commit.

This only holds because the push uses `RELEASE_TRAIN_TOKEN` (see above). With
the default `GITHUB_TOKEN` neither `ci.yml` nor `release.yml` would fire, and
the release commit would be both unchecked *and* unpublished.

Note the ordering consequence: `release.yml` starts from the tag immediately
and does not wait for that `ci.yml` run. Gate A is therefore the real check on
what ships — the post-push CI run is a record of the release commit, not a
barrier in front of it. Designing it as a barrier would mean the button could
not complete in one press.

## Testing

There is no way to unit-test a GitHub Actions workflow in this repository, and
adding a framework for one workflow is not justified. Verification is:

- **`dry_run` against the current `develop`** — exercises tag discovery, the
  semver sort, version computation, and both gates for real.
- **`dry_run` with a deliberately failing precondition** — run it while a
  docs-only diff is the only change, and confirm Gate B refuses. Gate A is
  observable the same way whenever CI is mid-flight.
- **The first real cut is the end-to-end test**, and is expected to be watched.

## Out of scope

- Any schedule or automatic departure.
- Changes to `release.yml`, `release_candidate.yml`, or `ci.yml`.
- An RC stage; `release_candidate.yml` already exists.
- Generating the README's prose paragraph.
- Changing the `firmware.bin` asset name or anything else `OtaUpdater` parses.
- Backfilling release-notes files for 1.0.0–1.0.2.

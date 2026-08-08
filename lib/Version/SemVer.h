#pragma once

// Semantic-version comparison for the OTA release check. Pure string/integer
// work, no device dependencies -- safe to unit test on the host.
//
// The two sides come from different places and are not formatted alike:
//   - "current" is ALMANAC_VERSION, a bare "1.0.1" possibly carrying a build
//     suffix ("1.0.1-slim", "1.0.1-sticky", "1.0.1-rc+a1b2c3d").
//   - "latest" is a GitHub release's tag_name, which is prefixed:
//     "almanac-v1.0.1". Bare numeric tags are not available -- 0.4.0 through
//     1.5.0 already exist in this repository, inherited from CrossPoint.
// parseSemVer therefore skips any leading non-digit prefix.
namespace SemVer {

// Parses the first "<major>.<minor>.<patch>" triple out of `text`, ignoring a
// leading non-digit prefix and anything after the patch number. Returns false
// (leaving the outputs untouched) when no complete triple is present.
bool parseSemVer(const char* text, int& major, int& minor, int& patch);

// True when `latest` is a strictly newer release than `current`, with one
// exception: a `current` that is a release candidate ("-rc") of the same
// triple is treated as older, so an RC upgrades to its final release.
//
// Returns false if either side fails to parse. An unparseable tag means "no
// update", never an update built from indeterminate numbers.
bool isNewer(const char* latest, const char* current);

}  // namespace SemVer

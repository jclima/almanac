#include "SemVer.h"

#include <cstring>

namespace {

// Reads a run of ASCII digits at *p into `out`, advancing p past them. Returns
// false when there is no digit there, or when the run is absurdly long -- a
// version field that large means the tag is malformed, and the caller's
// contract is that malformed parses as "no update".
//
// Hand-rolled rather than sscanf/strtol: sscanf leaves its outputs untouched on
// a match failure, which is exactly the trap this file exists to close, and
// strtol's digit classification is locale-dependent.
bool readNumber(const char*& p, int& out) {
  if (*p < '0' || *p > '9') return false;
  int value = 0;
  while (*p >= '0' && *p <= '9') {
    if (value > 999999) return false;
    value = value * 10 + (*p - '0');
    ++p;
  }
  out = value;
  return true;
}

}  // namespace

namespace SemVer {

bool parseSemVer(const char* text, int& major, int& minor, int& patch) {
  if (text == nullptr) return false;

  // Skip a release-tag prefix such as "almanac-v" by running to the first digit.
  const char* p = text;
  while (*p != '\0' && (*p < '0' || *p > '9')) ++p;

  int parsedMajor = 0, parsedMinor = 0, parsedPatch = 0;
  if (!readNumber(p, parsedMajor)) return false;
  if (*p != '.') return false;
  ++p;
  if (!readNumber(p, parsedMinor)) return false;
  if (*p != '.') return false;
  ++p;
  if (!readNumber(p, parsedPatch)) return false;

  // Outputs are written only on a complete parse, so a caller that ignores the
  // return value still cannot read indeterminate values out of them.
  major = parsedMajor;
  minor = parsedMinor;
  patch = parsedPatch;
  return true;
}

bool isNewer(const char* latest, const char* current) {
  // Initialized, though parseSemVer's contract already guarantees they are
  // only read after a complete parse. Belt and braces: reading indeterminate
  // version numbers is the exact bug this file was written to close.
  int latestMajor = 0, latestMinor = 0, latestPatch = 0;
  int currentMajor = 0, currentMinor = 0, currentPatch = 0;
  if (!parseSemVer(latest, latestMajor, latestMinor, latestPatch)) return false;
  if (!parseSemVer(current, currentMajor, currentMinor, currentPatch)) return false;

  if (latestMajor != currentMajor) return latestMajor > currentMajor;
  if (latestMinor != currentMinor) return latestMinor > currentMinor;
  if (latestPatch != currentPatch) return latestPatch > currentPatch;

  // Same triple. An RC build is a pre-release of that triple, so the tagged
  // release is newer; a released tag never carries "-rc", so only `current`
  // needs checking. `current` is non-null here -- parseSemVer rejects nullptr.
  return strstr(current, "-rc") != nullptr;
}

}  // namespace SemVer

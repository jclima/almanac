#include "ZipGeocodeParser.h"

#include <cstdlib>
#include <cstring>

namespace {
void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  const size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

bool keyIs(const char* key, size_t len, const char* literal) {
  const size_t litLen = strlen(literal);
  return len == litLen && memcmp(key, literal, litLen) == 0;
}
}  // namespace

ZipGeocodeParser::ZipGeocodeParser()
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                            sOnArrayStart, sOnArrayEnd}) {}

void ZipGeocodeParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  sawFirstPlace = false;
  sawLatitude = false;
  sawLongitude = false;
  result = ZipGeocodeResult{};
}

void ZipGeocodeParser::feed(const char* data, size_t len) { parser.feed(data, len); }

void ZipGeocodeParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<ZipGeocodeParser*>(ctx);
  switch (self->position) {
    case Position::TOP_LEVEL:
      self->lastKey = keyIs(key, len, "places") ? LastKey::PLACES : LastKey::NONE;
      break;
    case Position::IN_PLACES:
      // Arrays don't have keys of their own; a key callback here would only
      // fire for something unexpected nested inside a non-first element.
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_PLACE:
      if (keyIs(key, len, "place name")) {
        self->lastKey = LastKey::PLACE_NAME;
      } else if (keyIs(key, len, "longitude")) {
        self->lastKey = LastKey::LONGITUDE;
      } else if (keyIs(key, len, "latitude")) {
        self->lastKey = LastKey::LATITUDE;
      } else if (keyIs(key, len, "state abbreviation")) {
        self->lastKey = LastKey::STATE_ABBREV;
      } else {
        // Deliberately ignores "state" (the full name) and any other field
        // -- stateAbbrev is the compact one the settings row needs.
        self->lastKey = LastKey::NONE;
      }
      break;
  }
}

void ZipGeocodeParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<ZipGeocodeParser*>(ctx);
  switch (self->position) {
    case Position::TOP_LEVEL:
      self->depth++;
      break;
    case Position::IN_PLACES:
      if (!self->sawFirstPlace) {
        self->position = Position::IN_PLACE;
        self->sawFirstPlace = true;
        // Reset: depth now tracks nesting within IN_PLACE, not the
        // leftover count from the array.
        self->depth = 0;
      } else {
        // A second (or later) places[] entry -- ignored, but still needs
        // balanced depth tracking so its close doesn't miscount.
        self->depth++;
      }
      break;
    case Position::IN_PLACE:
      self->depth++;
      break;
  }
  self->lastKey = LastKey::NONE;
}

void ZipGeocodeParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<ZipGeocodeParser*>(ctx);
  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_PLACES:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_PLACE:
      if (self->depth > 0) {
        self->depth--;
      } else {
        // A complete places[0] object closed -- but only trustworthy if it
        // actually carried both coordinates. A record missing lat/lon must
        // not silently write 0,0 (Null Island) into the home location.
        self->result.found = self->sawLatitude && self->sawLongitude;
        self->position = Position::IN_PLACES;
      }
      break;
  }
  self->lastKey = LastKey::NONE;
}

void ZipGeocodeParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<ZipGeocodeParser*>(ctx);
  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKey == LastKey::PLACES) {
        self->position = Position::IN_PLACES;
        self->depth = 0;
      } else {
        self->depth++;
      }
      break;
    case Position::IN_PLACES:
    case Position::IN_PLACE:
      self->depth++;
      break;
  }
  self->lastKey = LastKey::NONE;
}

void ZipGeocodeParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<ZipGeocodeParser*>(ctx);
  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_PLACES:
      if (self->depth > 0) {
        self->depth--;
      } else {
        // The places array closed -- nothing further to capture.
        self->position = Position::TOP_LEVEL;
      }
      break;
    case Position::IN_PLACE:
      if (self->depth > 0) self->depth--;
      break;
  }
  self->lastKey = LastKey::NONE;
}

void ZipGeocodeParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<ZipGeocodeParser*>(ctx);
  if (self->position == Position::IN_PLACE && self->depth == 0) {
    switch (self->lastKey) {
      case LastKey::PLACE_NAME:
        safeCopy(self->result.placeName, sizeof(self->result.placeName), value, len);
        break;
      case LastKey::LONGITUDE: {
        char buf[24];
        safeCopy(buf, sizeof(buf), value, len);
        self->result.longitude = strtod(buf, nullptr);
        self->sawLongitude = true;
        break;
      }
      case LastKey::LATITUDE: {
        char buf[24];
        safeCopy(buf, sizeof(buf), value, len);
        self->result.latitude = strtod(buf, nullptr);
        self->sawLatitude = true;
        break;
      }
      case LastKey::STATE_ABBREV:
        safeCopy(self->result.stateAbbrev, sizeof(self->result.stateAbbrev), value, len);
        break;
      default:
        break;
    }
  }
  self->lastKey = LastKey::NONE;
}

void ZipGeocodeParser::sOnNumber(void* ctx, const char* /*value*/, size_t /*len*/) {
  static_cast<ZipGeocodeParser*>(ctx)->lastKey = LastKey::NONE;
}

void ZipGeocodeParser::sOnBool(void* ctx, bool /*value*/) {
  static_cast<ZipGeocodeParser*>(ctx)->lastKey = LastKey::NONE;
}

void ZipGeocodeParser::sOnNull(void* ctx) { static_cast<ZipGeocodeParser*>(ctx)->lastKey = LastKey::NONE; }

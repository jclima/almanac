#include "AircraftInfoParser.h"

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

AircraftInfoParser::AircraftInfoParser()
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd, nullptr,
                           nullptr}) {}

void AircraftInfoParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  sawAircraftObject = false;
  result = AircraftInfo{};
}

void AircraftInfoParser::feed(const char* data, size_t len) { parser.feed(data, len); }

void AircraftInfoParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<AircraftInfoParser*>(ctx);
  switch (self->position) {
    case Position::TOP_LEVEL:
      self->lastKey = keyIs(key, len, "response") ? LastKey::RESPONSE : LastKey::NONE;
      break;
    case Position::IN_RESPONSE:
      self->lastKey = keyIs(key, len, "aircraft") ? LastKey::AIRCRAFT : LastKey::NONE;
      break;
    case Position::IN_AIRCRAFT:
      if (keyIs(key, len, "manufacturer")) {
        self->lastKey = LastKey::MANUFACTURER;
      } else if (keyIs(key, len, "icao_type")) {
        self->lastKey = LastKey::ICAO_TYPE;
      } else if (keyIs(key, len, "registration")) {
        self->lastKey = LastKey::REGISTRATION;
      } else {
        // Deliberately ignores "type" -- verbose and inconsistent
        // ("737NG 990ER/W"); icao_type is the clean designator.
        self->lastKey = LastKey::NONE;
      }
      break;
  }
}

void AircraftInfoParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<AircraftInfoParser*>(ctx);
  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKey == LastKey::RESPONSE) {
        self->position = Position::IN_RESPONSE;
        // Reset: depth now tracks nesting within IN_RESPONSE, not the
        // leftover count from the outer top-level object.
        self->depth = 0;
      } else {
        self->depth++;
      }
      break;
    case Position::IN_RESPONSE:
      if (self->lastKey == LastKey::AIRCRAFT) {
        self->position = Position::IN_AIRCRAFT;
        self->sawAircraftObject = true;
        // Reset: depth now tracks nesting within IN_AIRCRAFT.
        self->depth = 0;
      } else {
        self->depth++;
      }
      break;
    case Position::IN_AIRCRAFT:
      self->depth++;
      break;
  }
  self->lastKey = LastKey::NONE;
}

void AircraftInfoParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<AircraftInfoParser*>(ctx);
  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_RESPONSE:
      if (self->depth > 0) {
        self->depth--;
      } else {
        self->position = Position::TOP_LEVEL;
      }
      break;
    case Position::IN_AIRCRAFT:
      if (self->depth > 0) {
        self->depth--;
      } else {
        // A complete aircraft object closed: the record is trustworthy now.
        self->result.found = self->sawAircraftObject;
        self->position = Position::IN_RESPONSE;
      }
      break;
  }
  self->lastKey = LastKey::NONE;
}

void AircraftInfoParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<AircraftInfoParser*>(ctx);
  if (self->position == Position::IN_AIRCRAFT && self->depth == 0) {
    switch (self->lastKey) {
      case LastKey::MANUFACTURER:
        safeCopy(self->result.manufacturer, sizeof(self->result.manufacturer), value, len);
        break;
      case LastKey::ICAO_TYPE:
        safeCopy(self->result.icaoType, sizeof(self->result.icaoType), value, len);
        break;
      case LastKey::REGISTRATION:
        safeCopy(self->result.registration, sizeof(self->result.registration), value, len);
        break;
      default:
        break;
    }
  }
  // A string value for the top-level "response" key is adsbdb's
  // {"response":"unknown aircraft"} miss -- normal, not an error.
  self->lastKey = LastKey::NONE;
}

void AircraftInfoParser::sOnNumber(void* ctx, const char* /*value*/, size_t /*len*/) {
  static_cast<AircraftInfoParser*>(ctx)->lastKey = LastKey::NONE;
}

void AircraftInfoParser::sOnBool(void* ctx, bool /*value*/) {
  static_cast<AircraftInfoParser*>(ctx)->lastKey = LastKey::NONE;
}

void AircraftInfoParser::sOnNull(void* ctx) { static_cast<AircraftInfoParser*>(ctx)->lastKey = LastKey::NONE; }

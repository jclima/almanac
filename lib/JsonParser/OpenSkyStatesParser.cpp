#include "OpenSkyStatesParser.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "GeoMath.h"

namespace {
constexpr double MPS_TO_MPH = 2.23694;
constexpr double METERS_TO_FEET = 3.28084;

void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  const size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

void trimTrailingSpaces(char* s) {
  size_t len = strlen(s);
  while (len > 0 && s[len - 1] == ' ') s[--len] = '\0';
}

double parseDouble(const char* value, size_t len) {
  char buf[32];
  const size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  memcpy(buf, value, n);
  buf[n] = '\0';
  return strtod(buf, nullptr);
}
}  // namespace

OpenSkyStatesParser::OpenSkyStatesParser()
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, nullptr, nullptr, sOnArrayStart,
                           sOnArrayEnd}) {}

void OpenSkyStatesParser::reset(const double homeLatitude, const double homeLongitude, const double radius) {
  parser.reset();
  position = Position::AWAITING_STATES;
  expectStatesArray = false;
  fieldIndex = 0;
  nestedArrayDepth = 0;
  scratch = RowScratch{};
  homeLat = homeLatitude;
  homeLon = homeLongitude;
  radiusMiles = radius;
  matchCount_ = 0;
}

void OpenSkyStatesParser::feed(const char* data, size_t len) { parser.feed(data, len); }

void OpenSkyStatesParser::insertSorted(const FlightMatch& match) {
  if (matchCount_ < MAX_MATCHES) {
    size_t i = matchCount_;
    while (i > 0 && matches[i - 1].distanceMiles > match.distanceMiles) {
      matches[i] = matches[i - 1];
      --i;
    }
    matches[i] = match;
    ++matchCount_;
  } else if (match.distanceMiles < matches[MAX_MATCHES - 1].distanceMiles) {
    size_t i = MAX_MATCHES - 1;
    while (i > 0 && matches[i - 1].distanceMiles > match.distanceMiles) {
      matches[i] = matches[i - 1];
      --i;
    }
    matches[i] = match;
  }
}

void OpenSkyStatesParser::commitRow() {
  if (scratch.hasLat && scratch.hasLon && !scratch.onGround) {
    const double distance = GeoMath::distanceMiles(homeLat, homeLon, scratch.lat, scratch.lon);
    if (distance <= radiusMiles) {
      FlightMatch m{};
      safeCopy(m.icao24, sizeof(m.icao24), scratch.icao24, strlen(scratch.icao24));
      safeCopy(m.callsign, sizeof(m.callsign), scratch.callsign, strlen(scratch.callsign));
      trimTrailingSpaces(m.callsign);
      safeCopy(m.originCountry, sizeof(m.originCountry), scratch.originCountry, strlen(scratch.originCountry));
      m.latitude = scratch.lat;
      m.longitude = scratch.lon;

      m.hasAltitudeFeet = scratch.hasGeoAlt || scratch.hasBaroAlt;
      const float altMeters = scratch.hasGeoAlt ? scratch.geoAltM : scratch.baroAltM;
      m.altitudeFeet = m.hasAltitudeFeet ? static_cast<int32_t>(lroundf(altMeters * METERS_TO_FEET)) : 0;

      m.hasSpeedMph = scratch.hasVelocity;
      m.speedMph = m.hasSpeedMph ? static_cast<int32_t>(lroundf(scratch.velocityMs * MPS_TO_MPH)) : 0;

      m.hasHeading = scratch.hasTrueTrack;
      m.headingDeg =
          m.hasHeading ? ((static_cast<int32_t>(lroundf(scratch.trueTrackDeg)) % 360 + 360) % 360) : 0;

      m.hasVerticalRate = scratch.hasVerticalRate;
      m.verticalRateMs = scratch.verticalRateMs;

      m.distanceMiles = distance;
      m.bearingDeg = GeoMath::initialBearingDegrees(homeLat, homeLon, scratch.lat, scratch.lon);

      insertSorted(m);
    }
  }
  scratch = RowScratch{};
}

void OpenSkyStatesParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<OpenSkyStatesParser*>(ctx);
  if (self->position == Position::AWAITING_STATES) {
    self->expectStatesArray = (len == 6 && memcmp(key, "states", 6) == 0);
  }
}

void OpenSkyStatesParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<OpenSkyStatesParser*>(ctx);
  switch (self->position) {
    case Position::AWAITING_STATES:
      if (self->expectStatesArray) {
        self->position = Position::IN_STATES_ARRAY;
        self->expectStatesArray = false;
      }
      break;
    case Position::IN_STATES_ARRAY:
      self->position = Position::IN_ROW;
      self->fieldIndex = 0;
      self->nestedArrayDepth = 0;
      self->scratch = RowScratch{};
      break;
    case Position::IN_ROW:
      // Nested array within a row (OpenSky's "sensors" field, index 12).
      // Reserve its field-index slot once, then ignore its contents.
      if (self->nestedArrayDepth == 0) self->fieldIndex++;
      self->nestedArrayDepth++;
      break;
  }
}

void OpenSkyStatesParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<OpenSkyStatesParser*>(ctx);
  switch (self->position) {
    case Position::AWAITING_STATES:
      break;  // stray top-level array; not part of this schema
    case Position::IN_STATES_ARRAY:
      self->position = Position::AWAITING_STATES;
      break;
    case Position::IN_ROW:
      if (self->nestedArrayDepth > 0) {
        self->nestedArrayDepth--;
      } else {
        self->commitRow();
        self->position = Position::IN_STATES_ARRAY;
      }
      break;
  }
}

void OpenSkyStatesParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<OpenSkyStatesParser*>(ctx);
  if (self->position != Position::IN_ROW || self->nestedArrayDepth > 0) return;
  switch (self->fieldIndex) {
    case 0:  // icao24
      safeCopy(self->scratch.icao24, sizeof(self->scratch.icao24), value, len);
      break;
    case 1:  // callsign
      safeCopy(self->scratch.callsign, sizeof(self->scratch.callsign), value, len);
      break;
    case 2:  // origin_country
      safeCopy(self->scratch.originCountry, sizeof(self->scratch.originCountry), value, len);
      break;
    default:
      break;
  }
  self->fieldIndex++;
}

void OpenSkyStatesParser::sOnNumber(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<OpenSkyStatesParser*>(ctx);
  if (self->position != Position::IN_ROW || self->nestedArrayDepth > 0) return;
  switch (self->fieldIndex) {
    case 5:  // longitude
      self->scratch.lon = parseDouble(value, len);
      self->scratch.hasLon = true;
      break;
    case 6:  // latitude
      self->scratch.lat = parseDouble(value, len);
      self->scratch.hasLat = true;
      break;
    case 7:  // baro_altitude (meters)
      self->scratch.baroAltM = static_cast<float>(parseDouble(value, len));
      self->scratch.hasBaroAlt = true;
      break;
    case 9:  // velocity (m/s)
      self->scratch.velocityMs = static_cast<float>(parseDouble(value, len));
      self->scratch.hasVelocity = true;
      break;
    case 10:  // true_track (degrees)
      self->scratch.trueTrackDeg = static_cast<float>(parseDouble(value, len));
      self->scratch.hasTrueTrack = true;
      break;
    case 11:  // vertical_rate (m/s)
      self->scratch.verticalRateMs = static_cast<float>(parseDouble(value, len));
      self->scratch.hasVerticalRate = true;
      break;
    case 13:  // geo_altitude (meters)
      self->scratch.geoAltM = static_cast<float>(parseDouble(value, len));
      self->scratch.hasGeoAlt = true;
      break;
    default:
      break;
  }
  self->fieldIndex++;
}

void OpenSkyStatesParser::sOnBool(void* ctx, bool value) {
  auto* self = static_cast<OpenSkyStatesParser*>(ctx);
  if (self->position != Position::IN_ROW || self->nestedArrayDepth > 0) return;
  if (self->fieldIndex == 8) self->scratch.onGround = value;  // on_ground
  self->fieldIndex++;
}

void OpenSkyStatesParser::sOnNull(void* ctx) {
  auto* self = static_cast<OpenSkyStatesParser*>(ctx);
  if (self->position != Position::IN_ROW || self->nestedArrayDepth > 0) return;
  // A null just means "field absent" -- the corresponding hasX flag in
  // scratch was already false from the row reset, so there's nothing to set.
  self->fieldIndex++;
}

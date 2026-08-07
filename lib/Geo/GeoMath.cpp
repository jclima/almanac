#include "GeoMath.h"

#include <cmath>

namespace GeoMath {

namespace {
constexpr double DEG_TO_RAD = M_PI / 180.0;
constexpr double MILES_PER_DEGREE_LAT = 69.0;

double normalizeDegrees(double deg) {
  double d = std::fmod(deg, 360.0);
  if (d < 0) d += 360.0;
  return d;
}
}  // namespace

double distanceMiles(const double lat1Deg, const double lon1Deg, const double lat2Deg, const double lon2Deg) {
  const double lat1 = lat1Deg * DEG_TO_RAD;
  const double lat2 = lat2Deg * DEG_TO_RAD;
  const double dLat = (lat2Deg - lat1Deg) * DEG_TO_RAD;
  const double dLon = (lon2Deg - lon1Deg) * DEG_TO_RAD;

  const double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
                   std::cos(lat1) * std::cos(lat2) * std::sin(dLon / 2) * std::sin(dLon / 2);
  const double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
  return EARTH_RADIUS_MILES * c;
}

double initialBearingDegrees(const double lat1Deg, const double lon1Deg, const double lat2Deg, const double lon2Deg) {
  const double lat1 = lat1Deg * DEG_TO_RAD;
  const double lat2 = lat2Deg * DEG_TO_RAD;
  const double dLon = (lon2Deg - lon1Deg) * DEG_TO_RAD;

  const double y = std::sin(dLon) * std::cos(lat2);
  const double x = std::cos(lat1) * std::sin(lat2) - std::sin(lat1) * std::cos(lat2) * std::cos(dLon);
  const double bearing = std::atan2(y, x) / DEG_TO_RAD;
  return normalizeDegrees(bearing);
}

const char* compassPoint(const double bearingDeg) {
  static constexpr const char* POINTS[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  const double normalized = normalizeDegrees(bearingDeg);
  const int index = static_cast<int>((normalized + 22.5) / 45.0) % 8;
  return POINTS[index];
}

BoundingBox computeBoundingBox(const double lat, const double lon, const double radiusMiles) {
  const double latSpan = radiusMiles / MILES_PER_DEGREE_LAT;

  double milesPerDegreeLon = MILES_PER_DEGREE_LAT * std::cos(lat * DEG_TO_RAD);
  if (milesPerDegreeLon < 1.0) milesPerDegreeLon = 1.0;  // guard near the poles
  const double lonSpan = radiusMiles / milesPerDegreeLon;

  return BoundingBox{lat - latSpan, lon - lonSpan, lat + latSpan, lon + lonSpan};
}

ScreenPoint polarToScreen(const double distanceMiles, const double bearingDeg, const double maxRangeMiles, const int cx,
                          const int cy, const int radiusPx) {
  if (maxRangeMiles <= 0.0 || radiusPx <= 0) {
    return ScreenPoint{cx, cy};
  }

  double ratio = distanceMiles / maxRangeMiles;
  if (ratio < 0.0) ratio = 0.0;
  if (ratio > 1.0) ratio = 1.0;  // clamp over-range aircraft to the outer ring

  const double r = ratio * radiusPx;
  const double theta = normalizeDegrees(bearingDeg) * DEG_TO_RAD;

  // Bearing 0 = north = up, so it maps to -y; bearing 90 = east = +x.
  const double dx = r * std::sin(theta);
  const double dy = -r * std::cos(theta);

  return ScreenPoint{cx + static_cast<int>(std::lround(dx)), cy + static_cast<int>(std::lround(dy))};
}

void headingTriangle(const int cx, const int cy, const double headingDeg, const int size, int xs[4], int ys[4]) {
  // Unrotated glyph, nose pointing up (-y), expressed as fractions of `size`:
  // nose, right-rear, tail notch, left-rear. The notch is what makes it read
  // as an arrow rather than a plain triangle at ~12px on e-ink.
  static constexpr double SHAPE_X[4] = {0.0, 0.64, 0.0, -0.64};
  static constexpr double SHAPE_Y[4] = {-1.0, 0.82, 0.36, 0.82};

  const double theta = normalizeDegrees(headingDeg) * DEG_TO_RAD;
  const double c = std::cos(theta);
  const double s = std::sin(theta);

  for (int i = 0; i < 4; ++i) {
    const double px = SHAPE_X[i] * size;
    const double py = SHAPE_Y[i] * size;
    // Clockwise rotation in screen coords (+y down).
    const double rx = px * c - py * s;
    const double ry = px * s + py * c;
    xs[i] = cx + static_cast<int>(std::lround(rx));
    ys[i] = cy + static_cast<int>(std::lround(ry));
  }
}

}  // namespace GeoMath

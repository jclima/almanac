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

double initialBearingDegrees(const double lat1Deg, const double lon1Deg, const double lat2Deg,
                             const double lon2Deg) {
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

}  // namespace GeoMath

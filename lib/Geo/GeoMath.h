#pragma once

// Great-circle distance/bearing/bounding-box helpers for the Nearby Flights
// feature. Pure math, no device dependencies -- safe to unit test on the host.
namespace GeoMath {

constexpr double EARTH_RADIUS_MILES = 3958.7613;

// Great-circle distance between two WGS84 points, in miles.
double distanceMiles(double lat1Deg, double lon1Deg, double lat2Deg, double lon2Deg);

// Initial bearing from (lat1,lon1) to (lat2,lon2), degrees clockwise from
// true north, normalized to [0, 360).
double initialBearingDegrees(double lat1Deg, double lon1Deg, double lat2Deg, double lon2Deg);

// 8-point compass label ("N", "NE", "E", "SE", "S", "SW", "W", "NW") for a
// bearing in degrees. Input is normalized internally, so any finite value works.
const char* compassPoint(double bearingDeg);

struct BoundingBox {
  double latMin;
  double lonMin;
  double latMax;
  double lonMax;
};

// Bounding box covering a circle of radiusMiles around (lat,lon), using an
// equirectangular approximation -- fine at these radii, not valid near the poles.
BoundingBox computeBoundingBox(double lat, double lon, double radiusMiles);

}  // namespace GeoMath

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

struct ScreenPoint {
  int x;
  int y;
};

// Maps a (distance, bearing) polar pair onto a radar plot. Screen coords are
// +x right and +y DOWN; bearing 0 is north (up) and increases clockwise, so
// bearing 90 lands due right of centre.
//
// Distances at or beyond maxRangeMiles clamp to the outer ring instead of
// escaping the plot. A maxRangeMiles of 0 collapses everything to the centre
// rather than dividing by zero.
ScreenPoint polarToScreen(double distanceMiles, double bearingDeg, double maxRangeMiles, int cx, int cy, int radiusPx);

// Fills xs[4]/ys[4] with an arrow-like quadrilateral centred on (cx,cy),
// rotated so its nose points along headingDeg (0 = up/north, clockwise).
// xs[0]/ys[0] is always the nose. `size` is the centre-to-nose distance in
// pixels, but the two rear vertices sit farther out, at ~1.04*size from
// centre (for any heading) -- callers sizing a bounding box or clip margin
// should budget for that, not just `size`. Output feeds GfxRenderer::fillPolygon
// directly.
void headingTriangle(int cx, int cy, double headingDeg, int size, int xs[4], int ys[4]);

}  // namespace GeoMath

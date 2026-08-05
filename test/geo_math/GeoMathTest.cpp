#include <gtest/gtest.h>

#include <cstdlib>

#include "lib/Geo/GeoMath.h"

TEST(GeoMath, DistanceZeroForSamePoint) {
  EXPECT_NEAR(GeoMath::distanceMiles(37.6213, -122.3790, 37.6213, -122.3790), 0.0, 1e-6);
}

TEST(GeoMath, DistanceOneDegreeLatitudeConstantAtEquator) {
  EXPECT_NEAR(GeoMath::distanceMiles(0.0, 0.0, 1.0, 0.0), 69.09, 0.1);
}

TEST(GeoMath, DistanceOneDegreeLatitudeConstantAtMidLatitude) {
  // A degree of latitude is ~constant everywhere (meridians are great circles),
  // unlike a degree of longitude, which shrinks away from the equator.
  EXPECT_NEAR(GeoMath::distanceMiles(37.0, -122.0, 38.0, -122.0), 69.09, 0.15);
}

TEST(GeoMath, DistanceOneDegreeLongitudeAtEquator) {
  EXPECT_NEAR(GeoMath::distanceMiles(0.0, 0.0, 0.0, 1.0), 69.09, 0.1);
}

TEST(GeoMath, DistanceIsSymmetric) {
  const double ab = GeoMath::distanceMiles(37.6213, -122.3790, 40.6413, -73.7781);
  const double ba = GeoMath::distanceMiles(40.6413, -73.7781, 37.6213, -122.3790);
  EXPECT_NEAR(ab, ba, 1e-6);
}

TEST(GeoMath, BearingDueNorth) {
  EXPECT_NEAR(GeoMath::initialBearingDegrees(0.0, 0.0, 1.0, 0.0), 0.0, 0.5);
}

TEST(GeoMath, BearingDueEast) {
  EXPECT_NEAR(GeoMath::initialBearingDegrees(0.0, 0.0, 0.0, 1.0), 90.0, 0.5);
}

TEST(GeoMath, BearingDueSouth) {
  EXPECT_NEAR(GeoMath::initialBearingDegrees(0.0, 0.0, -1.0, 0.0), 180.0, 0.5);
}

TEST(GeoMath, BearingDueWest) {
  EXPECT_NEAR(GeoMath::initialBearingDegrees(0.0, 0.0, 0.0, -1.0), 270.0, 0.5);
}

TEST(GeoMath, CompassPointCardinals) {
  EXPECT_STREQ(GeoMath::compassPoint(0.0), "N");
  EXPECT_STREQ(GeoMath::compassPoint(90.0), "E");
  EXPECT_STREQ(GeoMath::compassPoint(180.0), "S");
  EXPECT_STREQ(GeoMath::compassPoint(270.0), "W");
}

TEST(GeoMath, CompassPointIntercardinals) {
  EXPECT_STREQ(GeoMath::compassPoint(45.0), "NE");
  EXPECT_STREQ(GeoMath::compassPoint(135.0), "SE");
  EXPECT_STREQ(GeoMath::compassPoint(225.0), "SW");
  EXPECT_STREQ(GeoMath::compassPoint(315.0), "NW");
}

TEST(GeoMath, CompassPointWrapsNearZero) {
  EXPECT_STREQ(GeoMath::compassPoint(350.0), "N");
  EXPECT_STREQ(GeoMath::compassPoint(-10.0), "N");
}

TEST(GeoMath, BoundingBoxCoversHomeLocation) {
  const auto box = GeoMath::computeBoundingBox(37.6213, -122.3790, 30.0);
  EXPECT_LT(box.latMin, 37.6213);
  EXPECT_GT(box.latMax, 37.6213);
  EXPECT_LT(box.lonMin, -122.3790);
  EXPECT_GT(box.lonMax, -122.3790);
}

TEST(GeoMath, BoundingBoxLatitudeSpanMatchesRadius) {
  const auto box = GeoMath::computeBoundingBox(0.0, 0.0, 69.09);
  EXPECT_NEAR(box.latMax - box.latMin, 2.0, 0.05);  // ~1 degree each direction
}

TEST(GeoMath, BoundingBoxWidensLongitudeSpanAwayFromEquator) {
  const auto equatorBox = GeoMath::computeBoundingBox(0.0, 0.0, 50.0);
  const auto highLatBox = GeoMath::computeBoundingBox(60.0, 0.0, 50.0);
  const double equatorLonSpan = equatorBox.lonMax - equatorBox.lonMin;
  const double highLatLonSpan = highLatBox.lonMax - highLatBox.lonMin;
  EXPECT_GT(highLatLonSpan, equatorLonSpan);
}

// -- polarToScreen ----------------------------------------------------------
// Screen coords: +x right, +y DOWN. Bearing 0 = north = up = -y, increasing
// clockwise, so bearing 90 = east = +x.

TEST(GeoMath, PolarToScreenCentreForZeroDistance) {
  const auto p = GeoMath::polarToScreen(0.0, 137.0, 30.0, 100, 100, 90);
  EXPECT_EQ(p.x, 100);
  EXPECT_EQ(p.y, 100);
}

TEST(GeoMath, PolarToScreenNorthIsUp) {
  const auto p = GeoMath::polarToScreen(30.0, 0.0, 30.0, 100, 100, 90);
  EXPECT_EQ(p.x, 100);
  EXPECT_EQ(p.y, 10);  // cy - radiusPx
}

TEST(GeoMath, PolarToScreenEastIsRight) {
  const auto p = GeoMath::polarToScreen(30.0, 90.0, 30.0, 100, 100, 90);
  EXPECT_EQ(p.x, 190);  // cx + radiusPx
  EXPECT_EQ(p.y, 100);
}

TEST(GeoMath, PolarToScreenSouthIsDown) {
  const auto p = GeoMath::polarToScreen(30.0, 180.0, 30.0, 100, 100, 90);
  EXPECT_EQ(p.x, 100);
  EXPECT_EQ(p.y, 190);  // cy + radiusPx
}

TEST(GeoMath, PolarToScreenWestIsLeft) {
  const auto p = GeoMath::polarToScreen(30.0, 270.0, 30.0, 100, 100, 90);
  EXPECT_EQ(p.x, 10);  // cx - radiusPx
  EXPECT_EQ(p.y, 100);
}

TEST(GeoMath, PolarToScreenDistanceScalesLinearly) {
  // A third of max range lands a third of the way out.
  const auto p = GeoMath::polarToScreen(10.0, 0.0, 30.0, 100, 100, 90);
  EXPECT_EQ(p.x, 100);
  EXPECT_EQ(p.y, 70);  // cy - 90/3
}

TEST(GeoMath, PolarToScreenClampsBeyondMaxRange) {
  // Over-range aircraft pin to the outer ring rather than escaping the plot.
  const auto p = GeoMath::polarToScreen(500.0, 90.0, 30.0, 100, 100, 90);
  EXPECT_EQ(p.x, 190);
  EXPECT_EQ(p.y, 100);
}

TEST(GeoMath, PolarToScreenNormalizesBearing) {
  const auto wrapped = GeoMath::polarToScreen(30.0, 450.0, 30.0, 100, 100, 90);
  const auto plain = GeoMath::polarToScreen(30.0, 90.0, 30.0, 100, 100, 90);
  EXPECT_EQ(wrapped.x, plain.x);
  EXPECT_EQ(wrapped.y, plain.y);
}

TEST(GeoMath, PolarToScreenHandlesZeroMaxRange) {
  // Degenerate config must not divide by zero; everything collapses to centre.
  const auto p = GeoMath::polarToScreen(10.0, 45.0, 0.0, 100, 100, 90);
  EXPECT_EQ(p.x, 100);
  EXPECT_EQ(p.y, 100);
}

TEST(GeoMath, PolarToScreenIntercardinalIsDiagonal) {
  // NE: equal +x and -y offsets.
  const auto p = GeoMath::polarToScreen(30.0, 45.0, 30.0, 100, 100, 90);
  EXPECT_EQ(p.x - 100, 100 - p.y);
  EXPECT_GT(p.x, 100);
  EXPECT_LT(p.y, 100);
}

// -- headingTriangle --------------------------------------------------------
// xs[0]/ys[0] is the nose vertex; the remaining three form the tail.

TEST(GeoMath, HeadingTriangleNoseUpForZeroHeading) {
  int xs[4];
  int ys[4];
  GeoMath::headingTriangle(100, 100, 0.0, 12, xs, ys);
  EXPECT_EQ(xs[0], 100);
  EXPECT_EQ(ys[0], 88);  // cy - size
}

TEST(GeoMath, HeadingTriangleNoseRightForEastHeading) {
  int xs[4];
  int ys[4];
  GeoMath::headingTriangle(100, 100, 90.0, 12, xs, ys);
  EXPECT_EQ(xs[0], 112);  // cx + size
  EXPECT_EQ(ys[0], 100);
}

TEST(GeoMath, HeadingTriangleNoseDownForSouthHeading) {
  int xs[4];
  int ys[4];
  GeoMath::headingTriangle(100, 100, 180.0, 12, xs, ys);
  EXPECT_EQ(xs[0], 100);
  EXPECT_EQ(ys[0], 112);  // cy + size
}

TEST(GeoMath, HeadingTriangleNoseLeftForWestHeading) {
  int xs[4];
  int ys[4];
  GeoMath::headingTriangle(100, 100, 270.0, 12, xs, ys);
  EXPECT_EQ(xs[0], 88);  // cx - size
  EXPECT_EQ(ys[0], 100);
}

TEST(GeoMath, HeadingTriangleVerticesAreDistinct) {
  int xs[4];
  int ys[4];
  GeoMath::headingTriangle(100, 100, 33.0, 12, xs, ys);
  // A degenerate glyph (all points coincident) would render as nothing.
  bool anyDifferent = false;
  for (int i = 1; i < 4; ++i) {
    if (xs[i] != xs[0] || ys[i] != ys[0]) anyDifferent = true;
  }
  EXPECT_TRUE(anyDifferent);
}

TEST(GeoMath, HeadingTriangleStaysWithinSizeBounds) {
  int xs[4];
  int ys[4];
  GeoMath::headingTriangle(100, 100, 217.0, 12, xs, ys);
  for (int i = 0; i < 4; ++i) {
    EXPECT_LE(std::abs(xs[i] - 100), 13);  // size + 1 for rounding
    EXPECT_LE(std::abs(ys[i] - 100), 13);
  }
}

TEST(GeoMath, HeadingTriangleNormalizesHeading) {
  int a[4];
  int b[4];
  int ay[4];
  int by[4];
  GeoMath::headingTriangle(100, 100, 450.0, 12, a, ay);
  GeoMath::headingTriangle(100, 100, 90.0, 12, b, by);
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(a[i], b[i]);
    EXPECT_EQ(ay[i], by[i]);
  }
}

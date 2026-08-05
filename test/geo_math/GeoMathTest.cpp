#include <gtest/gtest.h>

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

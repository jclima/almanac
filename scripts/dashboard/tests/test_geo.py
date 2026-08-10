import pytest

from dashboard.geo import (
    bounding_box,
    compass_point,
    distance_miles,
    initial_bearing_degrees,
)


def test_distance_to_self_is_zero():
    assert distance_miles(38.7223, -9.1393, 38.7223, -9.1393) == pytest.approx(0.0, abs=1e-9)


def test_london_to_paris():
    # Great-circle London -> Paris is ~213.7 miles.
    d = distance_miles(51.5074, -0.1278, 48.8566, 2.3522)
    assert d == pytest.approx(213.7, abs=2.0)


def test_bearing_due_north():
    assert initial_bearing_degrees(0.0, 0.0, 1.0, 0.0) == pytest.approx(0.0, abs=1e-6)


def test_bearing_due_east():
    assert initial_bearing_degrees(0.0, 0.0, 0.0, 1.0) == pytest.approx(90.0, abs=1e-6)


@pytest.mark.parametrize(
    "bearing,expected",
    [
        (0.0, "N"),
        (22.4, "N"),
        (22.6, "NE"),
        (45.0, "NE"),
        (90.0, "E"),
        (180.0, "S"),
        (270.0, "W"),
        (350.0, "N"),
        (-10.0, "N"),
        (720.0, "N"),
    ],
)
def test_compass_point(bearing, expected):
    assert compass_point(bearing) == expected


def test_bounding_box_at_the_equator():
    # 69 miles per degree of latitude, and cos(0) == 1, so both spans are 1 degree.
    box = bounding_box(0.0, 0.0, 69.0)
    assert box.lat_min == pytest.approx(-1.0)
    assert box.lat_max == pytest.approx(1.0)
    assert box.lon_min == pytest.approx(-1.0)
    assert box.lon_max == pytest.approx(1.0)


def test_bounding_box_longitude_span_widens_with_latitude():
    equator = bounding_box(0.0, 0.0, 25.0)
    north = bounding_box(60.0, 0.0, 25.0)
    assert (north.lon_max - north.lon_min) > (equator.lon_max - equator.lon_min)


def test_bounding_box_guards_near_the_pole():
    # The clamp fires only when 69*cos(lat) < 1, i.e. above ~89.17 degrees.
    # There, lon_span = 25/1.0, so the full span is 50 degrees.
    box = bounding_box(89.9999, 0.0, 25.0)
    assert box.lon_max - box.lon_min == pytest.approx(50.0)


def test_bounding_box_does_not_clamp_below_the_guard_latitude():
    # Proves the previous test is exercising the clamp rather than passing by
    # accident: at 80 degrees, 69*cos(80) is ~11.98, well above the 1.0 floor.
    box = bounding_box(80.0, 0.0, 25.0)
    assert box.lon_max - box.lon_min == pytest.approx(4.174, abs=0.01)

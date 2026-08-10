"""Great-circle helpers, mirroring lib/Geo/GeoMath.cpp.

Same constants and same formulas as the firmware, so the generated Flights
section and the on-device Nearby Flights view agree about what is in range.
Pure: no network, no I/O.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

EARTH_RADIUS_MILES = 3958.7613
MILES_PER_DEGREE_LAT = 69.0

_COMPASS_POINTS = ("N", "NE", "E", "SE", "S", "SW", "W", "NW")


@dataclass(frozen=True)
class BoundingBox:
    lat_min: float
    lon_min: float
    lat_max: float
    lon_max: float


def _normalize_degrees(deg: float) -> float:
    d = math.fmod(deg, 360.0)
    return d + 360.0 if d < 0 else d


def distance_miles(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Great-circle distance between two WGS84 points, in miles."""
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    d_phi = math.radians(lat2 - lat1)
    d_lambda = math.radians(lon2 - lon1)

    a = math.sin(d_phi / 2) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(d_lambda / 2) ** 2
    c = 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))
    return EARTH_RADIUS_MILES * c


def initial_bearing_degrees(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Initial bearing from point 1 to point 2, degrees clockwise from north."""
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    d_lambda = math.radians(lon2 - lon1)

    y = math.sin(d_lambda) * math.cos(phi2)
    x = math.cos(phi1) * math.sin(phi2) - math.sin(phi1) * math.cos(phi2) * math.cos(d_lambda)
    return _normalize_degrees(math.degrees(math.atan2(y, x)))


def compass_point(bearing_deg: float) -> str:
    """Nearest of the 8 compass points. Any finite input is normalized first."""
    normalized = _normalize_degrees(bearing_deg)
    return _COMPASS_POINTS[int((normalized + 22.5) / 45.0) % 8]


def bounding_box(lat: float, lon: float, radius_miles: float) -> BoundingBox:
    """Axis-aligned box enclosing a radius around a point."""
    lat_span = radius_miles / MILES_PER_DEGREE_LAT

    miles_per_degree_lon = MILES_PER_DEGREE_LAT * math.cos(math.radians(lat))
    if miles_per_degree_lon < 1.0:
        miles_per_degree_lon = 1.0  # guard near the poles
    lon_span = radius_miles / miles_per_degree_lon

    return BoundingBox(lat - lat_span, lon - lon_span, lat + lat_span, lon + lon_span)

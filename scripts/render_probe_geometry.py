"""Authored geometry shared by the canvas-stress box image oracles."""

from PIL import Image, ImageChops

FLOOR_COLOR = (108, 109, 115)
BOX_HALF_CENTER_SPAN = (8.5, 2.5, 3.5)
FLOOR_HALF_SPAN = 60
FLOOR_TOP = 2
BOX_Z = -12


def convex_hull(points: list[tuple[float, float]]) -> list[tuple[float, float]]:
    def cross(origin, a, b):
        return ((a[0] - origin[0]) * (b[1] - origin[1])
                - (a[1] - origin[1]) * (b[0] - origin[0]))

    def half(sequence):
        result = []
        for point in sequence:
            while len(result) >= 2 and cross(result[-2], result[-1], point) <= 0:
                result.pop()
            result.append(point)
        return result[:-1]

    ordered = sorted(set(points))
    return half(ordered) + half(reversed(ordered))


def plate_bounds(image: Image.Image):
    diff = ImageChops.difference(image, Image.new("RGB", image.size, FLOOR_COLOR))
    red, green, blue = diff.split()
    plate = ImageChops.lighter(ImageChops.lighter(red, green), blue).point(
        lambda value: 255 if value == 0 else 0
    )
    bounds = plate.getbbox()
    if bounds is None:
        raise ValueError("receiver plate color is absent")
    left, top, right, bottom = bounds
    if right - left < 100 or bottom - top < 50:
        raise ValueError("receiver plate is too small")
    return bounds

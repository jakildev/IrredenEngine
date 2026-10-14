"""Execute both shader ray solvers against geometric and sampled field controls."""

import math
import random
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")

HARNESS = r"""
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
using uint = unsigned;
using std::min; using std::max; using std::sqrt; using std::abs;
struct vec2 {
    float x, y;
    vec2() = default;
    explicit vec2(float a): x(a), y(a) {}
    vec2(float a, float b): x(a), y(b) {}
    float& operator[](int i) { return i == 0 ? x : y; }
    float operator[](int i) const { return i == 0 ? x : y; }
};
struct vec3 {
    float x, y, z;
    vec3() = default;
    explicit vec3(float a): x(a), y(a), z(a) {}
    vec3(vec2 a, float b): x(a.x), y(a.y), z(b) {}
    vec3(float a, float b, float c): x(a), y(b), z(c) {}
    float& operator[](int i) { return i == 0 ? x : i == 1 ? y : z; }
    float operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
};
struct vec4 { float x, y, z, w;
    vec4() = default;
    vec4(float a, float b, float c, float d): x(a), y(b), z(c), w(d) {}
};
vec2 xy(vec3 a) { return {a.x, a.y}; }
vec2 operator-(vec2 a, vec2 b) { return {a.x-b.x,a.y-b.y}; }
vec3 operator+(vec3 a, vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
vec3 operator-(vec3 a, vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
vec3 operator*(vec3 a, float b) { return {a.x*b,a.y*b,a.z*b}; }
vec3 operator/(vec3 a, float b) { return {a.x/b,a.y/b,a.z/b}; }
float dot(vec2 a, vec2 b) { return a.x*b.x+a.y*b.y; }
float dot(vec3 a, vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
float length(vec2 a) { return sqrt(dot(a,a)); }
float length(vec3 a) { return sqrt(dot(a,a)); }
vec3 normalize(vec3 a) { return a / length(a); }
vec3 cross(vec3 a, vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
const uint SHAPE_SPHERE=1, SHAPE_CYLINDER=2, SHAPE_CONE=8, SHAPE_TORUS=9;
"""

DRIVER = r"""
int main() {
    uint type; vec4 params; vec3 origin, direction; float threshold, nearDepth, farDepth;
    while (std::cin >> type >> params.x >> params.y >> params.z >> params.w
           >> origin.x >> origin.y >> origin.z >> direction.x >> direction.y >> direction.z
           >> threshold >> nearDepth >> farDepth) {
        float depth=0;
        bool hit=sdfRayHit(type, params, origin, direction, threshold, nearDepth, farDepth, depth);
        std::cout << hit << ' ' << std::setprecision(10) << depth << '\n';
    }
}
"""


def build_shader_executables(directory, fragments, driver):
    executables = []
    for suffix, subdirectory in (("glsl", ""), ("metal", "metal/")):
        shader = "\n".join((ROOT / "engine/render/src/shaders" / subdirectory /
                            f"{fragment}.{suffix}").read_text() for fragment in fragments)
        shader = re.sub(r'^#include .*$', '', shader, flags=re.MULTILINE)
        shader = re.sub(r"\bfloat([234])\b", r"vec\1", shader)
        shader = re.sub(r"\bthread (float|vec[234])&", r"\1&", shader)
        shader = re.sub(r"\b(?:inout|out) (float|vec[234]) (\w+)", r"\1& \2", shader)
        shader = re.sub(r"\b(\w+)\.xy\b", r"xy(\1)", shader)
        # GLSL and Metal scalar arithmetic is float even for unsuffixed literals.
        shader = re.sub(r"(?<![\w.])(\d+\.\d+(?:e[+-]?\d+)?)(?![\w.])", r"\1f", shader)
        source = directory / f"{suffix}.cpp"
        source.write_text(HARNESS + shader + driver)
        executable = directory / suffix
        build = subprocess.run([COMPILER, "-std=c++17", "-O2", str(source),
                                "-o", str(executable)], capture_output=True, text=True)
        if build.returncode:
            raise RuntimeError(build.stderr)
        executables.append((suffix, executable))
    return executables


def field(kind, params, point):
    x, y, z = point
    radial = math.hypot(x, y)
    if kind == 1:
        return math.sqrt(x * x + y * y + z * z) - params[0]
    if kind == 9:
        return math.hypot(radial - params[0], z) - params[1]
    half_height = params[2] * 0.5
    radius = params[0]
    if kind == 8:
        radius *= 1 - min(1, max(0, (z + half_height) / (2 * half_height)))
    dr, dz = radial - radius, abs(z) - half_height
    return math.hypot(max(dr, 0), max(dz, 0)) + min(max(dr, dz), 0)


def clipped_ray(kind, params, origin, direction, threshold):
    if kind == 1:
        extent = [params[0] + threshold] * 3
    elif kind == 9:
        extent = [params[0] + params[1] + threshold] * 2 + [params[1] + threshold]
    else:
        extent = [params[0] + threshold] * 2 + [params[2] * 0.5 + threshold]
    near, far = -math.inf, math.inf
    for position, slope, bound in zip(origin, direction, extent):
        if slope == 0:
            if abs(position) > bound:
                return None
            continue
        a, b = (-bound - position) / slope, (bound - position) / slope
        near, far = max(near, min(a, b)), min(far, max(a, b))
    if near > far:
        return None
    return kind, params, origin, direction, threshold, near, far


@unittest.skipUnless(COMPILER, "shader ray controls require a C++ compiler")
class SdfRayRootsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.executables = build_shader_executables(
            Path(cls.temporary.name), ("ir_sdf_ray_roots",), DRIVER)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def check_rays(self, controls, tolerance=0.0004):
        rows = []
        for ray, _ in controls:
            kind, params, origin, direction, threshold, near, far = ray
            values = [kind, *params, *origin, *direction, threshold, near, far]
            rows.append(" ".join(map(str, values)))
        for backend, executable in self.executables:
            result = subprocess.run([str(executable)], input="\n".join(rows) + "\n",
                                    capture_output=True, text=True, check=True)
            output = result.stdout.splitlines()
            self.assertEqual(len(output), len(controls))
            for index, ((ray, expected), actual) in enumerate(zip(controls, output)):
                with self.subTest(backend=backend, index=index, ray=ray):
                    hit, depth = actual.split()
                    self.assertEqual(hit == "1", expected is not None)
                    if expected is not None:
                        self.assertAlmostEqual(float(depth), expected, delta=tolerance)

    def test_geometric_tangencies_holes_and_shallow_cone(self):
        threshold = 0.0625
        controls = []
        for kind, params, radius in ((1, (4, 4, 4, 0), 4 + threshold),
                                     (2, (3, 3, 7, 0), 3 + threshold),
                                     (9, (4, 2, 0, 0), 6 + threshold)):
            for inset in (0.0, 0.0001, 0.01, 0.5):
                offset = radius - inset
                ray = clipped_ray(kind, params, (0, offset, 0), (1, 0, 0), threshold)
                controls.append((ray, -math.sqrt(radius * radius - offset * offset)))
            controls.append((clipped_ray(kind, params, (0.6 * radius, 0.8 * radius, 0),
                                         (0.8, -0.6, 0), threshold), 0))
        controls.extend([
            (clipped_ray(9, (4, 2, 0, 0), (0, 0, 0), (0, 0, 1), threshold), None),
            (clipped_ray(9, (4, 2, 0, 0), (4, 0, 0), (0, 0, 1), threshold), -2 - threshold),
            (clipped_ray(8, (100, 100, 1, 0), (0, 0, 0.25), (1, 0, 0), 0.25), -25.25),
            (clipped_ray(1, (99.75, 99.75, 99.75, 0), (0, 99.99, 0), (1, 0, 0), 0.25),
             -math.sqrt(100 ** 2 - 99.99 ** 2)),
            (clipped_ray(8, (4, 4, 8, 0), (0, 0, 0), (0, 0, -1), threshold), -4 - threshold),
            ((9, (4, 2, 0, 0), (6, 0, 0), (1, 0, 0), threshold, 0, 0), 0),
            ((9, (4, 2, 0, 0), (0, 0, 0), (1, 0, 0), threshold, 0, 0), None),
        ])
        self.check_rays(controls, tolerance=0.0005)

    def test_rounded_caps_and_shifted_depth_interval(self):
        controls = []
        for kind, params in ((2, (3, 3, 7, 0)), (8, (4, 4, 8, 0))):
            for threshold in (0.5, 0.0625, 0.0078125):
                for fraction in (0.0, 0.5, 0.99):
                    below = threshold * fraction
                    radius = params[0] + math.sqrt(threshold * threshold - below * below)
                    z = -params[2] * 0.5 - below
                    ray = clipped_ray(kind, params, (0, 0, z), (1, 0, 0), threshold)
                    controls.append((ray, -radius))
        for shift in (-200, 200, 10000):
            controls.append((clipped_ray(9, (4, 2, 0, 0), (shift, 0, 0), (1, 0, 0), 0.0625),
                             -6.0625 - shift))
        self.check_rays(controls, tolerance=0.001)

    def test_horn_spindle_and_zero_threshold(self):
        controls = []
        for major, minor in ((2, 2), (2, 3), (0, 3)):
            for offset in (0, 0.5, 1.5, 2.0, 3.0):
                squared = minor * minor - (offset - major) ** 2
                expected = -math.sqrt(squared) if squared >= 0 else None
                controls.append((clipped_ray(9, (major, minor, 0, 0), (offset, 0, 0),
                                             (0, 0, 1), 0), expected))
        controls.extend([
            (clipped_ray(2, (3, 3, 7, 0), (0, 0, 0), (0, 0, 1), 0), -3.5),
            (clipped_ray(2, (3, 3, 7, 0), (0, 3, 0), (1, 0, 0), 0), 0),
            (clipped_ray(8, (4, 4, 8, 0), (0, 0, 0), (0, 0, -1), 0), -4),
            (clipped_ray(8, (4, 4, 8, 0), (0, 0, 4), (1, 0, 0), 0), 0),
            (clipped_ray(8, (4, 4, 8, 0), (0, 0, 0), (1, 0, 0), 0), -2),
        ])
        self.check_rays(controls)

    def test_seeded_rays_against_independent_field_bracketing(self):
        generator = random.Random(1703)
        controls = []
        for kind, params in ((1, (4, 4, 4, 0)), (2, (3, 3, 7, 0)),
                             (8, (4, 4, 8, 0)), (9, (4, 2, 0, 0)),
                             (9, (2, 2, 0, 0)), (9, (2, 3, 0, 0)),
                             (8, (4, 4, 0.1, 0)), (2, (3, 3, 0.1, 0))):
            for _ in range(120):
                origin = tuple(generator.uniform(-5, 5) for _ in range(3))
                vector = tuple(generator.uniform(-1, 1) for _ in range(3))
                magnitude = math.sqrt(sum(x * x for x in vector))
                direction = tuple(x / magnitude for x in vector)
                threshold = generator.choice((0.5, 0.0625, 0.0078125, 0.0))
                ray = clipped_ray(kind, params, origin, direction, threshold)
                if ray is None:
                    continue
                near, far = ray[-2:]

                def value(depth):
                    point = tuple(p + d * depth for p, d in zip(origin, direction))
                    return field(kind, params, point) - threshold

                previous = near
                minimum = math.inf
                expected = None
                for index in range(2049):
                    candidate = near + (far - near) * index / 2048
                    distance = value(candidate)
                    minimum = min(minimum, distance)
                    if distance <= 0:
                        left, right = previous, candidate
                        for _ in range(48):
                            middle = (left + right) * 0.5
                            if value(middle) <= 0:
                                right = middle
                            else:
                                left = middle
                        expected = right
                        break
                    previous = candidate
                # A coarse reference cannot classify an unsampled tangent.
                if expected is not None or minimum > (far - near) / 2048:
                    controls.append((ray, expected))
        self.assertGreater(len(controls), 250)
        self.check_rays(controls)


@unittest.skipUnless(COMPILER, "typed sun query controls require a C++ compiler")
class SourceShapeRayQueryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        driver = r"""
int main() {
    vec2 uv; float sunZ; uint type; vec3 center, axis, parameters;
    while (std::cin >> uv.x >> uv.y >> sunZ >> type
           >> center.x >> center.y >> center.z >> axis.x >> axis.y >> axis.z
           >> parameters.x >> parameters.y >> parameters.z) {
        float separation=sourceShapeRaySeparation(uv, sunZ, type, center, axis, parameters);
        std::cout << std::setprecision(10) << separation << '\n';
    }
}
"""
        cls.executables = build_shader_executables(
            Path(cls.temporary.name),
            ("ir_sdf_ray_roots", "ir_sdf_sun_ray", "ir_sun_shape_query"), driver)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def check_queries(self, controls):
        rows = []
        for query, _ in controls:
            uv, sun_z, kind, center, axis, parameters = query
            rows.append(" ".join(map(str, [*uv, sun_z, kind, *center, *axis, *parameters])))
        for backend, executable in self.executables:
            result = subprocess.run([str(executable)], input="\n".join(rows) + "\n",
                                    capture_output=True, text=True, check=True)
            output = result.stdout.splitlines()
            self.assertEqual(len(output), len(controls))
            for index, ((query, expected), actual) in enumerate(zip(controls, output)):
                with self.subTest(backend=backend, index=index, query=query):
                    separation = float(actual)
                    if expected is None:
                        self.assertLessEqual(separation, 0)
                    else:
                        self.assertAlmostEqual(separation, expected, delta=0.0005)

    def test_record_types_rotation_translation_and_receiver_order(self):
        controls = []
        for center in ((0, 0, 0), (12, -8, 50), (-32, 64, -120)):
            x, y, z = center
            controls.extend([
                (((x, y), z + 20, 1, center, (0.6, 0, 0.8), (4, 0, 0.0625)), 24.0625),
                (((x + 10, y), z + 20, 1, center, (0, 0, 1), (4, 0, 0.0625)), None),
                (((x, y), z - 10, 1, center, (0, 0, 1), (4, 0, 0.0625)), None),
                (((x, y), z + 20, 2, center, (0, 0.6, 0.8), (3, 7, 0)), 24.375),
                (((x, y), z + 20, 8, center, (0, 0.6, 0.8), (4, 8, 0)), 25),
                (((x, y), z + 20, 8, center, (0, -0.6, -0.8), (4, 8, 0)), 22),
                (((x, y), z + 20, 9, center, (0, 0, 1), (4, 2, 0.0625)), None),
                (((x + 4, y), z + 20, 9, center, (0, 0, 1), (4, 2, 0.0625)), 22.0625),
                (((x + 4, y), z - 3, 9, center, (0, 0, 1), (4, 2, 0.0625)), None),
                (((x, y), z + 20, 9, center, (0, 0.6, 0.8), (4, 2, 0)), None),
                (((x, y), z + 20, 9, center, (1, 0, 0), (4, 2, 0)), 26),
            ])
        self.check_queries(controls)

    def test_rotated_queries_against_axis_projection_field_oracle(self):
        generator = random.Random(2357)
        controls = []
        for kind, parameters in ((1, (4, 0, 0.0625)), (2, (3, 7, 0.0625)),
                                  (8, (4, 8, 0.0625)), (9, (4, 2, 0.0625))):
            for _ in range(64):
                vector = tuple(generator.uniform(-1, 1) for _ in range(3))
                magnitude = math.sqrt(sum(value * value for value in vector))
                axis = tuple(value / magnitude for value in vector)
                center = tuple(generator.uniform(-80, 80) for _ in range(3))
                delta = tuple(generator.uniform(-6, 6) for _ in range(2))
                uv = tuple(center[i] + delta[i] for i in range(2))
                radius, height_or_minor, threshold = parameters
                params = (radius, height_or_minor if kind == 9 else 0,
                          0 if kind == 9 else height_or_minor, 0)

                def value(offset):
                    displacement = (*delta, offset)
                    axial = sum(a * d for a, d in zip(axis, displacement))
                    radial = math.sqrt(max(0, sum(d * d for d in displacement) - axial * axial))
                    return field(kind, params, (radial, 0, axial)) - threshold

                expected = None
                previous = -12
                minimum = math.inf
                for index in range(2049):
                    candidate = -12 + 24 * index / 2048
                    distance = value(candidate)
                    minimum = min(minimum, distance)
                    if distance <= 0:
                        left, right = previous, candidate
                        for _ in range(48):
                            middle = (left + right) * 0.5
                            if value(middle) <= 0:
                                right = middle
                            else:
                                left = middle
                        expected = 20 - right
                        break
                    previous = candidate
                if expected is not None or minimum > 0.02:
                    query = (uv, center[2] + 20, kind, center, axis, parameters)
                    controls.append((query, expected))
        self.assertGreater(len(controls), 240)
        self.check_queries(controls)


if __name__ == "__main__":
    unittest.main()

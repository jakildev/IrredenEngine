"""Execute spatial shape depth and reconstruction in both shader dialects."""

import math
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_block, extract_function
from test_render_sdf_ray_roots import field as sdf_field

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")
SHADERS = ROOT / "engine/render/src/shaders"

VECTOR_TYPES = r"""
#include <algorithm>
#include <cmath>
#include <iostream>
using uint = unsigned;
using std::abs; using std::ceil; using std::floor; using std::max; using std::min;
using std::sqrt;
struct vec2 {
    float x, y;
    vec2(): x(0), y(0) {}
    explicit vec2(float a): x(a), y(a) {}
    vec2(float a, float b): x(a), y(b) {}
    float& operator[](int i) { return i == 0 ? x : y; }
    float operator[](int i) const { return i == 0 ? x : y; }
};
struct ivec2 {
    int x, y;
    ivec2(): x(0), y(0) {}
    explicit ivec2(int a): x(a), y(a) {}
    ivec2(int a, int b): x(a), y(b) {}
    explicit ivec2(vec2 a): x(int(a.x)), y(int(a.y)) {}
};
struct vec3 {
    float x, y, z;
    vec3(): x(0), y(0), z(0) {}
    explicit vec3(float a): x(a), y(a), z(a) {}
    vec3(float a, float b, float c): x(a), y(b), z(c) {}
    vec3(vec2 a, float b): x(a.x), y(a.y), z(b) {}
    float& operator[](int i) { return i == 0 ? x : i == 1 ? y : z; }
    float operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
};
struct vec4 {
    float x, y, z, w;
    vec4(): x(0), y(0), z(0), w(0) {}
    explicit vec4(float a): x(a), y(a), z(a), w(a) {}
    vec4(float a, float b, float c, float d): x(a), y(b), z(c), w(d) {}
    vec4(vec3 a, float b): x(a.x), y(a.y), z(a.z), w(b) {}
};
vec3 xyz(vec4 a) { return {a.x, a.y, a.z}; }
vec2 operator+(vec2 a, vec2 b) { return {a.x + b.x, a.y + b.y}; }
vec2 operator*(vec2 a, float b) { return {a.x * b, a.y * b}; }
ivec2 operator+(ivec2 a, ivec2 b) { return {a.x + b.x, a.y + b.y}; }
ivec2 operator-(ivec2 a, ivec2 b) { return {a.x - b.x, a.y - b.y}; }
ivec2 operator*(ivec2 a, int b) { return {a.x * b, a.y * b}; }
vec3 operator+(vec3 a, vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
vec3 operator-(vec3 a, vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
vec3 operator-(vec3 a) { return {-a.x, -a.y, -a.z}; }
vec3 operator*(vec3 a, float b) { return {a.x * b, a.y * b, a.z * b}; }
vec3 operator*(float a, vec3 b) { return b * a; }
vec3 operator/(vec3 a, float b) { return {a.x / b, a.y / b, a.z / b}; }
float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
float dot(vec4 a, vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
vec3 cross(vec3 a, vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}
float length(vec3 a) { return sqrt(dot(a, a)); }
float length(vec2 a) { return sqrt(dot(a, a)); }
vec3 normalize(vec3 a) { return a / length(a); }
vec2 floor(vec2 a) { return {floor(a.x), floor(a.y)}; }
"""

DEPTH_DRIVER = r"""
int main() {
    ivec2 iso;
    uint shapeType;
    vec4 params, viewRotation, entityRotation;
    float dExtent;
    while (std::cin >> iso.x >> iso.y >> shapeType
           >> params.x >> params.y >> params.z >> params.w >> dExtent
           >> viewRotation.x >> viewRotation.y >> viewRotation.z >> viewRotation.w
           >> entityRotation.x >> entityRotation.y >> entityRotation.z >> entityRotation.w) {
        std::cout << spatialSurfaceDepth(iso, shapeType, params, false, dExtent,
                                         viewRotation, entityRotation) << '\n';
    }
}
"""

ROUND_TRIP_DRIVER = r"""
bool close(vec3 a, vec3 b) {
    return abs(a.x - b.x) < 2e-4 && abs(a.y - b.y) < 2e-4 && abs(a.z - b.z) < 2e-4;
}
int main() {
    const vec4 rotations[] = {
        vec4(0.14572394f, 0.29144788f, -0.14572394f, 0.93412376f),
        vec4(0.15284789f, -0.07642395f, -0.22927184f, 0.95824388f),
    };
    int checks = 0;
    for (vec4 rotation : rotations) for (int scale : {1, 2, 5})
    for (int x : {-11, -2, 0, 7}) for (int y : {-9, 0, 5})
    for (int z : {-4, 1, 8}) {
        const vec3 viewPoint{float(x), float(y), float(z)};
        const vec3 worldPoint = rotateByQuat(viewPoint, rotation);
        const vec2 projected = spatialViewIso(worldPoint, rotation);
        const float projectedDepth = spatialViewDepth(worldPoint, rotation);
        if (abs(projected.x - round(projected.x)) > 2e-4 ||
            abs(projected.y - round(projected.y)) > 2e-4 ||
            abs(projectedDepth - round(projectedDepth)) > 2e-4) return 1;
        const ivec2 origin(37, -19);
        const vec2 cameraOffset(1.25f, -0.75f);
        const ivec2 options(1, scale);
        const ivec2 frameOffset = origin + ivec2(floor(cameraOffset * float(scale)));
        const ivec2 pixel = frameOffset + ivec2(int(round(projected.x)) * scale,
                                                int(round(projected.y)) * scale);
        const int rawDepth = int(round(projectedDepth)) * scale;
        const vec3 recovered = trixelCanvasPixelToWorld3DSpatial(
            pixel, rawDepth, origin, cameraOffset, options, rotation);
        if (!close(recovered, worldPoint)) return 2;
        ++checks;
    }
    std::cout << "spatial_round_trips=" << checks << '\n';
}
"""


def shader_source(suffix, mutation=None):
    folder = SHADERS / ("metal" if suffix == "metal" else "")
    common = (folder / f"ir_iso_common.{suffix}").read_text()
    producer = (folder / f"c_shapes_to_trixel_body.{suffix}").read_text()
    roots = (folder / f"ir_sdf_ray_roots.{suffix}").read_text()
    functions = "\n".join(extract_function(common, name) for name in (
        "isoPositionToPos3D", "isoPixelToPos3D", "isoToLocal3D",
        "rotateByQuat", "rotateByInverseQuat"))
    spatial = extract_function(producer, "spatialSurfaceDepth")
    if mutation == "drop_view_rotation":
        vector_type = "vec3" if suffix == "glsl" else "float3"
        literal = "1.0"
        targets = (
            "rotateByQuat(isoToLocal3D(isoRel, 0.0), viewRotation)",
            f"rotateByQuat(normalize({vector_type}({literal})), viewRotation)",
        )
        count = 0
        for target in targets:
            count += spatial.count(target)
            spatial = spatial.replace(target, target[len("rotateByQuat("):-len(", viewRotation)")])
        if count != 2:
            raise ValueError("view-rotation mutation did not reach the spatial queries")
    elif mutation == "wrong_depth_scale":
        spatial, count = re.subn(r"entry \* sqrt\(3\.0f?\)", "entry", spatial)
        if count != 1:
            raise ValueError("depth-scale mutation did not reach the analytic return")
    source = roots + "\n" + functions + "\n" + spatial
    source = re.sub(r'^#include .*$', '', source, flags=re.MULTILINE)
    source = re.sub(r"\bfloat([234])\b", r"vec\1", source)
    source = re.sub(r"\bint2\b", "ivec2", source)
    source = re.sub(r"\bthread (float|vec[234])&", r"\1&", source)
    source = re.sub(r"\b(?:inout|out) (float|vec[234]) (\w+)", r"\1& \2", source)
    source = re.sub(r"\b(\w+)\.xy\b", r"vec2(\1.x, \1.y)", source)
    source = re.sub(r"\b(\w+)\.xyz\b", r"xyz(\1)", source)
    source = source.replace("constant ", "const ")
    source = re.sub(r"(?<![\w.])(\d+\.\d+(?:e[+-]?\d+)?)f?(?![\w.])", r"\1f", source)
    declarations = r"""
const uint SHAPE_BOX=0, SHAPE_SPHERE=1, SHAPE_CYLINDER=2, SHAPE_CONE=8, SHAPE_TORUS=9;
const int kInvalidDepth = 2147483647;
const float kSdfBiasEpsilon = 1.0e-3f;
float evaluateSDF(vec3, uint, vec4) { return 1.0e30f; }
"""
    return VECTOR_TYPES + declarations + source + DEPTH_DRIVER


def round_trip_source(suffix, mutation=None):
    folder = SHADERS / ("metal" if suffix == "metal" else "")
    common = (folder / f"ir_iso_common.{suffix}").read_text()
    names = (
        "isoPositionToPos3D", "isoPixelToPos3D", "effectiveTrixelSubdivisionScale",
        "rotateByQuat",
        "rotateByInverseQuat", "spatialViewIso", "spatialViewDepth")
    source = "\n".join(extract_function(common, name) for name in names)
    vector_type = "ivec2" if suffix == "glsl" else "int2"
    source += "\n" + extract_block(common, f"{vector_type} trixelFrameOffset")
    source += "\n" + extract_block(common, f"{vector_type} trixelCanvasPixelToIsoRel")
    source += "\n" + extract_function(common, "trixelCanvasPixelToWorld3DSpatial")
    if mutation == "drop_inverse_rotation":
        source, count = re.subn(
            r"return rotateByQuat\((position / float\(effectiveTrixelSubdivisionScale"
            r"\(renderOptions\)\)), viewRotation\);",
            r"return \1;", source)
        if count != 1:
            raise ValueError("inverse-rotation mutation did not reach reconstruction")
    source = re.sub(r"\bfloat([234])\b", r"vec\1", source)
    source = re.sub(r"\bint2\b", "ivec2", source)
    source = re.sub(r"\b(\w+)\.xyz\b", r"xyz(\1)", source)
    source = source.replace("constant ", "const ").replace("inline ", "")
    source = re.sub(r"(?<![\w.])(\d+\.\d+)f(?![\w.])", r"\1", source)
    return VECTOR_TYPES + source + ROUND_TRIP_DRIVER


def compile_source(directory, name, source):
    cpp = directory / f"{name}.cpp"
    executable = directory / name
    cpp.write_text(source)
    build = subprocess.run([COMPILER, "-std=c++17", "-O2", str(cpp), "-o", str(executable)],
                           capture_output=True, text=True)
    if build.returncode:
        raise RuntimeError(build.stderr)
    return executable


def quaternion(axis, angle):
    magnitude = math.sqrt(sum(value * value for value in axis))
    sine = math.sin(angle * 0.5) / magnitude
    return tuple(value * sine for value in axis) + (math.cos(angle * 0.5),)


def rotate(vector, rotation):
    x, y, z = vector
    qx, qy, qz, qw = rotation
    tx = 2 * (qy * z - qz * y)
    ty = 2 * (qz * x - qx * z)
    tz = 2 * (qx * y - qy * x)
    return (x + qw * tx + qy * tz - qz * ty,
            y + qw * ty + qz * tx - qx * tz,
            z + qw * tz + qx * ty - qy * tx)


def inverse_rotate(vector, rotation):
    return rotate(vector, tuple(-value for value in rotation[:3]) + (rotation[3],))


def sdf(shape_type, params, point):
    if shape_type == 0:
        half = tuple(value * 0.5 for value in params[:3])
        q = tuple(abs(value) - extent for value, extent in zip(point, half))
        outside = math.sqrt(sum(max(value, 0) ** 2 for value in q))
        return outside + min(max(q), 0)
    return sdf_field(shape_type, params, point)


def reference_depth(iso, shape_type, params, d_extent, view_rotation, entity_rotation):
    iso_x, iso_y = iso
    view_origin = ((-3 * iso_x - iso_y) / 6,
                   (3 * iso_x - iso_y) / 6, iso_y / 3)
    origin = inverse_rotate(rotate(view_origin, view_rotation), entity_rotation)
    direction = inverse_rotate(rotate((1 / math.sqrt(3),) * 3, view_rotation),
                               entity_rotation)
    near, far = -d_extent / math.sqrt(3), d_extent / math.sqrt(3)

    def value(distance):
        point = tuple(base + distance * slope for base, slope in zip(origin, direction))
        return sdf(shape_type, params, point) - 0.5

    previous = near
    if value(previous) <= 0:
        entry = previous
    else:
        entry = None
        for index in range(1, 16385):
            candidate = near + (far - near) * index / 16384
            if value(candidate) <= 0:
                left, right = previous, candidate
                for _ in range(45):
                    middle = (left + right) * 0.5
                    if value(middle) <= 0:
                        right = middle
                    else:
                        left = middle
                entry = right
                break
            previous = candidate
    return None if entry is None else math.ceil(entry * math.sqrt(3) - 1.0e-3)


@unittest.skipUnless(COMPILER, "spatial shader contracts require a C++ compiler")
class SpatialShapeProjectionTest(unittest.TestCase):
    def test_spatial_surface_depth_against_reference_rays_and_mutations(self):
        rotations = (
            (quaternion((1, 2, -1), 0.73), quaternion((0, 0, 1), -0.41)),
            (quaternion((-2, 1, 3), -0.58), quaternion((1, -1, 0.5), 0.36)),
        )
        shapes = ((0, (5, 3, 7, 0)), (1, (3.5, 0, 0, 0)),
                  (2, (3, 3, 6, 0)), (8, (4, 4, 7, 0)), (9, (4, 1.5, 0, 0)))
        controls = []
        for view_rotation, entity_rotation in rotations:
            for shape_type, params in shapes:
                for iso_x in range(-7, 8, 2):
                    for iso_y in range(-7, 8, 2):
                        expected = reference_depth((iso_x, iso_y), shape_type, params, 24,
                                                   view_rotation, entity_rotation)
                        if shape_type == 0 and expected is None:
                            continue
                        controls.append(((iso_x, iso_y, shape_type, params, 24,
                                          view_rotation, entity_rotation), expected))
        self.assertGreater(sum(expected is not None for _, expected in controls), 150)
        rows = []
        for query, _ in controls:
            iso_x, iso_y, shape_type, params, extent, view, entity = query
            rows.append(" ".join(map(str, (iso_x, iso_y, shape_type, *params, extent,
                                            *view, *entity))))
        input_text = "\n".join(rows) + "\n"
        for suffix in ("glsl", "metal"):
            with tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                for mutation in (None, "drop_view_rotation", "wrong_depth_scale"):
                    with self.subTest(backend=suffix, mutation=mutation or "production"):
                        executable = compile_source(
                            directory, f"{suffix}_{mutation or 'production'}",
                            shader_source(suffix, mutation))
                        result = subprocess.run([str(executable)], input=input_text,
                                                capture_output=True, text=True, check=True)
                        actual = [int(value) for value in result.stdout.splitlines()]
                        wanted = [2147483647 if expected is None else expected
                                  for _, expected in controls]
                        if mutation is None:
                            for index, (observed, expected) in enumerate(zip(actual, wanted)):
                                with self.subTest(backend=suffix, ray=index):
                                    if expected == 2147483647:
                                        self.assertEqual(observed, expected)
                                    else:
                                        self.assertLessEqual(abs(observed - expected), 1)
                        else:
                            escaped = all(
                                (observed == expected == 2147483647) or
                                (expected != 2147483647 and observed != 2147483647 and
                                 abs(observed - expected) <= 1)
                                for observed, expected in zip(actual, wanted))
                            self.assertFalse(escaped, "mutation survived the reference rays")

    def test_spatial_projection_round_trip_and_mutation(self):
        for suffix in ("glsl", "metal"):
            with tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                for mutation in (None, "drop_inverse_rotation"):
                    with self.subTest(backend=suffix, mutation=mutation or "production"):
                        executable = compile_source(
                            directory, f"round_trip_{suffix}_{mutation or 'production'}",
                            round_trip_source(suffix, mutation))
                        result = subprocess.run([str(executable)], capture_output=True, text=True)
                        if mutation is None:
                            self.assertEqual(result.returncode, 0, result.stderr)
                            self.assertIn("spatial_round_trips=216", result.stdout)
                        else:
                            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()

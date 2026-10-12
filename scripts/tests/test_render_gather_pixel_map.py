"""Execute the gather pixel map and the unit-vector UNORM8 encode from both backends.

Both helpers live in `ir_iso_common` (GLSL and Metal). The C++ adapter compiles
each backend's source text and checks the arithmetic contract: at the
orbit-6 fixture's placement every pixel centre maps to an exactly integral
canvas row, the two backends select the same texels, and the three axis
values encode to the exact codes 0, 128 and 255. GPU interpolation, binding
and the hardware store are covered by the IRCanvasStress orbit-geometry rows.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_block, extract_function

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "engine/render/src/shaders"
COMPILER = shutil.which("c++")

PREAMBLE = """
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
struct vec2 {
    float x, y;
    explicit vec2(float s) : x(s), y(s) {}
    vec2(float a, float b) : x(a), y(b) {}
};
struct ivec3 { int x, y, z; };
struct vec3 {
    float x, y, z;
    explicit vec3(float s) : x(s), y(s), z(s) {}
    vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    explicit vec3(ivec3 v) : x(float(v.x)), y(float(v.y)), z(float(v.z)) {}
};
struct vec4 {
    float x, y, z, w;
    vec2 xy, zw;
    vec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d), xy(a, b), zw(c, d) {}
};
inline vec2 operator+(vec2 a, vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline vec2 operator-(vec2 a, vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline vec2 operator*(vec2 a, vec2 b) { return {a.x * b.x, a.y * b.y}; }
inline vec2 operator*(float s, vec2 a) { return {s * a.x, s * a.y}; }
inline vec3 operator+(vec3 a, float s) { return {a.x + s, a.y + s, a.z + s}; }
inline vec3 operator+(vec3 a, vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline vec3 operator*(vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline vec3 operator/(vec3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }
inline vec3 floor(vec3 v) { return {std::floor(v.x), std::floor(v.y), std::floor(v.z)}; }
inline ivec3 make_ivec3(vec3 v) { return {int(v.x), int(v.y), int(v.z)}; }
using float2 = vec2;
using float3 = vec3;
using float4 = vec4;
using int3 = ivec3;
#define ivec3(v) make_ivec3(v)
#define int3(v) make_ivec3(v)
"""

CHECKS = """
static unsigned unorm8(float v) { return unsigned(std::floor(v * 255.0f + 0.5f)); }

int main() {
    const vec3 axes = encodeUnitVectorUnorm8(vec3(-1.0f, 0.0f, 1.0f));
    const float exact[3] = {0.0f / 255.0f, 128.0f / 255.0f, 255.0f / 255.0f};
    const float got[3] = {axes.x, axes.y, axes.z};
    for (int i = 0; i < 3; ++i) {
        if (std::memcmp(&got[i], &exact[i], sizeof(float)) != 0 ||
            unorm8(got[i]) != unsigned(i == 0 ? 0 : i == 1 ? 128 : 255)) {
            std::fprintf(stderr, "encode of axis value %d is not the exact code\\n", i - 1);
            return 1;
        }
    }

    // Orbit-6 cardinal fixture: quad centre (642, 361) in a 722-row raster
    // target, one canvas column and two canvas rows per raster pixel.
    const vec4 map(642.0f, 361.0f, 1.0f, 2.0f);
    const vec2 textureSize(642.0f, 722.0f), textureOffset(0.0f);
    std::uint64_t hash = 1469598103934665603ull;
    for (int gy = 210; gy < 510; ++gy) {
        for (int gx = 500; gx < 780; ++gx) {
            const vec2 pixel(float(gx) + 0.5f, 722.0f - float(gy) - 0.5f);
            const vec2 origin = canvasPositionFromPixel(pixel, map, textureOffset, textureSize);
            const double rowRef = -(double(pixel.y) - 361.0) * 2.0 + 361.0;
            const double colRef = (double(pixel.x) - 642.0) * 1.0 + 321.0;
            if (origin.y != std::floor(origin.y) || double(origin.y) != rowRef) {
                std::fprintf(stderr, "canvas row of pixel (%d, %d) is not exact\\n", gx, gy);
                return 2;
            }
            if (double(origin.x) != colRef) {
                std::fprintf(stderr, "canvas column of pixel (%d, %d) is not exact\\n", gx, gy);
                return 3;
            }
            const std::int32_t texel[2] = {std::int32_t(std::floor(origin.x)),
                                           std::int32_t(std::floor(origin.y))};
            const unsigned char *bytes = reinterpret_cast<const unsigned char *>(texel);
            for (unsigned i = 0; i < sizeof(texel); ++i)
                hash = (hash ^ bytes[i]) * 1099511628211ull;
        }
    }
    std::printf("texels=%016llx\\n", static_cast<unsigned long long>(hash));
}
"""


def helpers(backend: str) -> str:
    if backend == "glsl":
        source = (SHADERS / "ir_iso_common.glsl").read_text()
        round_vec3 = "ivec3 roundHalfUp(vec3"
    else:
        source = (SHADERS / "metal/ir_iso_common.metal").read_text()
        round_vec3 = "inline int3 roundHalfUp(float3"
    return "\n".join((extract_block(source, round_vec3),
                      extract_function(source, "encodeUnitVectorUnorm8"),
                      extract_function(source, "canvasPositionFromPixel")))


def run_program(text: str) -> subprocess.CompletedProcess:
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary)
        cpp, executable = path / "gather.cpp", path / "gather"
        cpp.write_text(PREAMBLE + text + CHECKS)
        build = subprocess.run(
            [COMPILER, "-std=c++17", "-O2", str(cpp), "-o", str(executable)],
            capture_output=True, text=True)
        if build.returncode != 0:
            raise AssertionError(build.stderr)
        return subprocess.run([str(executable)], capture_output=True, text=True)


@unittest.skipUnless(COMPILER, "shader arithmetic controls require a C++ compiler")
class GatherPixelMapTest(unittest.TestCase):
    def test_backends_agree_and_select_exact_texels(self):
        outputs = {}
        for backend in ("glsl", "metal"):
            with self.subTest(backend=backend):
                result = run_program(helpers(backend))
                self.assertEqual(result.returncode, 0, result.stderr)
                outputs[backend] = result.stdout.strip()
                self.assertRegex(outputs[backend], r"^texels=[0-9a-f]{16}$")
        self.assertEqual(outputs["glsl"], outputs["metal"])

    def test_mutation_controls(self):
        for backend in ("glsl", "metal"):
            source = helpers(backend)
            mutations = (
                ("scale_one_ulp_short",
                 source.replace("canvasFromPixel.zw", "(canvasFromPixel.zw * vec2(0.99999994f))"),
                 "canvas row"),
                ("encode_midpoint_off_by_half",
                 re.sub(r"\+ 127\.5f?\)", "+ 127.0f)", source), "encode"),
            )
            for name, text, failure in mutations:
                with self.subTest(backend=backend, variant=name):
                    self.assertNotEqual(text, source, "mutation did not alter the source")
                    result = run_program(text)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(failure, result.stderr)


if __name__ == "__main__":
    unittest.main()

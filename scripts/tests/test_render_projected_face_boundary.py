"""Run production finite-face queries against independently rounded edge points."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")

PREAMBLE = r"""
#include <cmath>
#include <cstdint>
#include <iostream>
using std::abs;
using uint = std::uint32_t;
struct vec2 { float x, y; vec2(float a, float b): x(a), y(b) {} };
struct vec3 { float x, y, z; };
"""

CASES = r"""
int main() {
    int samples = 0;
    const double positions[] = {0.0, .25, .5, .75, 1.0};
    for (double scale : {.125, 1.0, 8.0}) {
        for (double offset : {-64.0, -.125, 0.0, .125, 64.0}) {
            for (int step = 0; step < 24; ++step) {
                for (double handedness : {-1.0, 1.0}) {
                    const double angle = step * .2617993877991494;
                    const double ax = scale * std::cos(angle);
                    const double ay = scale * std::sin(angle);
                    const double bx = scale * std::cos(angle + handedness * 1.1);
                    const double by = scale * std::sin(angle + handedness * 1.1);
                    const double cx = offset + .137, cy = -offset + .291;
                    const vec3 corner{float(cx), float(cy), 0};
                    const vec3 a{float(ax), float(ay), .1f};
                    const vec3 b{float(bx), float(by), -.2f};
                    // Construct on the exact double-precision parallelogram,
                    // then independently round query, origin and basis to float.
                    for (double u : positions) for (double v : positions) {
                        const vec2 point{float(cx + u*ax + v*bx),
                                         float(cy + u*ay + v*by)};
                        const float hit = sourceFaceRaySeparation(point, 2, corner, a, b);
                        if (hit < 0) {
                            std::cerr << "closed boundary missed: " << scale << ' '
                                      << offset << ' ' << step << ' ' << handedness
                                      << ' ' << u << ' ' << v << '\n';
                            return 1;
                        }
                        if (abs(hit - (2 - u*.1 + v*.2)) > .0002) return 2;
                        ++samples;
                    }
                    // These gaps are outside binary32 uncertainty for this grid.
                    for (double outside : {-.002, 1.002}) {
                        for (int axis = 0; axis < 2; ++axis) {
                            const double u = axis == 0 ? outside : .5;
                            const double v = axis == 1 ? outside : .5;
                            const vec2 point{float(cx + u*ax + v*bx),
                                             float(cy + u*ay + v*by)};
                            if (sourceFaceRaySeparation(point, 2, corner, a, b) != -1) {
                                std::cerr << "exterior accepted\n";
                                return 3;
                            }
                            ++samples;
                        }
                    }
                }
            }
        }
    }
    // A grazing unit face's projected width can approach coordinate roundoff.
    // Conservative boundary ownership must still interpolate finite depth.
    for (float offset : {0.0f, 100.0f, -100.0f}) {
        for (float sign : {-1.0f, 1.0f}) {
            const vec3 corner{offset, offset, 0};
            for (bool swapAxes : {false, true}) {
                const vec3 wide{1, 0, 0}, grazing{0, sign * 1e-5f, 1};
                const vec3 a = swapAxes ? grazing : wide;
                const vec3 b = swapAxes ? wide : grazing;
                for (float v : {-1.0f, 0.0f, .5f, 1.0f, 2.0f}) {
                    const vec2 point{offset + .5f, offset + sign * v * 1e-5f};
                    const float separation = sourceFaceRaySeparation(point, 2, corner, a, b);
                    if (separation != -1 && (separation < 1 || separation > 2)) return 4;
                    ++samples;
                }
                if (sourceFaceRaySeparation({offset + .5f, offset + .01f},
                                           2, corner, a, b) != -1) return 5;
                ++samples;
            }
        }
    }
    std::cout << samples << " finite-face boundary/exterior checks\n";
}
"""


def query_source(suffix, mutation):
    directory = ROOT / "engine/render/src/shaders"
    if suffix == "metal":
        directory /= "metal"
    layout = (directory / f"ir_sun_face_query_layout.{suffix}").read_text()
    geometry = (directory / f"ir_projected_face.{suffix}").read_text()
    source = layout.replace(f'#include "ir_projected_face.{suffix}"', geometry)
    source = (source.replace("constant uint", "const uint")
              .replace("float2", "vec2").replace("float3", "vec3"))
    if mutation in ("unbounded_u", "unbounded_v"):
        before, after = (r"\bu \* edgeU.z", "uv.x * edgeU.z") if mutation == "unbounded_u" else (
            r"\bv \* edgeV.z", "uv.y * edgeV.z")
        source, count = re.subn(before, after, source)
        if count != 1:
            raise ValueError("depth mutation did not reach exactly one query")
    elif mutation:
        gate = r"(?<=if \()!projectedFaceContainsClosed\([\s\S]*?, uv\)(?=\) \{)"
        replacement = (
            "uv.x < 0 || uv.y < 0 || uv.x > 1 || uv.y > 1"
            if mutation == "strict" else
            "uv.x < -.01 || uv.y < -.01 || uv.x > 1.01 || uv.y > 1.01"
        )
        source, count = re.subn(gate, replacement, source)
        if count != 1:
            raise ValueError("finite-footprint mutation did not reach exactly one query")
    return PREAMBLE + source + CASES


@unittest.skipUnless(COMPILER, "projected-face controls require a C++ compiler")
class ProjectedFaceBoundaryTest(unittest.TestCase):
    def test_closed_boundaries_and_exterior_controls(self):
        for suffix in ("glsl", "metal"):
            for mutation in (None, "strict", "inflated", "unbounded_u", "unbounded_v"):
                with (self.subTest(backend=suffix, mutation=mutation),
                      tempfile.TemporaryDirectory() as tmp):
                    directory = Path(tmp)
                    source = directory / "boundary.cpp"
                    binary = directory / "boundary"
                    source.write_text(query_source(suffix, mutation))
                    build = subprocess.run(
                        [COMPILER, "-std=c++17", "-O2", "-ffp-contract=off",
                         str(source), "-o", str(binary)], capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(binary)], capture_output=True, text=True)
                    expected = {None: 0, "strict": 1, "inflated": 3,
                                "unbounded_u": 4, "unbounded_v": 4}[mutation]
                    self.assertEqual(run.returncode, expected, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()

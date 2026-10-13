"""Execute smooth-shape caster eligibility and the shared rotation deadband."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_function

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "engine/render/src/shaders"
COMPILER = shutil.which("c++")

HARNESS = r"""
#include <cmath>
#include <cstdio>
#include <initializer_list>
using std::abs;
using uint = unsigned;
struct ivec2 { int x, y; };
struct vec4 { float x, y, z, w; };
struct ShapeDescriptor { uint shapeType, flags; };
constexpr uint SHAPE_SPHERE = 1, SHAPE_CYLINDER = 2, SHAPE_CONE = 8, SHAPE_TORUS = 9;
"""
CHECKS = r"""
int main() {
    int failures = 0;
    // Density alone cannot turn a lattice shape into an analytic caster.
    struct Mode { ivec2 options; int yaw, lattice; bool continuous; };
    Mode modes[] = {{{0,1},0,0,false}, {{0,8},0,0,false}, {{1,1},0,0,false},
                    {{1,8},0,0,true}, {{0,1},1,0,true}, {{0,8},1,1,false},
                    {{1,8},1,1,true}};
    for (auto mode : modes) for (uint type = 0; type <= 9; ++type) {
        bool supported = type == 1 || type == 2 || type == 8 || type == 9;
        for (uint flags : {0u, 1u, 8u, 9u, 16u, 17u}) {
            bool expected = mode.continuous && supported && !(flags & 1u);
            failures += shapeUsesSunRay({type, flags}, mode.options,
                                       mode.yaw, mode.lattice) != expected;
        }
    }
    for (float sign : {-1.f, 1.f}) {
        failures += shapeHasEntityRotation({0,0,0,sign});
        failures += shapeHasEntityRotation({0,0,0,sign * 0.99995f});
        failures += !shapeHasEntityRotation({0,0,0,sign * 0.9998f});
        failures += !shapeHasEntityRotation({0,0,0,sign * 0.7071f});
    }
    std::printf("failures=%d\n", failures);
    return failures ? 1 : 0;
}
"""


@unittest.skipUnless(COMPILER, "shader routing checks require a C++ compiler")
class ShapeSunRoutingTest(unittest.TestCase):
    def test_modes_and_rotation_signs(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal")):
            directory = SHADERS / folder
            route = extract_function(
                (directory / f"ir_shape_sun_ray.{suffix}").read_text(), "shapeUsesSunRay")
            rotation = extract_function(
                (directory / f"ir_shape_data.{suffix}").read_text(), "shapeHasEntityRotation")
            code = (HARNESS + route + rotation + CHECKS).replace("int2", "ivec2")
            code = code.replace("float4", "vec4")
            with self.subTest(backend=suffix), tempfile.TemporaryDirectory() as tmp:
                source, binary = Path(tmp) / "routing.cpp", Path(tmp) / "routing"
                source.write_text(code)
                build = subprocess.run([COMPILER, "-std=c++17", str(source), "-o", str(binary)],
                                       capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stderr)
                run = subprocess.run([str(binary)], capture_output=True, text=True)
                self.assertEqual(run.returncode, 0, run.stdout)

    def test_display_and_caster_consume_shared_contracts(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal")):
            for shader in ("c_shapes_to_trixel_body", "c_bake_box_sun_shadow"):
                source = (SHADERS / folder / f"{shader}.{suffix}").read_text()
                with self.subTest(backend=suffix, shader=shader):
                    self.assertIn("shapeUsesSunRay(shape,", source)
                    self.assertIn("shapeHasEntityRotation(shape.rotation)", source)


if __name__ == "__main__":
    unittest.main()

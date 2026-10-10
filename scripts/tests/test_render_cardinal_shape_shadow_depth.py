"""Execute the cardinal shape caster/receiver depth convention in both backends."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_function

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "engine/render/src/shaders"
COMPILER = shutil.which("c++")

CHECKS = r"""
#include <cstdio>
#include <initializer_list>
int main() {
    int checks = 0;
    int failures = 0;
    for (int subdivisions : {1, 2, 3, 8}) {
        const int offset = cardinalRasterLatticeDepthOffset(subdivisions);
        const int expectedStored = IR_SHAPE_PASS == 2 ? 100 : 100 + offset;
        failures += cardinalShapeStoredDepth(100, true, subdivisions) != expectedStored;
        failures += cardinalShapeStoredDepth(100, false, subdivisions) != 100;
        checks += 2;
        for (bool owned : {false, true}) {
            for (bool carries : {false, true}) {
                for (bool finite : {false, true}) {
                    const int expected = owned && carries && finite ? 100 - offset : 100;
                    failures += cardinalShapeReceiverDepth(
                        100, subdivisions, owned, carries, finite) != expected;
                    ++checks;
                }
            }
        }
    }
    std::printf("checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
"""


def compile_and_run(caster, receiver, offset, shape_pass):
    code = (f"#define IR_SHAPE_PASS {shape_pass}\n" + offset + "\n" + caster +
            "\n" + receiver + CHECKS)
    code = code.replace("inline ", "")
    with tempfile.TemporaryDirectory() as tmp:
        cpp = Path(tmp) / "depth.cpp"
        exe = Path(tmp) / "depth"
        cpp.write_text(code)
        build = subprocess.run(
            [COMPILER, "-std=c++17", str(cpp), "-o", str(exe)],
            capture_output=True, text=True)
        if build.returncode != 0:
            return build.returncode, build.stderr
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        return run.returncode, run.stdout


@unittest.skipUnless(COMPILER, "cardinal shape shadow checks require a C++ compiler")
class CardinalShapeShadowDepthTest(unittest.TestCase):
    def sources(self, suffix, folder):
        shaders = SHADERS / folder
        common = (shaders / f"ir_iso_common.{suffix}").read_text()
        shape_data = (shaders / f"ir_shape_data.{suffix}").read_text()
        return (
            extract_function(shape_data, "cardinalShapeStoredDepth"),
            extract_function(shape_data, "cardinalShapeReceiverDepth"),
            extract_function(common, "cardinalRasterLatticeDepthOffset"),
        )

    def test_caster_pass_omits_offset_and_display_passes_keep_it(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            caster, receiver, offset = self.sources(suffix, folder)
            for shape_pass in (0, 1, 2, 3):
                with self.subTest(backend=suffix, shape_pass=shape_pass):
                    code, output = compile_and_run(caster, receiver, offset, shape_pass)
                    self.assertEqual(code, 0, output)
                    self.assertIn("failures=0", output)

    def test_positive_controls_reject_either_half_reverted(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            caster, receiver, offset = self.sources(suffix, folder)
            caster_mutant = caster.replace("#if IR_SHAPE_PASS == 2", "#if 0")
            receiver_mutant = receiver.replace(
                "shapeOwned && carriesLatticeOffset && finiteCoverage",
                "shapeOwned && carriesLatticeOffset")
            self.assertNotEqual(caster_mutant, caster)
            self.assertNotEqual(receiver_mutant, receiver)
            with self.subTest(backend=suffix, mutant="caster"):
                code, output = compile_and_run(caster_mutant, receiver, offset, 2)
                self.assertEqual(code, 1, output)
            with self.subTest(backend=suffix, mutant="receiver"):
                code, output = compile_and_run(caster, receiver_mutant, offset, 2)
                self.assertEqual(code, 1, output)


if __name__ == "__main__":
    unittest.main()

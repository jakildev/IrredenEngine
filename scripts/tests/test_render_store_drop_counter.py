"""Execute the production per-axis store-drop counter for both shader dialects."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_render_per_axis_caster_position import COMPILER, ROOT, functions

SHADERS = (
    ROOT / "engine/render/src/shaders/c_voxel_to_trixel_stage_1_body.glsl",
    ROOT / "engine/render/src/shaders/metal/c_voxel_to_trixel_stage_1_body.metal",
)

HOST = r"""
#include <cstdio>
using uint = unsigned;
struct ivec2 { int x, y; };
struct ivec4 { int x, y, z, w; };
ivec4 overflowScratchLayout{0, 12, 0, 0};
uint perAxisWinnerIds[32]{};
int triangleCanvasDistances = 0;
int distanceScratch[4]{99, 99, 99, 99};
ivec2 canvasSize{2, 2};
bool isInsideCanvas(ivec2 p, ivec2 size) {
    return p.x >= 0 && p.y >= 0 && p.x < size.x && p.y < size.y;
}
ivec2 imageSize(int) { return canvasSize; }
void imageAtomicMin(int, ivec2 p, int value) {
    const int i = p.y * canvasSize.x + p.x;
    if (value < distanceScratch[i]) distanceScratch[i] = value;
}
uint atomicAdd(uint& slot, uint value) { const uint old=slot; slot+=value; return old; }
struct FrameDataVoxelToTrixel { ivec4 overflowScratchLayout; };
constexpr int memory_order_relaxed=0;
uint atomic_fetch_add_explicit(uint* slot,uint value,int) {
    return atomicAdd(*slot,value);
}
void atomic_fetch_min_explicit(int* slot,int value,int) {
    if (value < *slot) *slot=value;
}
"""

CHECKS_GLSL = r"""
int main() {
    writePerAxisDistanceTap({0,0}, 7, true);
    writePerAxisDistanceTap({-1,0}, 8, true);
    writePerAxisDistanceTap({2,0}, 9, true);
    writePerAxisDistanceTap({2,0}, 10, false);
    if (distanceScratch[0] != 7 || perAxisWinnerIds[20] != 2 ||
        perAxisWinnerIds[19] != 0) return 1;
    std::puts("store drop counter passed");
}
"""

CHECKS_METAL = r"""
int main() {
    FrameDataVoxelToTrixel frame{overflowScratchLayout};
    writePerAxisDistanceTap({0,0}, 7, true, frame, distanceScratch,
                            perAxisWinnerIds, canvasSize);
    writePerAxisDistanceTap({-1,0}, 8, true, frame, distanceScratch,
                            perAxisWinnerIds, canvasSize);
    writePerAxisDistanceTap({2,0}, 9, true, frame, distanceScratch,
                            perAxisWinnerIds, canvasSize);
    writePerAxisDistanceTap({2,0}, 10, false, frame, distanceScratch,
                            perAxisWinnerIds, canvasSize);
    if (distanceScratch[0] != 7 || perAxisWinnerIds[20] != 2 ||
        perAxisWinnerIds[19] != 0) return 1;
    std::puts("store drop counter passed");
}
"""


def adapter(source, metal):
    counter = functions(source, "countPerAxisStoreDrop")
    store = functions(source, "writePerAxisDistanceTap")
    if metal:
        for token, replacement in (("int2", "ivec2"), ("atomic_int", "int"),
                                   ("atomic_uint", "uint")):
            counter = re.sub(r"\b" + token + r"\b", replacement, counter)
            store = re.sub(r"\b" + token + r"\b", replacement, store)
        counter = re.sub(r"\b(?:constant|device)\s+", "", counter)
        store = re.sub(r"\b(?:constant|device)\s+", "", store)
    return HOST + counter + "\n" + store + (CHECKS_METAL if metal else CHECKS_GLSL)


def run_host(source):
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "store_drop.cpp"
        program = Path(folder) / "store_drop"
        path.write_text(source)
        built = subprocess.run(
            [COMPILER, "-std=c++17", str(path), "-o", str(program)],
            capture_output=True,
            text=True,
        )
        if built.returncode:
            raise AssertionError(built.stderr)
        return subprocess.run([str(program)], capture_output=True, text=True)


@unittest.skipUnless(COMPILER, "C++ host compiler required")
class StoreDropCounterTest(unittest.TestCase):
    def test_production_counter_and_both_drop_sites(self):
        for shader in SHADERS:
            with self.subTest(shader=shader.name):
                source = shader.read_text()
                self.assertEqual(source.count("countPerAxisStoreDrop"), 3)
                result = run_host(adapter(source, shader.suffix == ".metal"))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("counter passed", result.stdout)

                wrong_slot = adapter(source, shader.suffix == ".metal").replace("+ 8u", "+ 7u")
                self.assertNotEqual(run_host(wrong_slot).returncode, 0)


if __name__ == "__main__":
    unittest.main()

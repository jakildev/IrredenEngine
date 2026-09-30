"""Execute the production GLSL and Metal overflow append functions on host data.

This checks cardinal-loser routing and bounded append accounting; native render
tests still cover GPU atomics, barriers, and finite-quad rasterization.
"""

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
struct ivec2 { int x, y; ivec2 operator+(ivec2 b) const { return {x+b.x,y+b.y}; } };
struct ivec3 { int x, y, z; };
struct ivec4 { int x, y, z, w; };
ivec2 pos3DtoPos2DIso(ivec3 p) { return {-p.x+p.y,-p.x-p.y+2*p.z}; }
bool isInsideCanvas(ivec2 p, ivec2 size) {
    return p.x>=0 && p.y>=0 && p.x<size.x && p.y<size.y;
}
constexpr ivec2 canvasSize{16,16};
int distances[256]{};
uint perAxisWinnerIds[512]{};
struct Image {} triangleCanvasDistances;
struct Texel { int x; };
ivec2 imageSize(Image) { return canvasSize; }
Texel imageLoad(Image,ivec2 p) { return {distances[p.y*16+p.x]}; }
uint atomicAdd(uint& slot,uint value) { uint old=slot; slot+=value; return old; }
ivec4 overflowScratchLayout{256,300,320,2};
struct FrameDataVoxelToTrixel { ivec4 overflowScratchLayout; };
constexpr int memory_order_relaxed=0;
int atomic_load_explicit(const int* slot,int) { return *slot; }
uint atomic_fetch_add_explicit(uint* slot,uint value,int) {
    return atomicAdd(*slot,value);
}
uint atomic_fetch_sub_explicit(uint* slot,uint value,int) {
    uint old=*slot; *slot-=value; return old;
}
void atomic_store_explicit(uint* slot,uint value,int) { *slot=value; }
"""

CHECKS = r"""
int main() {
    distances[8*16+8]=100;
    const ivec2 base{8,8};
    const ivec3 face{0,0,0};
    FrameDataVoxelToTrixel frame{overflowScratchLayout};
    auto append=[&](ivec3 p,int depth,uint color) {
#ifdef METAL_ADAPTER
        overflowAppendTap(base,p,depth,color,frame,distances,perAxisWinnerIds,canvasSize);
#else
        overflowAppendTap(base,p,depth,color);
#endif
    };
    append(face,100,0x11111111u); // settled cardinal winner
    if(perAxisWinnerIds[301]!=0) return 1;
    append(face,101,0x22222222u); // visible cardinal loser
    append(face,102,0x33333333u); // another loser, at capacity
    if(perAxisWinnerIds[301]!=2 || perAxisWinnerIds[305]!=0) return 2;
    if(perAxisWinnerIds[320]!=0x00080008u ||
       perAxisWinnerIds[321]!=0x22222222u || perAxisWinnerIds[322]!=101u ||
       perAxisWinnerIds[323]!=0x00080008u ||
       perAxisWinnerIds[324]!=0x33333333u || perAxisWinnerIds[325]!=102u) return 3;
    append(face,103,0x44444444u); // cap reports, count stays capped
    if(perAxisWinnerIds[301]!=2 || perAxisWinnerIds[305]!=1 ||
       perAxisWinnerIds[326]!=0) return 4;
    append({1000,0,0},104,0x55555555u); // off-canvas cardinal key
    if(perAxisWinnerIds[301]!=2 || perAxisWinnerIds[305]!=1) return 5;
    std::puts("overflow append controls passed");
    return 0;
}
"""


def adapter(source, metal):
    function = functions(source, "overflowAppendTap")
    if metal:
        for token, replacement in (("int2", "ivec2"), ("int3", "ivec3"),
                                   ("atomic_int", "int"), ("atomic_uint", "uint")):
            function = re.sub(r"\b" + token + r"\b", replacement, function)
        function = re.sub(r"\b(?:constant|device)\s+", "", function)
    return HOST + ("#define METAL_ADAPTER\n" if metal else "") + function + CHECKS


def run_host(source):
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "overflow.cpp"
        program = Path(folder) / "overflow"
        path.write_text(source)
        built = subprocess.run([COMPILER, "-std=c++17", str(path), "-o", str(program)],
                               capture_output=True, text=True)
        if built.returncode:
            raise AssertionError(built.stderr)
        return subprocess.run([str(program)], capture_output=True, text=True)


@unittest.skipUnless(COMPILER, "C++ host compiler required")
class OverflowAppendTest(unittest.TestCase):
    def test_production_append_both_backends(self):
        for shader in SHADERS:
            with self.subTest(shader=shader.name):
                host = adapter(shader.read_text(), shader.suffix == ".metal")
                result = run_host(host)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("controls passed", result.stdout)

                # Reversing the winner predicate admits the stored face.
                winner_mutant, count = re.subn(
                    r"==\s*voxelDistance(?=\s*\))", "!= voxelDistance", host
                )
                self.assertEqual(count, 1)
                self.assertNotEqual(run_host(winner_mutant).returncode, 0)

                # If the cap is ignored, instanceCount exceeds the allocation.
                cap_mutant, count = re.subn(r"if \(idx >= uint\([^)]*\)\)",
                                            "if (false)", host)
                self.assertEqual(count, 1)
                self.assertNotEqual(run_host(cap_mutant).returncode, 0)


if __name__ == "__main__":
    unittest.main()

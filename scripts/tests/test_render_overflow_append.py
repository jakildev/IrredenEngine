"""Execute the production GLSL and Metal overflow append functions on host data.

This checks cardinal-loser routing, the routing of the occlusion verdict and
bounded append accounting. The footprint projection and the occluder pyramid
are stubbed: the verdict is an input here. Native render tests still cover GPU
atomics, barriers, finite-quad rasterization and the verdict itself.
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
struct vec2 { float x, y; };
struct ivec2 {
    int x, y;
    constexpr ivec2(int x_, int y_) : x(x_), y(y_) {}
    explicit ivec2(vec2 v) : x(int(v.x)), y(int(v.y)) {}
    ivec2 operator+(ivec2 b) const { return {x+b.x,y+b.y}; }
};
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
ivec4 overflowScratchLayout{0,300,320,2};
struct FrameDataVoxelToTrixel { ivec4 overflowScratchLayout; };
ivec2 canvasSizePixels = canvasSize;
struct PerAxisScatterView { vec2 framebufferSize; };
struct PerAxisFaceFootprint {};
struct PerAxisScatterFrame {};
struct PerAxisScatterGlobals {};
namespace access { struct read; }
template <class T, class A> struct texture2d {};
constexpr int kFaceMayOwnPixel=0, kFaceUnsampled=1, kFaceHidden=2;
int stubVerdict=kFaceMayOwnPixel;
int occlusionTests=0;
PerAxisScatterView perAxisScatterView(ivec2) { return {}; }
PerAxisScatterView perAxisScatterView(PerAxisScatterFrame&, PerAxisScatterGlobals&, ivec2) {
    return {};
}
PerAxisFaceFootprint perAxisFaceFootprint(ivec2, int, PerAxisScatterView) { return {}; }
int perAxisFaceOcclusion(PerAxisFaceFootprint, ivec2) { ++occlusionTests; return stubVerdict; }
int perAxisFaceOcclusion(PerAxisFaceFootprint, ivec2, texture2d<int, access::read>) {
    ++occlusionTests;
    return stubVerdict;
}
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
    PerAxisScatterFrame scatterFrame;
    PerAxisScatterGlobals scatterGlobals;
    auto append=[&](ivec3 p,int depth,uint color,bool cut=false) {
#ifdef METAL_ADAPTER
        FrameDataVoxelToTrixel frame{overflowScratchLayout};
        overflowAppendTap(base,p,depth,color,cut,17u,frame,scatterFrame,scatterGlobals,
                          texture2d<int, access::read>{},distances,perAxisWinnerIds,canvasSize);
#else
        overflowAppendTap(base,p,depth,color,cut,17u);
#endif
    };
    append(face,100,0x11111111u); // settled cardinal winner
    if(perAxisWinnerIds[301]!=0) return 1;
    append(face,101,0x22222222u); // visible cardinal loser
    append(face,102,0x33333333u,true); // another loser, at capacity
    if(perAxisWinnerIds[301]!=2 || perAxisWinnerIds[305]!=0) return 2;
    if(perAxisWinnerIds[320]!=0x00080008u ||
       perAxisWinnerIds[321]!=0x22222222u || perAxisWinnerIds[322]!=101u ||
       perAxisWinnerIds[324]!=(0x00080008u|kOverflowCutFaceBit) ||
       perAxisWinnerIds[325]!=0x33333333u || perAxisWinnerIds[326]!=102u ||
       perAxisWinnerIds[323]!=17u ||
       perAxisWinnerIds[327]!=17u) return 3;
    if(overflowEntryCell(perAxisWinnerIds[324])!=0x00080008u ||
       !overflowEntryIsCutFace(perAxisWinnerIds[324])) return 6;
    append(face,103,0x44444444u); // cap reports, count stays capped
    if(perAxisWinnerIds[301]!=2 || perAxisWinnerIds[305]!=1 ||
       perAxisWinnerIds[328]!=0) return 4;
    append({1000,0,0},104,0x55555555u); // off-canvas cardinal key
    if(perAxisWinnerIds[301]!=2 || perAxisWinnerIds[305]!=1) return 5;
    perAxisWinnerIds[301]=0;
    append(face,-32768,0x87654321u,false);
    append(face,-32768,0x87654321u,true);
    if((perAxisWinnerIds[320]^perAxisWinnerIds[324])!=kOverflowCutFaceBit ||
       perAxisWinnerIds[321]!=perAxisWinnerIds[325] ||
       perAxisWinnerIds[322]!=perAxisWinnerIds[326] ||
       int(perAxisWinnerIds[322])!=-32768) return 7;
    // Without an occluder pyramid nothing is tested.
    if(occlusionTests!=0) return 8;

    // With one, the verdict routes each cardinal loser: kept, or counted as
    // rejected in its own word and never as appended or dropped.
    for(uint& word : perAxisWinnerIds) word=0;
    overflowScratchLayout.x=1;
    append(face,100,0x11111111u); // the settled winner is the cell path's: untested
    if(occlusionTests!=0 || perAxisWinnerIds[301]!=0) return 9;
    stubVerdict=kFaceHidden;
    append(face,101,0x22222222u);
    stubVerdict=kFaceUnsampled;
    append(face,102,0x33333333u);
    append(face,103,0x44444444u);
    if(occlusionTests!=3 || perAxisWinnerIds[301]!=0 || perAxisWinnerIds[305]!=0 ||
       perAxisWinnerIds[306]!=2 || perAxisWinnerIds[307]!=1 ||
       perAxisWinnerIds[320]!=0) return 10;
    stubVerdict=kFaceMayOwnPixel;
    append(face,104,0x55555555u);
    if(perAxisWinnerIds[301]!=1 || perAxisWinnerIds[321]!=0x55555555u ||
       perAxisWinnerIds[306]!=2 || perAxisWinnerIds[307]!=1) return 11;
    // A rejected face at a full lane is still a rejection, not a drop.
    append(face,105,0x66666666u);
    stubVerdict=kFaceHidden;
    append(face,106,0x77777777u);
    if(perAxisWinnerIds[301]!=2 || perAxisWinnerIds[305]!=0 ||
       perAxisWinnerIds[307]!=2) return 12;
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
    folder = ROOT / "engine/render/src/shaders" / ("metal" if metal else "")
    common = (folder / ("ir_iso_common.metal" if metal else "ir_iso_common.glsl")).read_text()
    flag = re.search(r"(?:const|constant) uint kOverflowCutFaceBit = [^;]+;", common).group()
    flag = flag.replace("constant", "const")
    key = "\n".join(functions(common, name) for name in (
        "packOverflowEntryCell", "overflowEntryCell", "overflowEntryIsCutFace"))
    key = re.sub(r"\bint2\b", "ivec2", key)
    return (HOST + flag + key + "\n" + ("#define METAL_ADAPTER\n" if metal else "")
            + function + CHECKS)


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

                cut_mutant = host.replace(
                    "isCutFace ? kOverflowCutFaceBit", "false ? kOverflowCutFaceBit")
                self.assertNotEqual(cut_mutant, host)
                self.assertNotEqual(run_host(cut_mutant).returncode, 0)

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

                # A verdict that is ignored appends the hidden face.
                verdict_mutant, count = re.subn(
                    r"if \(occlusion != kFaceMayOwnPixel\)", "if (false)", host)
                self.assertEqual(count, 1)
                self.assertNotEqual(run_host(verdict_mutant).returncode, 0)

                # A test that runs with no pyramid bound reads a stale one.
                gate_mutant, count = re.subn(
                    r"if \((?:frameData\.)?overflowScratchLayout\.x != 0\)", "if (true)", host)
                self.assertEqual(count, 1)
                self.assertNotEqual(run_host(gate_mutant).returncode, 0)

                # Both rejection classes share one word: the split is lost.
                class_mutant, count = re.subn(
                    r"occlusion == kFaceUnsampled \? 6u : 7u", "7u", host)
                self.assertEqual(count, 1)
                self.assertNotEqual(run_host(class_mutant).returncode, 0)


if __name__ == "__main__":
    unittest.main()

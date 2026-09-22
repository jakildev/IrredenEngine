"""Execute both SDF box interval solvers against an independent world-ray oracle.

The interval is surface geometry; its quantized integer depth is not. These
controls protect that distinction without blessing current floor-shadow edges.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_function

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")

PREAMBLE = r"""
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
using std::min;
using std::max;
using std::abs;
struct ivec2 { int x,y; };
struct vec3 { float x,y,z; };
"""

CASES = r"""
struct Hit { bool valid; double entry,exit; int face; };
Hit intersect(const double *origin,const double *direction,const double *half) {
    Hit h{true,-1.e30,1.e30,-1};
    for(int axis=0;axis<3;++axis) {
        if(std::abs(direction[axis])<1.e-12) {
            if(std::abs(origin[axis])>half[axis]) return {false,0,0,-1};
            continue;
        }
        double a=(-half[axis]-origin[axis])/direction[axis];
        double b=( half[axis]-origin[axis])/direction[axis];
        int face=2*axis+(direction[axis]<0 ? 1 : 0);
        if(a>b) std::swap(a,b);
        if(a>h.entry) {h.entry=a;h.face=face;}
        h.exit=std::min(h.exit,b);
    }
    h.valid=h.entry<=h.exit;
    return h;
}
int main() {
    int hits=0,misses=0,normals=0;
    double maxError=0;
    for(int density:{1,2,4,8}) for(int step=0;step<32;++step)
    for(double perturb:{-0.00001,0.0,0.00001}) {
        const double yaw=step*std::acos(-1.0)/16+perturb;
        const double c=std::cos(yaw),s=std::sin(yaw);
        for(int x=-12;x<=12;++x) for(int y=-12;y<=12;++y) {
            // Screen-right and screen-down bases, then inverse camera rotation.
            const double view[]={-x/2.0-y/6.0,x/2.0-y/6.0,y/3.0};
            const double origin[]={c*view[0]-s*view[1],s*view[0]+c*view[1],view[2]};
            const double direction[]={(c-s)/3.0,(s+c)/3.0,1.0/3.0};
            const vec3 ext{1.37f*density,2.19f*density,0.83f*density};
            const double half[]={ext.x,ext.y,ext.z};
            const Hit expected=intersect(origin,direction,half);
            float entry=0,exit=0;
            const bool actual=boxSlabIntersectYaw({x,y},ext,float(c),float(s),entry,exit);
            if(actual!=expected.valid) return 1;
            if(!actual) {++misses;continue;}
            ++hits;normals|=1<<expected.face;
            const double error=std::max(
                std::abs(entry-expected.entry),std::abs(exit-expected.exit));
            maxError=std::max(maxError,error);
            if(error>2.e-4) return 2;
            // Exact entry lies on a face and inside every other slab.
            bool boundary=false;
            for(int axis=0;axis<3;++axis) {
                const double position=origin[axis]+direction[axis]*entry;
                if(std::abs(position)>half[axis]+2.e-4) return 3;
                boundary|=std::abs(std::abs(position)-half[axis])<2.e-4;
            }
            if(!boundary) return 4;
        }
    }
    // Yaw can expose both X/Y polarities, but never the underside (+Z).
    if(hits==0 || misses==0 || normals!=31) return 5;
    for(int density:{1,2,4,8}) {
        float a,ae,b,be;
        if(!boxSlabIntersectYaw({0,0},{density+.1f,5.f*density,6.f*density},1,0,a,ae)
        || !boxSlabIntersectYaw({0,0},{5.f*density,density+.2f,6.f*density},1,0,b,be)) return 6;
        // The same stored source depth describes distinct X/Y entry planes.
        if(stableCeilToInt(a)!=stableCeilToInt(b) || std::abs(a-b)<.2f) return 7;
        for(int slot=0;slot<3;++slot) {
            if(encodeDepthWithFace(stableCeilToInt(a),slot,0)
            != encodeDepthWithFace(stableCeilToInt(b),slot,0)) return 9;
        }
        if(std::abs(a/3.0/density-(-1.0-.1/density))>1.e-6
        || std::abs(b/3.0/density-(-1.0-.2/density))>1.e-6) return 8;
    }
    std::printf("rays=%d hits=%d misses=%d normal_mask=%d max_depth_error=%.9g alias_pairs=4\n",
        hits+misses,hits,misses,normals,maxError);
    return 0;
}
"""


@unittest.skipUnless(COMPILER, "SDF surface controls require a C++ compiler")
class SdfSurfaceContractTest(unittest.TestCase):
    def test_shader_intervals_and_lossy_depth(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            path = ROOT / "engine/render/src/shaders" / directory / f"c_shapes_to_trixel.{suffix}"
            source = path.read_text()
            epsilon = re.search(
                r"(?:const|constant) float kCeilBiasEpsilon = [^;]+;", source)
            self.assertIsNotNone(epsilon)
            functions = epsilon.group().replace("constant ", "const ") + "\n"
            functions += "\n".join(extract_function(source, name) for name in (
                "stableCeilToInt", "slabFromLinear", "boxSlabIntersectYaw"))
            common = (path.parent / f"ir_iso_common.{suffix}").read_text()
            shift = re.search(r"(?:const|constant) int kDepthEncodeShift = \d+;", common)
            self.assertIsNotNone(shift)
            functions += "\n" + shift.group().replace("constant ", "const ")
            functions += "\n" + extract_function(common, "encodeDepthWithFace")
            functions = re.sub(r"out float (\w+)", r"float& \1", functions)
            functions = functions.replace("thread ", "").replace("float3", "vec3")
            functions = functions.replace("int2", "ivec2")
            variants = {
                "production": functions,
                "wrong_yaw_polarity": functions.replace("yawS", "(-yawS)").replace(
                    "float (-yawS)", "float yawS"),
                "rounded_surface": functions.replace(
                    "dEntry = max(dxLo, max(dyLo, dzLo));",
                    "dEntry = ceil(max(dxLo, max(dyLo, dzLo)));"),
                "wrong_parallel_slab": functions.replace(
                    "return false;", "dLo=-1e18; dHi=1e18; return true;", 1),
            }
            for name, body in variants.items():
                with (
                    self.subTest(backend=suffix, variant=name),
                    tempfile.TemporaryDirectory() as tmp,
                ):
                    if name != "production":
                        self.assertNotEqual(body, functions)
                    cpp, binary = Path(tmp) / "surface.cpp", Path(tmp) / "surface"
                    cpp.write_text(PREAMBLE + body + CASES)
                    build = subprocess.run([COMPILER, "-std=c++17", str(cpp), "-o", str(binary)],
                                           capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(binary)], capture_output=True, text=True)
                    if name == "production":
                        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                        print(suffix, run.stdout.strip())
                    else:
                        self.assertIn(run.returncode, (1, 2, 3, 4), name)


if __name__ == "__main__":
    unittest.main()

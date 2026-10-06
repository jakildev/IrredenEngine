"""Execute both backends' SDF box face-slot helper against a world-ray oracle.

`shapeBoxFaceSlot` names the depth slot all three diamonds of an analytic box
sample store. AO, lighting and fog map that slot through the store canvas's
visible-face triplet, so it must name the box face the sample's view ray
enters through. The oracle is an independent double-precision ray/box solve
and the cardinal triplet table; the helper is fed the body's real input, the
integer iso pixel relative to the shape origin.

The point-based mutations are the forms a reader might reach for instead: the
argmax of the reconstructed surface point's |offset| - halfSize, and its
|offset| / halfSize ratio. The body stores ceil(dEntry), a point just inside
the box, so both read an edge-on face beside the visible one, and the ratio
form also divides by the zero half extent of a one-voxel axis.
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
#include <cstdlib>
#include <initializer_list>
using std::min;
using std::max;
using std::abs;
struct ivec2 { int x,y; };
struct vec3 { float x,y,z; vec3()=default;
    explicit vec3(float a): x(a),y(a),z(a) {}
    vec3(float a,float b,float c): x(a),y(b),z(c) {} };
vec3 operator+(vec3 a,vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
"""

# The body's surface point for a solid, unrotated box: the integer depth
# boxDepthIntersectYaw stores, through isoPositionToPos3D and R_z(+yaw). Only
# the point-based mutations read it.
BODY_POINT = r"""
vec3 bodySurfacePoint(ivec2 iso,vec3 h,float c,float s) {
    float dEntry=0,dExit=0;vec3 n;
    boxSurfaceIntervalYaw(float(iso.x),float(iso.y),h+vec3(0.5f),c,s,dEntry,dExit,n);
    auto at=[&](int d) {
        const float x=(2.f*d-3.f*iso.x-iso.y)/6.f;
        const float vx=x,vy=x+iso.x,vz=(iso.y+2.f*x+iso.x)/2.f;
        return vec3(c*vx-s*vy,s*vx+c*vy,vz);
    };
    const int d=int(std::ceil(dEntry-1.e-3f));
    vec3 p=at(d);
    const float sdf=max(abs(p.x)-h.x,max(abs(p.y)-h.y,abs(p.z)-h.z));
    if(sdf>0.5f+1.e-3f && float(d+1)<=dExit) p=at(d+1);
    return p;
}
"""

POINT_AXIS = r"""vec3 p = bodySurfacePoint(isoRel, halfSize, yawC, yawS);
    vec3 e = {FORM};
    int axis = (e.x >= e.y && e.x >= e.z) ? 0 : (e.y >= e.z ? 1 : 2);
    if (axis == 2) return 2;"""

CASES = r"""
// FaceId order: X_NEG, X_POS, Y_NEG, Y_POS, Z_NEG (visibleFaceTripletCardinal).
const int kTriplet[4][3]={{0,2,4},{2,1,4},{1,3,4},{3,0,4}};
struct Hit { bool valid; double entry,runnerUp,exit; int face; };
Hit intersect(const double *origin,const double *direction,const double *half) {
    Hit h{true,-1.e30,-1.e30,1.e30,-1};
    for(int axis=0;axis<3;++axis) {
        double a=(-half[axis]-origin[axis])/direction[axis];
        double b=( half[axis]-origin[axis])/direction[axis];
        if(a>b) std::swap(a,b);
        if(a>h.entry) {h.runnerUp=h.entry;h.entry=a;h.face=2*axis+(direction[axis]<0);}
        else h.runnerUp=std::max(h.runnerUp,a);
        h.exit=std::min(h.exit,b);
    }
    h.valid=h.entry<=h.exit;
    return h;
}
int main(int argc,char **argv) {
    // Voxel counts; a count of 1 is a zero-width scaled axis.
    const float boxes[][3]={{6,9,4},{9,3,7},{1,8,6},{7,1,5},{8,6,1}};
    const int only=argc>1 ? std::atoi(argv[1]) : -1;
    const double pi=std::acos(-1.0);
    long checked=0,skipped=0,perSlot[3]={0,0,0},perCardinal[4]={0,0,0,0},degenerate=0;
    for(int box=0;box<5;++box) {
        if(only>=0 && box!=only) continue;
        for(int sub:{1,4,16}) {
            const vec3 h((boxes[box][0]-1)*sub*.5f,(boxes[box][1]-1)*sub*.5f,
                         (boxes[box][2]-1)*sub*.5f);
            const double half[]={h.x+.5,h.y+.5,h.z+.5};
            const int reach=int(2*(half[0]+half[1]+half[2]))+2;
            const int stride=sub==16 ? 3 : 1;
            for(int cardinal=0;cardinal<4;++cardinal)
            for(int residual=-44;residual<=44;residual+=4) {
                const double yaw=cardinal*pi/2+residual*pi/180;
                const float c=float(std::cos(yaw)),s=float(std::sin(yaw));
                const double direction[]={(double(c)-s)/3.0,(double(s)+c)/3.0,1.0/3.0};
                for(int x=-reach;x<=reach;x+=stride) for(int y=-reach;y<=reach;y+=stride) {
                    const double view[]={-x/2.0-y/6.0,x/2.0-y/6.0,y/3.0};
                    const double origin[]={c*view[0]-s*view[1],s*view[0]+c*view[1],view[2]};
                    const Hit hit=intersect(origin,direction,half);
                    if(!hit.valid) continue;
                    // A ray crossing two face planes almost together sits on an
                    // edge, where either face is right; one that barely enters
                    // grazes the silhouette, where float32 may miss.
                    if(hit.entry-hit.runnerUp<1.e-2 || hit.exit-hit.entry<1.e-2) {
                        ++skipped;continue;
                    }
                    const int slot=shapeBoxFaceSlot({x,y},h,c,s,cardinal);
                    if(slot<0 || slot>2) return 3;
                    if(kTriplet[cardinal][slot]!=hit.face) {
                        std::printf("box=%d sub=%d yaw=%d+%d iso=(%d,%d) face=%d slot=%d\n",
                            box,sub,cardinal*90,residual,x,y,hit.face,slot);
                        return 2;
                    }
                    ++checked;++perSlot[slot];++perCardinal[cardinal];
                    degenerate+=boxes[box][0]==1 || boxes[box][1]==1 || boxes[box][2]==1;
                }
            }
        }
    }
    for(long n:perSlot) if(n==0) return 4;
    for(long n:perCardinal) if(n==0) return 4;
    if(only<0 && degenerate==0) return 4;
    std::printf("checked=%ld edge_skipped=%ld degenerate_axis=%ld slots=%ld/%ld/%ld\n",
        checked,skipped,degenerate,perSlot[0],perSlot[1],perSlot[2]);
    return 0;
}
"""

DEGENERATE_X_BOX = 2


def lift(suffix, directory):
    shaders = ROOT / "engine/render/src/shaders" / directory
    sdf = (shaders / f"ir_sdf_common.{suffix}").read_text()
    body = (shaders / f"c_shapes_to_trixel_body.{suffix}").read_text()
    source = "\n".join(extract_function(sdf, name) for name in (
        "slabFromLinear", "boxSurfaceIntervalYaw"))
    source += "\n" + BODY_POINT + "\n" + extract_function(body, "shapeBoxFaceSlot")
    source = re.sub(r"out (float|vec3) (\w+)", r"\1& \2", source)
    source = source.replace("thread ", "").replace("inline ", "")
    return source.replace("float3", "vec3").replace("int2", "ivec2")


def point_axis(source, form):
    selection = re.compile(
        r"if \(entryNormal\.z != 0\.0\) return 2;\s*"
        r"(?:const )?int axis = \(entryNormal\.x != 0\.0\) \? 0 : 1;")
    mutated, count = selection.subn(POINT_AXIS.replace("{FORM}", form), source)
    return mutated if count == 1 else source


@unittest.skipUnless(COMPILER, "SDF box face-slot controls require a C++ compiler")
class SdfBoxFaceSlotTest(unittest.TestCase):
    def run_cases(self, source, tmp, *args):
        cpp, binary = Path(tmp) / "slot.cpp", Path(tmp) / "slot"
        cpp.write_text(PREAMBLE + source + CASES)
        build = subprocess.run([COMPILER, "-std=c++17", "-O1", str(cpp), "-o", str(binary)],
                               capture_output=True, text=True)
        self.assertEqual(build.returncode, 0, build.stderr)
        return subprocess.run([str(binary), *map(str, args)], capture_output=True, text=True)

    def test_slot_names_the_entered_face_on_both_backends(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            source = lift(suffix, directory)
            mutations = {
                "no_odd_cardinal_swap": (
                    source.replace("((cardinalIndex & 1) != 0) ? 1 - axis : axis", "axis"),
                    ()),
                "surface_point_excess": (
                    point_axis(source, "vec3(abs(p.x) - halfSize.x, abs(p.y) - halfSize.y, "
                                       "abs(p.z) - halfSize.z)"),
                    ()),
                "surface_point_ratio": (
                    point_axis(source, "vec3(abs(p.x) / halfSize.x, abs(p.y) / halfSize.y, "
                                       "abs(p.z) / halfSize.z)"),
                    (DEGENERATE_X_BOX,)),
            }
            with self.subTest(backend=suffix, variant="production"), \
                    tempfile.TemporaryDirectory() as tmp:
                run = self.run_cases(source, tmp)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                checked = re.search(r"checked=(\d+)", run.stdout)
                self.assertIsNotNone(checked, run.stdout)
                self.assertGreater(int(checked.group(1)), 0)
                print(suffix, run.stdout.strip())
            for name, (mutated, args) in mutations.items():
                with self.subTest(backend=suffix, variant=name), \
                        tempfile.TemporaryDirectory() as tmp:
                    self.assertNotEqual(mutated, source, name)
                    run = self.run_cases(mutated, tmp, *args)
                    self.assertEqual(run.returncode, 2, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()

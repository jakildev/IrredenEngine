"""Execute the finite fragment's cascade depth against the canvas cell recovery."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_function

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "engine/render/src/shaders"
COMPILER = shutil.which("c++")

PREAMBLE = r"""
#include <algorithm>
#include <cmath>
#include <cstdio>
using std::cos; using std::sin; using std::round; using std::max;
struct vec2 {float x,y; vec2(float a,float b):x(a),y(b){}};
struct ivec2 {int x,y;};
struct vec3 {float x,y,z; vec3(float a,float b,float c):x(a),y(b),z(c){}};
struct ShapeProjectionData {
 ivec2 voxelRenderOptions; int smoothYawEnabled=0,latticeShapes=0;
 float visualYaw=0,rasterYaw=0;
};
"""

# Canvas cell receivers recover their position from (iso pixel, rawDepth) and
# select the cascade with rawDepth itself (c_compute_sun_shadow_body).
CHECKS = r"""
int main(){
 const float kHalfPi=float(std::acos(-1.0)/2.0);
 int checks=0,failures=0,perCardinal[4]={0,0,0,0};
 for(int cardinal=0;cardinal<4;++cardinal) for(float residual:{0.f,.27f,-.58f})
 for(int smooth:{0,1}) for(int mode:{0,1}) for(int density:{1,2,4,8})
 for(int isoX:{-37,0,5,122}) for(int isoY:{-91,-2,0,64}) for(int depth:{-700,-33,0,18,512}) {
  if(!smooth && residual!=0.f) continue;
  ShapeProjectionData f;
  f.voxelRenderOptions={mode,density}; f.smoothYawEnabled=smooth;
  f.rasterYaw=cardinal*kHalfPi; f.visualYaw=f.rasterYaw+residual;
  const int scale=mode!=0?density:1;
  vec3 view=isoPixelToPos3D(isoX,isoY,float(depth));
  view=vec3(view.x/scale,view.y/scale,view.z/scale);
  const vec3 world=residual!=0.f
   ? rotateYawZInv(view,f.visualYaw)
   : rotateCardinalZInv(view,rasterYawCardinalIndex(f.rasterYaw));
  const float actual=shapeCanvasIsoDepth(world,f);
  if(std::abs(actual-float(depth))>1e-3f*(1.f+std::abs(float(depth)))){
   ++perCardinal[cardinal];
   if(++failures<=4)printf("fail cardinal=%d smooth=%d mode=%d density=%d depth=%d actual=%g\n",
                           cardinal,smooth,mode,density,depth,actual);
  }
  ++checks;
 }
 printf("cardinal_failures=%d,%d,%d,%d\n",perCardinal[0],perCardinal[1],perCardinal[2],
        perCardinal[3]);
 printf("checks=%d failures=%d\n",checks,failures);
 return failures?1:0;
}
"""


@unittest.skipUnless(COMPILER, "cascade depth checks require a C++ compiler")
class ShapeCascadeDepthTest(unittest.TestCase):
    def test_fragment_depth_matches_canvas_raw_depth(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            shaders = SHADERS / folder
            common = (shaders / f"ir_iso_common.{suffix}").read_text()
            receiver = (shaders / f"ir_shape_receiver.{suffix}").read_text()
            helpers = "\n".join(
                [extract_function(common, name)
                 for name in ("rasterYawCardinalIndex", "cardinalYawCosSin",
                              "isoPositionToPos3D", "isoPixelToPos3D",
                              "rotateCardinalZInv", "rotateYawZInv", "yawedIsoDistance")]
                + [extract_function(receiver, name)
                   for name in ("shapeYawCosSin", "shapeCanvasIsoDepth")])
            variants = {
                "production": helpers,
                "world_frame_depth": helpers.replace(
                    "yawedIsoDistance(position, yaw)", "yawedIsoDistance(position, 0.0)"),
                "world_units_depth": helpers.replace(
                    "yaw) * float(scale)", "yaw)"),
            }
            for name, functions in variants.items():
                with (self.subTest(backend=suffix, variant=name),
                      tempfile.TemporaryDirectory() as tmp):
                    if name != "production":
                        self.assertNotEqual(functions, helpers)
                    code = PREAMBLE + functions + CHECKS
                    for source, target in (("float2", "vec2"), ("float3", "vec3"),
                                           ("inline ", ""), ("constexpr float", "const float")):
                        code = code.replace(source, target)
                    cpp, exe = Path(tmp) / "depth.cpp", Path(tmp) / "depth"
                    cpp.write_text(code)
                    build = subprocess.run([COMPILER, "-std=c++17", str(cpp), "-o", str(exe)],
                                           capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(exe)], capture_output=True, text=True)
                    if name == "production":
                        self.assertEqual(run.returncode, 0, run.stdout)
                        print(suffix, run.stdout.strip().splitlines()[-1])
                    else:
                        self.assertEqual(run.returncode, 1, run.stdout)
                    if name == "world_frame_depth":
                        counts = run.stdout.split("cardinal_failures=")[1].split()[0]
                        self.assertGreater(int(counts.split(",")[2]), 0, run.stdout)


if __name__ == "__main__":
    unittest.main()

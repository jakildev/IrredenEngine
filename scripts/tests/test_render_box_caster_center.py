"""Execute the box sun-caster center against the rasterizer's shape origin."""

import re
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
struct ivec3 {int x,y,z;};
struct vec3 {
 float x,y,z; vec3(float a,float b,float c):x(a),y(b),z(c){}
 explicit vec3(ivec3 v):x(float(v.x)),y(float(v.y)),z(float(v.z)){}
};
struct vec4 {vec3 xyz;};
struct ShapeDescriptor {vec4 worldPosition;};
struct ShapeProjectionData {
 ivec2 voxelRenderOptions; int smoothYawEnabled=0,latticeShapes=0;
 float visualYaw=0,rasterYaw=0;
};
ivec3 roundHalfUp(vec3 v){
 return {int(std::floor(v.x+.5f)),int(std::floor(v.y+.5f)),int(std::floor(v.z+.5f))};
}
"""

CHECKS = r"""
vec3 bake(ShapeDescriptor shape, ShapeProjectionData boxProjection){
 return BAKE_CENTER;
}
int main(){
 const float kHalfPi=float(std::acos(-1.0)/2.0);
 int checks=0,failures=0,perCardinal[4]={0,0,0,0};
 for(int cardinal=0;cardinal<4;++cardinal) for(float residual:{0.f,.31f,-.62f})
 for(int smooth:{0,1}) for(int lattice:{0,1}) for(int mode:{0,1}) for(int density:{1,2,4})
 for(float x:{-3.5f,-1.25f,0.f,.5f,2.75f}) for(float y:{-2.5f,.4f,1.5f})
 for(float z:{-.5f,.5f,3.5f}) {
  if(!smooth && residual!=0.f) continue;
  ShapeProjectionData f;
  f.voxelRenderOptions={mode,density}; f.smoothYawEnabled=smooth; f.latticeShapes=lattice;
  f.rasterYaw=cardinal*kHalfPi; f.visualYaw=f.rasterYaw+residual;
  ShapeDescriptor shape{{vec3(x,y,z)}};
  const vec3 actual=bake(shape,f);
  const float rasterYaw=f.rasterYaw,visualYaw=f.visualYaw;
  const int smoothYawEnabled=f.smoothYawEnabled,latticeShapes=f.latticeShapes;
  const ivec2 voxelRenderOptions=f.voxelRenderOptions;
RASTER_PROLOGUE
  bool ok;
  if(PLACEMENT_PREDICATE){
   ok=actual.x==worldPos.x&&actual.y==worldPos.y&&actual.z==worldPos.z;
  } else {
   const vec3 view=yawZero?actual:vec3(yawC*actual.x+yawS*actual.y,
                                       -yawS*actual.x+yawC*actual.y,actual.z);
   const float tolerance=smoothYaw?1e-4f:0.f;
   ok=std::abs(view.x-origin.x)<=tolerance&&std::abs(view.y-origin.y)<=tolerance&&
      view.z==float(origin.z);
  }
  (void)sub;
  if(!ok){
   ++perCardinal[cardinal];
   if(++failures<=4)printf("fail cardinal=%d smooth=%d lattice=%d mode=%d density=%d "
                           "world=(%g,%g,%g) center=(%g,%g,%g)\n",cardinal,smooth,lattice,
                           mode,density,x,y,z,actual.x,actual.y,actual.z);
  }
  ++checks;
 }
 printf("cardinal_failures=%d,%d,%d,%d\n",perCardinal[0],perCardinal[1],perCardinal[2],
        perCardinal[3]);
 printf("checks=%d failures=%d\n",checks,failures);
 return failures?1:0;
}
"""


def raster_prologue(suffix):
    source = (SHADERS / ("" if suffix == "glsl" else "metal/")
              / f"c_shapes_to_trixel_body.{suffix}").read_text()
    start = source.index("int cardinalIndex = rasterYawCardinalIndex(")
    start = source.rindex("\n", 0, start) + 1
    end = source.index(";", source.index("bool latticeWalk = ", start)) + 1
    predicate = re.search(r"originIsoScaled = (\(.*?\))\n", source).group(1)
    return source[start:end].replace("frameData.", ""), predicate


@unittest.skipUnless(COMPILER, "box caster center checks require a C++ compiler")
class BoxCasterCenterTest(unittest.TestCase):
    def test_bake_center_matches_rasterized_origin(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            shaders = SHADERS / folder
            common = (shaders / f"ir_iso_common.{suffix}").read_text()
            receiver = (shaders / f"ir_shape_receiver.{suffix}").read_text()
            helpers = "\n".join(
                [extract_function(common, name)
                 for name in ("rasterYawCardinalIndex", "cardinalYawCosSin")]
                + [extract_function(receiver, name)
                   for name in ("shapeYawCosSin", "shapeRenderedCenter")])
            bake_source = (shaders / f"c_bake_box_sun_shadow.{suffix}").read_text()
            bake_center = re.search(r"boxCenter = ([^;]+);", bake_source).group(1)
            prologue, predicate = raster_prologue(suffix)
            variants = {
                "production": (helpers, bake_center),
                "raw_position": (helpers, "shape.worldPosition.xyz"),
                "receiver_predicate": (helpers.replace(
                    " && (smoothMode || projection.latticeShapes == 0)", ""), bake_center),
                "world_frame_snap": (re.sub(r"yaw = shapeYawCosSin\(projection\)",
                                            "yaw = cardinalYawCosSin(0)", helpers),
                                     bake_center),
            }
            for name, (functions, center) in variants.items():
                with (self.subTest(backend=suffix, variant=name),
                      tempfile.TemporaryDirectory() as tmp):
                    if name != "production":
                        self.assertNotEqual((functions, center), (helpers, bake_center))
                    body = CHECKS.replace("BAKE_CENTER", center)
                    body = body.replace("RASTER_PROLOGUE", prologue)
                    body = body.replace("PLACEMENT_PREDICATE", predicate)
                    code = PREAMBLE + functions + body
                    for source, target in (("float2", "vec2"), ("float3", "vec3"),
                                           ("int3", "ivec3"), ("inline ", ""),
                                           ("constexpr float", "const float")):
                        code = code.replace(source, target)
                    cpp, exe = Path(tmp) / "center.cpp", Path(tmp) / "center"
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
                    if name in ("raw_position", "world_frame_snap"):
                        counts = re.search(r"cardinal_failures=(\S+)", run.stdout).group(1)
                        self.assertGreater(int(counts.split(",")[2]), 0, run.stdout)
                    if name == "receiver_predicate":
                        self.assertIn("smooth=1 lattice=1 mode=0", run.stdout)


if __name__ == "__main__":
    unittest.main()

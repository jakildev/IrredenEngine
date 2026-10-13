"""Execute selected sphere shadow geometry without changing displayed face normals."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_block, extract_function
from test_render_shape_receiver import PREAMBLE

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")


def cpp(source):
    source = re.sub(r"\bout (vec3|float) (\w+)", r"\1& \2", source)
    source = source.replace("thread ", "").replace("constant ", "const ")
    source = source.replace("float3", "vec3").replace("float2", "vec2")
    source = source.replace("int3", "ivec3").replace("int2", "ivec2")
    source = source.replace("shape.params.x", "shape.params.xyz.x")
    source = source.replace("const ShapeProjectionData &receiverFrame,", "")
    for declaration in (
        "device const ShapeDescriptor *receiverShapes,",
        "device const uint *receiverOwners,",
        "device const ShapeTileDescriptor *receiverTiles,",
    ):
        source = source.replace(declaration, "")
    source = re.sub(
        r"receiverFrame,\s*receiverShapes,\s*receiverOwners,\s*receiverTiles,", "", source
    )
    return source.replace("sunFrameData.", "").replace(", sunFrameData, sunDepthBuf", "")


HARNESS = (
    PREAMBLE.replace(
        "int smoothYawEnabled=0", "int shapeCount=1,finiteCoverage=1; int smoothYawEnabled=0"
    )
    + r"""
float max(float a,double b){return std::max(a,float(b));}
float length(vec3 v){return sqrt(dot(v,v));}
vec3 normalize(vec3 v){return v/length(v);}
const uint SHAPE_SPHERE=1;
const uint kShapeSamplesPerTile=384;
struct ShapeTileDescriptor {int shapeIndex;ivec2 tileIsoOrigin;};
ShapeProjectionData receiverFrame;
ShapeDescriptor receiverShapes[3];
ShapeTileDescriptor receiverTiles[3];
uint receiverOwners[4];
ivec2 pixel(1,1),size(2,2);
vec3 pos3D(91),normal(93),lastPosition,lastNormal;
vec4 sunCasterViewToWorld;
int rawDepth=123,shadowsEnabled=1,surfaceCalls=0,legacyCalls=0;
float worldSurfaceSunShadowFactor(vec3 p,vec3 n,float depth,vec4){
 lastPosition=p;lastNormal=n;surfaceCalls++;return depth==123?0.25f:-1.f;
}
float worldSunShadowFactor(vec3 p,vec3 n,float depth){
 lastPosition=p;lastNormal=n;legacyCalls++;return depth==123?0.75f:-1.f;
}
bool close(vec3 a,vec3 b,float tolerance=0.0003f){return length(a-b)<=tolerance;}
"""
)

CHECKS = r"""
int main(){
 int hits=0,misses=0;
 for(int smooth:{0,1})for(int sub:{2,4,8})for(int step=0;step<16;++step)
 for(float phase:{-.37f,.19f,.73f}) {
  ShapeProjectionData f;f.smoothYawEnabled=smooth;f.voxelRenderOptions={1,sub};
  f.visualYaw=float(step*acos(-1.0)/8);f.rasterYaw=float((step/4)*acos(-1.0)/2);
  ShapeDescriptor shape;shape.shapeType=SHAPE_SPHERE;shape.params.xyz={4,0,0};
  shape.worldPosition.xyz={phase*7,phase*3,phase*5};
  // Independent double-precision world ray and sphere equation, including the
  // producer's rounded projected origin and cardinal-rounded placement.
  double angle=smooth?double(f.visualYaw):double(step/4)*acos(-1.0)/2;
  double c=smooth?cos(angle):(step/4==0?1:step/4==2?-1:0);
  double s=smooth?sin(angle):(step/4==1?1:step/4==3?-1:0);
  double cx=shape.worldPosition.xyz.x,cy=shape.worldPosition.xyz.y,cz=shape.worldPosition.xyz.z;
  if(!smooth){double vx=floor(c*cx+s*cy+.5),vy=floor(-s*cx+c*cy+.5);
   cx=c*vx-s*vy;cy=s*vx+c*vy;cz=floor(cz+.5);}
  int oi=int(floor((-(c*cx+s*cy)+(-s*cx+c*cy))*sub+.5));
  int oj=int(floor((-(c*cx+s*cy)-(-s*cx+c*cy)+2*cz)*sub+.5));
  const double radius=4.+.5/sub;
  for(int i=-12;i<=12;i+=3)for(int j=-12;j<=12;j+=3){
   ivec2 sample(oi+i*sub/2,oj+j*sub/2);
   double u=double(sample.x-oi)/sub,v=double(sample.y-oj)/sub;
   double x=-u/2-v/6,y=u/2-v/6,z=v/3;
   double lengthSquared=x*x+y*y+z*z, chord=radius*radius-lengthSquared;
   bool expected=chord>=0;
   vec3 p(91);bool actual=shapeSphereShadowReceiver(shape,f,sample,p);
   if(actual!=expected)return 1;
   if(!actual){misses++;if(!close(p,vec3(91)))return 2;continue;}
   hits++;double along=-sqrt(chord/3);x+=along;y+=along;z+=along;
   vec3 expectedPosition(float(cx+c*x-s*y),float(cy+s*x+c*y),float(cz+z));
   if(!close(p,expectedPosition))return 3;
  }
 }
 receiverFrame.voxelRenderOptions={1,8};
 receiverShapes[2].shapeType=SHAPE_SPHERE;receiverShapes[2].params.xyz={4,0,0};
 receiverTiles[2].shapeIndex=2;receiverTiles[2].tileIsoOrigin=ivec2(-3,-4);
 for(uint lane=0;lane<64;lane++)for(uint face=0;face<3;face++)for(uint half=0;half<2;half++){
  receiverOwners[3]=((2u*64u+lane)*3u+face)*2u+half;
  vec3 p,wantP;
  ivec2 expectedIso(-3+int(lane%8),-4+int(lane/8));
  bool expected=shapeSphereShadowReceiver(receiverShapes[2],receiverFrame,expectedIso,wantP);
  bool actual=selectedShapeSphereShadowReceiver(pixel,size.x,p);
  if(actual!=expected||(actual&&!close(p,wantP)))return 4;
 }
 receiverOwners[3]=(2u*64u+35u)*6u;
 for(int kind=0;kind<8;kind++){
  auto f=receiverFrame;auto shape=receiverShapes[2];
  if(kind==0)f.finiteCoverage=0;if(kind==1)f.voxelRenderOptions.x=0;
  if(kind==2)f.voxelRenderOptions.y=1;if(kind==3)f.latticeShapes=1;
  if(kind==4)shape.flags=1;if(kind==5)shape.shapeType=9;
  if(kind==6)f.residualYaw=.1;if(kind==7)shape.params.xyz.x=-1;
  vec3 p(91);
  if(shapeSphereShadowReceiver(shape,f,{0,0},p)||!close(p,vec3(91)))return 5;
 }
 for(int kind=0;kind<2;kind++){
  auto key=receiverOwners[3];receiverFrame.shapeCount=kind==0?0:1;
  if(kind==1)receiverOwners[3]=0xffffffffu;
  vec3 p(91);
  if(selectedShapeSphereShadowReceiver(pixel,size.x,p)||!close(p,vec3(91)))return 6;
  receiverOwners[3]=key;
 }
 receiverFrame.shapeCount=1;
 for(int disabled=0;disabled<2;disabled++)for(int supported=0;supported<2;supported++){
  shadowsEnabled=disabled?0:1;receiverShapes[2].shapeType=supported?SHAPE_SPHERE:9;
  surfaceCalls=legacyCalls=0;pos3D=vec3(91);normal=vec3(93);
  float result=route();
  if(disabled){if(result!=1||surfaceCalls||legacyCalls)return 7;}
  else if(supported){
   vec3 expectedPosition(-(4.f+.5f/8.f)/sqrt(3.f));
   if(result!=.25f||surfaceCalls!=1||legacyCalls!=0||!close(lastNormal,normal)||
      !close(lastPosition,expectedPosition))return 8;
  }
  else if(result!=.75f||surfaceCalls||legacyCalls!=1||
          !close(lastPosition,pos3D)||!close(lastNormal,normal))return 9;
  if(!close(pos3D,vec3(91))||!close(normal,vec3(93)))return 10;
 }
 // A camera-front sphere sample can be sun-back-facing while its displayed
 // stair normal is sun-lit. The finite ray must find the sphere's near side.
 receiverShapes[2].shapeType=SHAPE_SPHERE;
 vec3 backPoint;
 if(!shapeSphereShadowReceiver(receiverShapes[2],receiverFrame,{-36,-36},backPoint))return 12;
 vec3 sun(-1,0,0),displayNormal(-1,0,0);float radius=4.f+.5f/8.f,hit;
 if(dot(backPoint,sun)>=0||dot(displayNormal,sun)<=0)return 13;
 if(!sdfRaySphere(backPoint+sun*12.f,-sun,radius,0.f,12.f,hit)||12.f-hit<1.f)return 14;
 vec3 frontPoint(-radius,0,0);
 if(!sdfRaySphere(frontPoint+sun*12.f,-sun,radius,0.f,12.f,hit)||abs(12.f-hit)>.0001f)return 15;
 vec3 blocker=frontPoint+sun*6.f;
 if(!sdfRaySphere(frontPoint+sun*12.f-blocker,-sun,.75f,0.f,12.f,hit)||
    abs(12.f-hit-6.75f)>.0001f)return 16;
 if(sdfRaySphere(frontPoint+sun*12.f-blocker+vec3(0,2,0),-sun,.75f,0.f,12.f,hit))return 17;
 printf("sphere_receivers=%d misses=%d owner_keys=384 route_controls=4\n",hits,misses);
 return hits>0&&misses>0?0:11;
}
"""


@unittest.skipUnless(COMPILER, "sphere receiver controls require a C++ compiler")
class SphereShadowReceiverTest(unittest.TestCase):
    def test_production_geometry_ownership_and_shadow_only_route(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            shaders = ROOT / "engine/render/src/shaders" / folder
            common = (shaders / f"ir_iso_common.{suffix}").read_text()
            receiver = (shaders / f"ir_shape_receiver.{suffix}").read_text()
            roots = (shaders / f"ir_sdf_ray_roots.{suffix}").read_text()
            helper = (shaders / f"ir_shape_shadow_receiver.{suffix}").read_text()
            compute = (shaders / f"c_compute_sun_shadow_body.{suffix}").read_text()
            functions = "\n".join(
                extract_function(common, name)
                for name in (
                    "rasterYawCardinalIndex",
                    "cardinalYawCosSin",
                    "pos3DtoPos2DIsoYawed",
                    "isoPositionToPos3D",
                )
            )
            functions += "\n" + "\n".join(
                extract_function(receiver, name)
                for name in ("shapeYawCosSin", "shapeRenderedCenter")
            )
            origin_type = "ivec2" if suffix == "glsl" else "int2"
            functions += "\n" + extract_block(receiver, f"{origin_type} shapeRenderedOriginIso")
            functions += "\n" + extract_function(roots, "sdfRaySphere") + "\n" + helper
            route = compute[compute.index("    float factor = 1.0;") :]
            route = route[
                : route.index("    imageStore" if suffix == "glsl" else "    canvasSunShadow.write")
            ]
            route = re.sub(r"^#.*$", "", route, flags=re.MULTILINE)
            source = (
                HARNESS
                + cpp(functions)
                + "\nfloat route(){\n"
                + cpp(route)
                + "return factor;}\n"
                + CHECKS
            )
            mutations = {
                "radius_without_cell": ("0.5 / float(density)", "0.0 / float(density)"),
                "wrong_density": ("0.0) / float(density)", "0.0) / 1.0"),
                "raw_origin": (
                    "roundHalfUp(pos3DtoPos2DIsoYawed(center * float(density), "
                    "projection.visualYaw))",
                    "ivec2(pos3DtoPos2DIsoYawed(center * float(density), projection.visualYaw))",
                ),
                "wrong_yaw": (
                    "yaw.x * viewOffset.x - yaw.y * viewOffset.y",
                    "yaw.x * viewOffset.x + yaw.y * viewOffset.y",
                ),
                "wrong_lane": ("(key / 6u) % 64u", "(key / 2u) % 64u"),
                "lattice_accepted": ("projection.latticeShapes != 0", "false"),
                "disabled_queries": ("if (shadowsEnabled != 0)", "if (true)"),
                "radial_facing_gate": (
                    "worldSurfaceSunShadowFactor(shadowPosition, normal,",
                    "worldSurfaceSunShadowFactor(shadowPosition, normalize(shadowPosition),",
                ),
                "wrong_receiver_position": (
                    "worldSurfaceSunShadowFactor(shadowPosition, normal,",
                    "worldSurfaceSunShadowFactor(pos3D, normal,",
                ),
                "lighting_normal_overwrite": (
                    "float factor = 1.0;",
                    "float factor = 1.0; normal = vec3(0);",
                ),
            }
            with tempfile.TemporaryDirectory() as directory:
                for name, mutation in [("production", None), *mutations.items()]:
                    with self.subTest(backend=suffix, variant=name):
                        altered = source if mutation is None else source.replace(*mutation)
                        if mutation is not None:
                            self.assertNotEqual(altered, source, f"no-op mutation {name}")
                        path = Path(directory) / "probe.cpp"
                        path.write_text(altered)
                        binary = Path(directory) / "probe"
                        build = subprocess.run(
                            [COMPILER, "-std=c++20", "-O2", str(path), "-o", str(binary)],
                            capture_output=True,
                            text=True,
                        )
                        self.assertEqual(build.returncode, 0, build.stderr)
                        result = subprocess.run([str(binary)], capture_output=True, text=True)
                        if mutation is None:
                            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                            print(suffix, result.stdout.strip())
                        else:
                            self.assertNotEqual(result.returncode, 0, name)


if __name__ == "__main__":
    unittest.main()

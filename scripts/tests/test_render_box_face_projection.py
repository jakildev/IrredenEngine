"""Compare production oriented box-face emission with an independent slab oracle."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_block, extract_function

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")

HARNESS = r"""
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
struct vec3 {
 double x,y,z;
 explicit vec3(double n):x(n),y(n),z(n){}
 vec3(double a,double b,double c):x(a),y(b),z(c){}
 double& operator[](int i){return i==0?x:(i==1?y:z);}
 double operator[](int i)const{return i==0?x:(i==1?y:z);}
};
using std::abs;
struct vec4 {double x,y,z,w;vec4(double a,double b,double c,double d):x(a),y(b),z(c),w(d){}};
vec3 operator+(vec3 a,vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
vec3 operator-(vec3 a,vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
vec3 operator-(vec3 a){return {-a.x,-a.y,-a.z};}
vec3 operator*(vec3 a,double s){return {a.x*s,a.y*s,a.z*s};}
double dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
vec3 cross(vec3 a,vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
vec3 unit(vec3 a){return a*(1/std::sqrt(dot(a,a)));}
vec3 rotateByQuat(vec3 p,vec4 q){vec3 v{q.x,q.y,q.z};return p+cross(v,cross(v,p)+p*q.w)*2;}
vec3 sunSpaceProject(vec3 p,vec3 u,vec3 v,vec3 light){return {dot(p,u),dot(p,v),-dot(p,light)};}
struct Shape {vec4 rotation{0,0,0,1};} shape;
const bool box=true;
vec3 shapeCenter{2.25,-3.125,1.75};
vec3 halfExtent{1,2,3},sunDirection{0,0,-1},sunBasisU{1,0,0},sunBasisV{0,1,0};
bool fogBody=FOG_BODY;
struct {int x;} fogCeilingEnabled{FOG_CEILING};
struct Face {vec3 corner,u,v;};
std::vector<Face> faces;
unsigned currentLane=0,faceCalls=0;
void indexSourceSunFace(vec3 c,vec3 u,vec3 v){
 ++faceCalls;if(currentLane==0)faces.push_back({c,u,v});
}
"""

CASES = r"""
int main(){
 long checks=0,hits=0;
 for(vec3 axis : {vec3{1,0,0},vec3{0,1,0},vec3{0,0,1},unit(vec3{1,2,-3})})
 for(int pose=0;pose<8;++pose)
 for(vec3 light : {vec3{1,0,0},vec3{-1,0,0},vec3{0,1,0},vec3{0,-1,0},
                   vec3{0,0,1},vec3{0,0,-1},unit(vec3{1,2,3}),unit(vec3{-3,1,-2})}) {
  double a=pose*3.141592653589793/8;
  shape.rotation={axis.x*std::sin(a),axis.y*std::sin(a),axis.z*std::sin(a),std::cos(a)};
  sunDirection=light;
  sunBasisU=unit(cross(light,std::abs(light.z)<.9?vec3{0,0,1}:vec3{0,1,0}));
  sunBasisV=cross(light,sunBasisU);
  faces.clear();faceCalls=0;emit();if(faces.size()!=3||faceCalls!=192)return 1;
  const vec4 q=shape.rotation;
  // Transposed rotation matrix transforms each world ray into box coordinates.
  const double m[3][3]={
   {1-2*(q.y*q.y+q.z*q.z),2*(q.x*q.y-q.z*q.w),2*(q.x*q.z+q.y*q.w)},
   {2*(q.x*q.y+q.z*q.w),1-2*(q.x*q.x+q.z*q.z),2*(q.y*q.z-q.x*q.w)},
   {2*(q.x*q.z-q.y*q.w),2*(q.y*q.z+q.x*q.w),1-2*(q.x*q.x+q.y*q.y)}};
  const vec3 center=sunSpaceProject(shapeCenter,sunBasisU,sunBasisV,light);
  for(int x=-30;x<=30;++x)for(int y=-30;y<=30;++y){
   const double u=center.x+x*.137+.017,v=center.y+y*.137+.031;
   double actual=1e30;
   for(const Face& f:faces){
    double det=f.u.x*f.v.y-f.u.y*f.v.x;
    if(std::abs(det)<1e-6)continue;
    double du=u-f.corner.x,dv=v-f.corner.y;
    double s=(du*f.v.y-dv*f.v.x)/det,t=(f.u.x*dv-f.u.y*du)/det;
    if(s>=0&&s<=1&&t>=0&&t<=1)actual=std::min(actual,f.corner.z+s*f.u.z+t*f.v.z);
   }
   vec3 origin=sunBasisU*u+sunBasisV*v-shapeCenter;
   double near=-1e30,far=1e30;bool hit=true;
   for(int i=0;i<3;++i){
    double o=0,d=0;for(int j=0;j<3;++j){o+=m[j][i]*origin[j];d-=m[j][i]*light[j];}
    if(std::abs(d)<1e-7){if(std::abs(o)>halfExtent[i])hit=false;}
    else {double a=(-halfExtent[i]-o)/d,b=(halfExtent[i]-o)/d;
     near=std::max(near,std::min(a,b));far=std::min(far,std::max(a,b));}
   }
   hit=hit&&near<=far;
   if(hit!=(actual<1e29)||(hit&&std::abs(actual-near)>2e-5))return 2;
   ++checks;hits+=hit;
  }
 }
 std::cout<<checks<<" rays, "<<hits<<" finite hits\n";
}
"""


@unittest.skipUnless(COMPILER, "box face projection controls require a C++ compiler")
class BoxFaceProjectionTest(unittest.TestCase):
    def test_fog_cut_shape_index_and_depth_layer(self):
        # The ray solver is controlled here so its AABB entry and final surface
        # straddle the ceiling. Geometry solvers have separate numeric oracles.
        preamble = r"""
#include <algorithm>
#include <cmath>
using uint=unsigned;
struct vec2 {float x,y;};
struct vec3 {float x,y,z;};
vec3 operator+(vec3 a,vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
vec3 operator-(vec3 a,vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
vec3 operator*(vec3 a,float b){return {a.x*b,a.y*b,a.z*b};}
struct {uint shapeType;vec3 params;} shape;
const uint kSourceFaceFallbackOffset=4;
const int kCascadeTexelCount=1,kSunShadowMapDim=1;
uint sunDepthBuf[8];
float solvedDepth=3,lastFogZ=-99;
int fogCalls=0;
bool solverHit=true;
bool sdfRayHit(uint,vec3,vec3,vec3,float,float,float,float& depth){
 depth=solvedDepth;return solverHit;
}
bool fogFieldCastsSunShadow(vec3 pos,bool body,int enabled){
 ++fogCalls;lastFogZ=pos.z;return body||!enabled||pos.z>=2;
}
uint packSunSurfaceDepth(float d){return uint(d*100);}
void atomicMin(uint& a,uint b){a=std::min(a,b);}
"""
        checks = r"""
int main(){
 for(bool box:{false,true})for(bool body:{false,true})for(int ceiling:{0,1})
 for(float depth:{1.f,3.f})for(bool complete:{false,true}){
  std::fill(sunDepthBuf,sunDepthBuf+8,999u);fogCalls=0;lastFogZ=-99;
  solvedDepth=depth;solverHit=true;
  const uint indexed=emit(box,body,ceiling);
  const bool retained=body||!ceiling||depth>=2;
  if(indexed!=uint(body||!ceiling))return 1;
  if(fogCalls!=1||std::abs(lastFogZ-depth)>1.e-5)return 2;
  const uint layer=body||!ceiling?kSourceFaceFallbackOffset:0u;
  if(sunDepthBuf[layer]!=(retained?uint(depth*100):999u))return 3;
  if(sunDepthBuf[layer==0?kSourceFaceFallbackOffset:0]!=999u)return 4;
  // Complete empty and BODY-only exact lists both skip the indexed fallback.
  // Deliberately unindexed FIELD samples must remain visible in either case.
  if(!body&&ceiling&&readDepth(complete)!=(retained?uint(depth*100):999u))return 5;
 }
 for(bool body:{false,true}){
  std::fill(sunDepthBuf,sunDepthBuf+8,999u);fogCalls=0;solverHit=false;
  emit(false,body,1);
  if(fogCalls||readDepth(false)!=999u)return 6;
 }
}
"""
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            shaders = ROOT / "engine/render/src/shaders" / folder
            source = (shaders / f"c_bake_box_sun_shadow.{suffix}").read_text()
            sample = (shaders / f"ir_sun_shadow_sample.{suffix}").read_text()
            policy = re.search(r"const bool indexWholeShape = [^;]+;", source).group()
            offset = re.search(r"const uint depthLayerOffset = [^;]+;", source).group()
            gates = [extract_block(source, start).split("{", 1)[0]
                     for start in ("if (box &&", "if (!box &&")]
            # Both finite record types use the same whole-shape permission.
            self.assertTrue(all("indexWholeShape" in gate for gate in gates))
            solved = extract_block(source, "if (hit && nearDepth <= farDepth &&")
            skip = re.search(r"if \(layer == 1 && sourceQueryComplete\) continue;", sample).group()
            solved = solved.replace("continue;", "return uint(indexWholeShape);")
            solved = re.sub(r"atomic_fetch_min_explicit\(&sunDepthBuf\[([^]]+)\], "
                            r"([^;]+), memory_order_relaxed\);",
                            r"atomicMin(sunDepthBuf[\1], \2);", solved)
            body = (policy + offset + "float casterDepth=nearDepth;" + solved
                    + "return uint(indexWholeShape);")
            body = body.replace("sunFrame.", "").replace(".xyz", "")
            body = body.replace(", fogObservers", "").replace("float3", "vec3")
            variants = {
                "production": body,
                "uncut_index": body.replace("fogBody || fogCeilingEnabled.x == 0", "true"),
                "aabb_ceiling": body.replace("* casterDepth", "* nearDepth"),
                "lost_unindexed_layer": body.replace(
                    "indexWholeShape ? kSourceFaceFallbackOffset : 0u",
                    "kSourceFaceFallbackOffset"),
            }
            emitter = r"""
uint emit(bool box,bool fogBody,int ceiling){
 struct {int x;} fogCeilingEnabled{ceiling};
 const float threshold=.5f,nearDepth=box?solvedDepth:solvedDepth-1,farDepth=8;
 const bool hit=true;const int cascade=0;
 const vec2 uv{0,0};struct {int x,y;} pixel{0,0};
 const vec3 sunBasisU{1,0,0},sunBasisV{0,1,0},sunDirection{0,0,-1};
 const vec3 rayOrigin{0,0,0},direction{0,0,1};
 BODY
}
"""
            receiver = ("uint readDepth(bool sourceQueryComplete){uint depth=999;"
                        "for(int layer=0;layer<2;++layer){" + skip
                        + "depth=std::min(depth,sunDepthBuf[layer?kSourceFaceFallbackOffset:0]);}"
                        "return depth;}")
            for name, variant in variants.items():
                with (self.subTest(backend=suffix, mutation=name),
                      tempfile.TemporaryDirectory() as tmp):
                    if name != "production":
                        self.assertNotEqual(variant, body)
                    cpp, exe = Path(tmp) / "fog.cpp", Path(tmp) / "fog"
                    cpp.write_text(preamble + emitter.replace("BODY", variant) + receiver + checks)
                    build = subprocess.run([COMPILER, "-std=c++17", str(cpp), "-o", str(exe)],
                                           capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(exe)], capture_output=True, text=True)
                    self.assertEqual(run.returncode, {"production": 0, "uncut_index": 1,
                                                     "aabb_ceiling": 2,
                                                     "lost_unindexed_layer": 3}[name])

    def test_oriented_faces_match_slab_entry(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            source = (ROOT / f"engine/render/src/shaders/{folder}"
                      f"c_bake_box_sun_shadow.{suffix}").read_text()
            face_block = extract_block(source, "if (box && ")
            gate = face_block[face_block.index("(") + 1:face_block.index(")")]
            block = face_block[face_block.index("{") + 1:-1]
            rotation = re.search(r"const (?:vec4|float4) rotation = [^;]+;", source).group()
            data = (ROOT / f"engine/render/src/shaders/{folder}"
                    f"ir_shape_data.{suffix}").read_text()
            rotation_helper = extract_function(data, "shapeHasEntityRotation")
            rotation_helper = rotation_helper.replace("float4", "vec4")
            index_policy = re.search(r"const bool indexWholeShape = [^;]+;", source).group()
            index_policy = index_policy.replace("sunFrame.", "")
            block = rotation + "\n" + block
            block = block.replace("sunFrame.", "").replace(".xyz", "")
            block = block.replace("sunDepthBuf, ", "").replace(", sunFrame", "")
            block = block.replace(", localId.x, sharedFaceIndex", "")
            block = block.replace("float3", "vec3").replace("float4", "vec4")
            block = block.replace("gl_LocalInvocationID.x", "lane").replace("localId.x", "lane")
            variants = {
                "production": block,
                "duplicate_index": block,
                "missing_face": block.replace("axis < 3", "axis < 2"),
                "wrong_polarity": block.replace("> 0.0", "< 0.0"),
                "unrotated_edges": block.replace(
                    "edgeU = rotateByQuat(edgeU, rotation);", ""),
                "half_extent_edge": block.replace("2.0 * halfExtent", "halfExtent"),
                "lost_translation": block.replace("shapeCenter +", ""),
            }
            gate = gate.replace("gl_WorkGroupID.z", "group").replace("groupId.z", "group")
            gate = gate.replace("gl_LocalInvocationID.x", "lane").replace("localId.x", "lane")
            gate = gate.replace("sunFrame.", "")
            # A FIELD box under an enabled ceiling indexes no whole faces; its
            # cut samples go through the fallback ray instead.
            variants["body_under_ceiling"] = block
            variants["field_under_ceiling"] = block
            passing = ("production", "body_under_ceiling")
            for name, body in variants.items():
                with (self.subTest(backend=suffix, mutation=name),
                      tempfile.TemporaryDirectory() as tmp):
                    if name not in passing + ("duplicate_index", "field_under_ceiling"):
                        self.assertNotEqual(body, block)
                    cpp, exe = Path(tmp) / "box.cpp", Path(tmp) / "box"
                    selected_gate = "true" if name == "duplicate_index" else gate
                    fog = (f"#define FOG_BODY {int(name == 'body_under_ceiling')}\n"
                           f"#define FOG_CEILING {int(name.endswith('_under_ceiling'))}\n")
                    emitter = ("void emit(){for(unsigned group=0;group<32;++group)"
                               "for(unsigned lane=0;lane<64;++lane){" + index_policy + "if("
                               + selected_gate + "){currentLane=lane;" + body + "}}}")
                    cpp.write_text(fog + HARNESS + rotation_helper + emitter + CASES)
                    build = subprocess.run(
                        [COMPILER, "-std=c++17", "-O2", str(cpp), "-o", str(exe)],
                        capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(exe)], capture_output=True, text=True)
                    if name in passing:
                        self.assertEqual(run.returncode, 0, run.stderr)
                        print(suffix, name, run.stdout.strip())
                    else:
                        self.assertNotEqual(run.returncode, 0)


if __name__ == "__main__":
    unittest.main()

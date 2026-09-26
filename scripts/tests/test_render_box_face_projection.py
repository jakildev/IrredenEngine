"""Compare production oriented box-face emission with an independent slab oracle."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

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
struct vec4 {double x,y,z,w;};
vec3 operator+(vec3 a,vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
vec3 operator-(vec3 a,vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
vec3 operator-(vec3 a){return {-a.x,-a.y,-a.z};}
vec3 operator*(vec3 a,double s){return {a.x*s,a.y*s,a.z*s};}
double dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
vec3 cross(vec3 a,vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
vec3 unit(vec3 a){return a*(1/std::sqrt(dot(a,a)));}
vec3 rotateByQuat(vec3 p,vec4 q){vec3 v{q.x,q.y,q.z};return p+cross(v,cross(v,p)+p*q.w)*2;}
vec3 sunSpaceProject(vec3 p,vec3 u,vec3 v,vec3 light){return {dot(p,u),dot(p,v),-dot(p,light)};}
struct Shape {vec3 worldPosition{2.25,-3.125,1.75};vec4 rotation{0,0,0,1};} shape;
vec3 halfExtent{1,2,3},sunDirection{0,0,-1},sunBasisU{1,0,0},sunBasisV{0,1,0};
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
  const vec3 center=sunSpaceProject(shape.worldPosition,sunBasisU,sunBasisV,light);
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
   vec3 origin=sunBasisU*u+sunBasisV*v-shape.worldPosition;
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
    def test_oriented_faces_match_slab_entry(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            source = (ROOT / f"engine/render/src/shaders/{folder}"
                      f"c_bake_box_sun_shadow.{suffix}").read_text()
            start = source.index("        for (int axis = 0;")
            end = source.index("    for (int cascade", start)
            block = source[start:end].rsplit("    }", 1)[0]
            block = block.replace("sunFrame.", "").replace(".xyz", "")
            block = block.replace("sunDepthBuf, ", "").replace(", sunFrame", "")
            block = block.replace(", localId.x, sharedFaceIndex", "")
            block = block.replace("float3", "vec3")
            block = block.replace("gl_LocalInvocationID.x", "lane").replace("localId.x", "lane")
            variants = {
                "production": block,
                "duplicate_index": block,
                "missing_face": block.replace("axis < 3", "axis < 2"),
                "wrong_polarity": block.replace("> 0.0", "< 0.0"),
                "unrotated_edges": block.replace(
                    "edgeU = rotateByQuat(edgeU, shape.rotation);", ""),
                "half_extent_edge": block.replace("2.0 * halfExtent", "halfExtent"),
                "lost_translation": block.replace("shape.worldPosition +", ""),
            }
            gate = re.search(r"if \(((?:gl_WorkGroupID|groupId).*?)\) \{", source)[1]
            gate = gate.replace("gl_WorkGroupID.z", "group").replace("groupId.z", "group")
            gate = gate.replace("gl_LocalInvocationID.x", "lane").replace("localId.x", "lane")
            for name, body in variants.items():
                with (self.subTest(backend=suffix, mutation=name),
                      tempfile.TemporaryDirectory() as tmp):
                    if name not in ("production", "duplicate_index"):
                        self.assertNotEqual(body, block)
                    cpp, exe = Path(tmp) / "box.cpp", Path(tmp) / "box"
                    selected_gate = "true" if name == "duplicate_index" else gate
                    emitter = ("void emit(){for(unsigned group=0;group<32;++group)"
                               "for(unsigned lane=0;lane<64;++lane)if("
                               + selected_gate + "){currentLane=lane;" + body + "}}")
                    cpp.write_text(HARNESS + emitter + CASES)
                    build = subprocess.run(
                        [COMPILER, "-std=c++17", "-O2", str(cpp), "-o", str(exe)],
                        capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(exe)], capture_output=True, text=True)
                    if name == "production":
                        self.assertEqual(run.returncode, 0, run.stderr)
                        print(suffix, run.stdout.strip())
                    else:
                        self.assertNotEqual(run.returncode, 0)


if __name__ == "__main__":
    unittest.main()

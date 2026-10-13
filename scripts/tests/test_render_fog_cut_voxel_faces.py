"""Retained FIELD solids expose cut boundaries without changing BODY or disabled fog."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_block, extract_function
from test_render_box_face_projection import HARNESS

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")

PREAMBLE = HARNESS.split("struct Shape")[0] + r"""
double max(double a,double b){return a>b?a:b;}
struct vec2 {double x,y;};
vec2 operator-(vec2 a,vec2 b){return {a.x-b.x,a.y-b.y};}
vec2 xy(vec3 a){return {a.x,a.y};}
vec2 xy(vec4 a){return {a.x,a.y};}
double length(vec2 a){return std::hypot(a.x,a.y);}
double smoothstep(double a,double b,double x){
 double t=std::clamp((x-a)/(b-a),0.,1.);return t*t*(3-2*t);
}
int fogShadowVisionCircleCount=1;
vec4 fogShadowVisionCircles[2]={{0,0,100,0},{0,0,100,0}};
vec4 fogShadowVisionCircleHeights[2]={{4,0,0,0},{4,0,0,0}};
vec4 fogShadowVisionCircleCeilings[2]={{5,4,0,0},{-1,0,0,0}};
struct {int x;} fogCeilingEnabled{1};
vec4 viewToWorld{0,0,0,1};
vec3 worldOrigin(0),sunDirection(.35,.85,-.4),kVoxelRasterCellAnchor(.5);
bool faceIsExposed(unsigned flags,int face){return (flags&(1u<<face))!=0;}
struct Face {vec3 corner,u,v;};
std::vector<Face> faces;
"""

CHECKS = r"""
bool occupied(int x,int y,int z){return x>=-1&&x<=2&&y>=-1&&y<=2&&z>=-11&&z<=4;}
bool retained(vec3 p,bool body,int enabled){return body||!enabled||p.z>=-3;}
// Independent ray/slab oracle for the union of retained original voxel cells.
bool slab(vec3 origin,vec3 ray,vec3 center,vec4 q){
 vec4 inv{-q.x,-q.y,-q.z,q.w};
 vec3 p=rotateByQuat(origin-center,inv),d=rotateByQuat(ray,inv);
 double lo=0,hi=1e30;
 for(int a=0;a<3;++a){
  if(abs(d[a])<1e-10){if(abs(p[a])>.5)return false;continue;}
  double x=(-.5-p[a])/d[a],y=(.5-p[a])/d[a];
  lo=max(lo,std::min(x,y));hi=std::min(hi,max(x,y));
 }
 return lo<=hi&&hi>1e-6;
}
bool faceHit(vec3 origin,vec3 ray){
 for(auto f:faces){
  vec3 n=cross(f.u,f.v);double den=dot(n,ray);
  if(abs(den)<1e-10)continue;
  double t=dot(n,f.corner-origin)/den;if(t<=1e-6)continue;
  vec3 p=origin+ray*t-f.corner;
  double u=dot(p,f.u)/dot(f.u,f.u),v=dot(p,f.v)/dot(f.v,f.v);
  if(u>=-1e-9&&u<=1+1e-9&&v>=-1e-9&&v<=1+1e-9)return true;
 }
 return false;
}
int main(){
 unsigned checks=0;
 for(vec4 q:{vec4(0,0,0,1),vec4(0,0,sin(.3),cos(.3)),vec4(sin(.2),0,0,cos(.2))})
 for(bool body:{false,true})for(int enabled:{0,1}){
  viewToWorld=q;fogCeilingEnabled.x=enabled;faces.clear();
  std::vector<vec3> cells;
  for(int x=-1;x<=2;++x)for(int y=-1;y<=2;++y)for(int z=-11;z<=4;++z){
   vec3 cell(x,y,z);unsigned flags=0;
   for(int axis=0;axis<3;++axis)for(int side:{-1,1}){
    int nx=x+(axis==0?side:0),ny=y+(axis==1?side:0),nz=z+(axis==2?side:0);
    if(!occupied(nx,ny,nz))flags|=1u<<(2*axis+(side>0));
   }
   emit(cell,flags,body);
   vec3 world=rotateByQuat(cell,q);
   if(retained(world,body,enabled))cells.push_back(world);
  }
  if((body||!enabled)&&faces.size()!=144)return 4;
  for(double x=-16.213;x<8;x+=.703)for(double y=-25.317;y<8;y+=.719){
   vec3 p(x,y,10);bool expected=false;
   for(vec3 c:cells)expected=expected||slab(p,sunDirection,c,q);
   if(faceHit(p,sunDirection)!=expected){
    std::cerr<<"mismatch q="<<q.x<<","<<q.z<<" body="<<body<<" enabled="<<enabled
             <<" xy="<<x<<","<<y<<" faces="<<faces.size()<<" expected="<<expected<<"\n";
    return 1;
   }
   ++checks;
  }
  if(q.w==1&&!body&&enabled&&!faceHit(vec3(-7,-16,4.5),sunDirection))return 2;
 }
 // A second covering source with no ceiling keeps the neighbor and its face hidden.
 fogShadowVisionCircleCount=2;
 if(fogVoxelFaceCastsSunShadow(false,vec3(0,0,-3),vec3(0,0,-1),false,1))return 3;
 std::cout<<checks<<" retained-solid rays; cut-cap witness and covering-source union pass\n";
}
"""


@unittest.skipUnless(COMPILER, "fog cut faces require a C++ compiler")
class FogCutVoxelFacesTest(unittest.TestCase):
    def test_retained_solid_matches_bake_faces(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            shaders = ROOT / "engine/render/src/shaders" / folder
            fog = (shaders / f"ir_fog_shadow_caster.{suffix}").read_text()
            common = (shaders / f"ir_iso_common.{suffix}").read_text()
            bake = (shaders / f"c_bake_voxel_sun_faces.{suffix}").read_text()
            functions = extract_function(common, "fogCeilingVisibility") + "\n"
            functions += extract_function(fog, "fogFieldCastsSunShadow") + "\n"
            functions += extract_function(fog, "fogVoxelFaceCastsSunShadow")
            gate = re.search(r"if \(!fogFieldCastsSunShadow\([\s\S]*?\)\) return;", bake).group()
            loop = extract_block(bake, "for (int axis = 0;")
            loop = loop[:loop.index("        // Arbitrary source rotations")]
            emit = ("void emit(vec3 position,unsigned flags,bool fogBody){"
                    "vec3 worldCenter=rotateByQuat(position,viewToWorld);" + gate + loop
                    + "faces.push_back({corner,edgeU,edgeV});}}")
            code = functions + emit
            code = re.sub(r",\s*constant FogShadowObserverData& observers", "", code)
            code = re.sub(r",\s*(?:observers|fogObservers)\)", ")", code)
            for field in ("visionCircleCount", "visionCircles", "visionCircleHeights",
                          "visionCircleCeilings"):
                code = code.replace("observers." + field,
                                    "fogShadow" + field[0].upper() + field[1:])
            code = code.replace("sunFrame.", "").replace("faceFrame.", "")
            code = code.replace(".xyz", "").replace("float3", "vec3").replace("float4", "vec4")
            code = re.sub(r"(worldPos|fogShadowVisionCircles\[i\])\.xy", r"xy(\1)", code)
            variants = {
                "production": code,
                "missing_cap": code.replace("return !fogFieldCastsSunShadow(",
                                             "return false && !fogFieldCastsSunShadow("),
                "reversed_neighbor": code.replace("worldCenter + outwardNormal",
                                                   "worldCenter - outwardNormal"),
                "lost_body_guard": code.replace("if (fogBody || ceilingEnabled == 0) return false;",
                                                "if (ceilingEnabled == 0) return false;"),
            }
            for name, variant in variants.items():
                with (self.subTest(backend=suffix, mutation=name),
                      tempfile.TemporaryDirectory() as tmp):
                    if name != "production":
                        self.assertNotEqual(variant, code)
                    cpp, exe = Path(tmp) / "fog.cpp", Path(tmp) / "fog"
                    cpp.write_text(PREAMBLE + variant + CHECKS)
                    build = subprocess.run([COMPILER, "-std=c++17", "-O2", str(cpp),
                                            "-o", str(exe)], capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(exe)], capture_output=True, text=True)
                    if name == "production":
                        self.assertEqual(run.returncode, 0, run.stderr)
                        print(suffix, run.stdout.strip())
                    else:
                        self.assertNotEqual(run.returncode, 0, name)


if __name__ == "__main__":
    unittest.main()

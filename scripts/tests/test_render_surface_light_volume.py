"""Execute shared world-light queries with checked volume, ID and spotlight adapters."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")

HARNESS = r"""
#include <cmath>
#include <cstdlib>
#include <iostream>
struct vec3 {
 float x,y,z;
 explicit vec3(float a):x(a),y(a),z(a){}
 vec3(float a,float b,float c):x(a),y(b),z(c){}
 vec3& operator*=(float a){x*=a;y*=a;z*=a;return *this;}
};
vec3 operator+(vec3 a,vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
vec3 operator-(vec3 a,vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
vec3 operator/(vec3 a,vec3 b){return {a.x/b.x,a.y/b.y,a.z/b.z};}
vec3 operator*(vec3 a,float b){return {a.x*b,a.y*b,a.z*b};}
struct ivec3 {
 int x,y,z;
 explicit ivec3(int a):x(a),y(a),z(a){}
 explicit ivec3(vec3 a):x(int(a.x)),y(int(a.y)),z(int(a.z)){}
};
struct ivec4 {int x,y,z,w;vec3 xyz()const{return {float(x),float(y),float(z)};}};
struct vec4 {float x,y,z,a;vec3 rgb()const{return {x,y,z};}};
vec3 floor(vec3 a){return {std::floor(a.x),std::floor(a.y),std::floor(a.z)};}
bool operator>=(ivec3 a,ivec3 b){return a.x>=b.x&&a.y>=b.y&&a.z>=b.z;}
bool operator<(ivec3 a,ivec3 b){return a.x<b.x&&a.y<b.y&&a.z<b.z;}
bool all(bool a){return a;}
bool greaterThanEqual(ivec3 a,ivec3 b){return a>=b;}
bool lessThan(ivec3 a,ivec3 b){return a<b;}
int roundHalfUp(float a){return int(std::floor(a+.5f));}
constexpr float kLightVolumeHalfExtent=64,kLightVolumeSize=128;
constexpr int kLightTypeSpot=3;
struct Light {struct {int w;} originAndType;};
Light lights[2]{{{1}},{{3}}};
vec3 expectedPosition{0},expectedCoord{0};ivec3 expectedCell{0};
int winner=0,volumeReads=0,idReads=0,coneCalls=0;
bool near(vec3 a,vec3 b){
 return std::abs(a.x-b.x)<1e-6&&std::abs(a.y-b.y)<1e-6&&std::abs(a.z-b.z)<1e-6;}
float spotConeFactor(int id,vec3 p){
 ++coneCalls;
 if(id!=1||!near(p,expectedPosition))std::exit(11);
 return .25f;}
vec4 sampleVolume(vec3 coord,float lod){
 ++volumeReads;
 if(lod!=0||!near(coord,expectedCoord))std::exit(12);
 return {.2f,.5f,.7f,.4f};}
vec4 sampleIds(ivec3 cell){
 ++idReads;
 if(!(cell>=ivec3(0))||!(cell<ivec3(128))
    ||cell.x!=expectedCell.x||cell.y!=expectedCell.y||cell.z!=expectedCell.z)std::exit(13);
 return {winner/255.f,0,0,1};}
"""
CASES = r"""
int main(){
 int checks=0;
 for(int anchor:{-10000,0,12345})for(int spot:{0,1})for(int id:{0,1,2})
 for(float x:{-65.f,-64.51f,-64.5f,-64.f,-.5f,0.f,.49f,63.f,63.49f,63.5f,64.f})
 for(int axis=0;axis<3;++axis){
  ivec4 volumeOrigin{anchor,anchor-4,anchor+7,spot};
  vec3 position=volumeOrigin.xyz()+vec3(axis==0?x:1.25f,axis==1?x:-2.75f,axis==2?x:.125f);
  expectedPosition=position;
  vec3 local=position-volumeOrigin.xyz();
  expectedCoord={(local.x+64.5f)/128,(local.y+64.5f)/128,(local.z+64.5f)/128};
  expectedCell=ivec3(floor(vec3(local.x+64.5f,local.y+64.5f,local.z+64.5f)));
  winner=id;volumeReads=idReads=coneCalls=0;
  vec3 result=query(position,volumeOrigin);
  bool readId=spot&&expectedCell>=ivec3(0)&&expectedCell<ivec3(128);
  bool cone=readId&&id==2;
  vec3 expected=vec3(.2f,.5f,.7f)*.4f*(cone?.25f:1.f);
  if(!near(result,expected)||volumeReads!=1||idReads!=int(readId)||coneCalls!=int(cone))return 1;
  ++checks;
 }
 std::cout<<checks<<" checked queries\n";
}
"""


@unittest.skipUnless(COMPILER, "surface-light queries require a C++ compiler")
class SurfaceLightVolumeTest(unittest.TestCase):
    def test_production_queries_and_mutations(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            path = ROOT / f"engine/render/src/shaders/{folder}ir_surface_light_volume.{suffix}"
            source = path.read_text()
            body = source[source.index("{") + 1:source.rindex("}")]
            body = body.replace("float3", "vec3").replace("float4", "vec4")
            body = re.sub(r"\bint3\b", "ivec3", body)
            body = body.replace(".xyz", ".xyz()").replace(".rgb", ".rgb()")
            body = body.replace("textureLod(volume, ", "sampleVolume(")
            body = body.replace("volume.sample(volumeSampler, ", "sampleVolume(")
            body = body.replace("level(0.0)", "0.0")
            body = body.replace("imageLoad(lightVolumeId, cell).r", "sampleIds(cell).x")
            body = body.replace("winnerIds.read(uint3(cell)).r", "sampleIds(cell).x")
            body = body.replace("spotConeFactor(lights, ", "spotConeFactor(")
            variants = {
                "production": body,
                "lost_origin": body.replace("position - vec3(volumeOrigin.xyz())", "position"),
                "lost_half_cell": body.replace("+ vec3(0.5)", ""),
                "ignored_strength": body.replace("* sampleValue.a", "* 1.0"),
                "ignored_spot_gate": body.replace("volumeOrigin.w != 0", "true"),
                "wrong_spot_position": body.replace(
                    "spotConeFactor(winner - 1, position)",
                    "spotConeFactor(winner - 1, localPosition)"),
            }
            for name, code in variants.items():
                with (self.subTest(backend=suffix, mutation=name),
                      tempfile.TemporaryDirectory() as tmp):
                    if name != "production":
                        self.assertNotEqual(code, body)
                    cpp, exe = Path(tmp) / "query.cpp", Path(tmp) / "query"
                    cpp.write_text(HARNESS + "vec3 query(vec3 position,ivec4 volumeOrigin){"
                                   + code + "}" + CASES)
                    build = subprocess.run([COMPILER, "-std=c++17", str(cpp), "-o", str(exe)],
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

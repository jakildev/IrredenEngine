"""Execute shared voxel-face selection from GLSL and Metal on a host compiler.

The adapter stubs fog texture access and vector math but compiles the shader's
face-selection body and exposed-mask helper. It does not exercise raster taps.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_render_per_axis_caster_position import COMPILER, ROOT, cpp, functions


def selection_source(suffix, directory, remove_route_guard=False):
    shaders = ROOT / "engine/render/src/shaders" / directory
    selector = functions((shaders / f"ir_voxel_face_select.{suffix}").read_text(),
                         "selectVoxelFace")
    if remove_route_guard:
        site = " && perAxisRouteIn == 0"
        if selector.count(site) != 1:
            raise ValueError("missing per-axis riser gate mutation site")
        selector = selector.replace(site, "")
    if suffix == "metal":
        selector, count = re.subn(
            r"texture2d<uint, access::read> fog,\s*constant FogObserverData& obs,\s*",
            "", selector)
        if count != 1:
            raise ValueError("missing Metal fog arguments")
        selector = selector.replace("obs.visionCircleCount", "visionCircleCount")
        selector = selector.replace("fogColumnReveal(fog, obs, ", "fogColumnReveal(")
    exposed = functions((shaders / f"ir_iso_common.{suffix}").read_text(),
                        "faceIsExposed")
    return cpp(exposed + "\n" + selector).replace("constant ", "")


PREAMBLE = r"""
#include <cstdio>
#include <cstdint>
#include <initializer_list>
using uint=unsigned int;
struct vec2 {float x,y;explicit vec2(float v=0):x(v),y(v){}vec2(float a,float b):x(a),y(b){}};
struct ivec2 {
    int x,y;explicit ivec2(int v=0):x(v),y(v){}ivec2(int a,int b):x(a),y(b){}
    ivec2 operator+(ivec2 v)const{return {x+v.x,y+v.y};}
};
struct ivec3 {ivec2 xy;int z;ivec3(int x,int y,int z):xy(x,y),z(z){}};
struct vec3 {
    vec2 xy;float z;
    explicit vec3(float v=0):xy(v),z(v){}
    vec3(float x,float y,float z):xy(x,y),z(z){}
    explicit vec3(ivec3 v):xy(float(v.xy.x),float(v.xy.y)),z(float(v.z)){}
};
struct vec4 {
    vec3 xyz;vec2 xy;float w;
    vec4(float x=0,float y=0,float z=0,float w=0):xyz(x,y,z),xy(x,y),w(w){}
};
constexpr int kFaceZNeg=4;
int visionCircleCount=0;
float fogReveal=1.f;
float fogColumnReveal(ivec2){return fogReveal;}
ivec3 roundHalfUp(vec3 v){return {int(v.xy.x),int(v.xy.y),int(v.z)};}
ivec2 roundHalfUp(vec2 v){return {int(v.x),int(v.y)};}
vec3 rotateByQuat(vec3 v,vec4){return v;}
ivec3 faceOutwardNormal6I(int face){
    int value=(face&1)?1:-1;
    if(face<2)return {value,0,0};
    if(face<4)return {0,value,0};
    return {0,0,value};
}
struct VoxelFaceSelect {
    int faceId,riserFlip;bool bothPolaritiesExposed,fogActive;
    ivec2 worldColumn;bool keepFace,isCutFace;
};
"""


CHECKS = r"""
bool exposed(int mask,int face){return (mask&(1<<face))==0;}
int main(){
    int cases=0,cardinalFlips=0,cardinalDuals=0;
    const vec4 position(0,0,0,1),receive(0,0,0,0),quaternion(0,0,0,1);
    for(int face=0;face<6;++face)for(int mask=0;mask<64;++mask)
    for(uint reserved:{0u,4u,12u})for(bool revoxelized:{false,true})
    for(int route=0;route<=3;++route){
        visionCircleCount=0;
        const VoxelFaceSelect actual=selectVoxelFace(face,revoxelized,reserved,
            uint(mask<<2),position,route,0.f,receive,quaternion);
        const bool cardinalRiser=route==0 && !revoxelized && (reserved&4u)!=0u;
        const bool flip=cardinalRiser && !exposed(mask,face) && exposed(mask,face^1);
        const int expectedFace=flip?(face^1):face;
        const bool dual=cardinalRiser && exposed(mask,face) && exposed(mask,face^1);
        const bool keep=exposed(mask,expectedFace);
        if(actual.faceId!=expectedFace || actual.riserFlip!=int(flip) ||
           actual.bothPolaritiesExposed!=dual || actual.keepFace!=keep ||
           actual.isCutFace || actual.fogActive){
            std::fprintf(stderr,"face selection route leak face=%d mask=%d route=%d "
                "reserved=%u revoxelized=%d\n",face,mask,route,reserved,revoxelized);
            return 1;
        }
        if(route!=0 && (actual.faceId!=face || actual.riserFlip || actual.bothPolaritiesExposed ||
                        actual.keepFace!=exposed(mask,face))){
            std::fprintf(stderr,"per-axis backface emitted\n");return 2;
        }
        cardinalFlips+=flip;cardinalDuals+=dual;++cases;
    }
    if(cases!=9216 || !cardinalFlips || !cardinalDuals)return 3;

    visionCircleCount=1;fogReveal=0.f;
    const uint occludedX=1u<<2;
    for(int route=0;route<=3;++route){
        const VoxelFaceSelect actual=selectVoxelFace(0,false,0u,occludedX,
            position,route,0.f,receive,quaternion);
        const bool cut=route<=2;
        if(actual.keepFace!=cut || actual.isCutFace!=cut || actual.fogActive!=cut ||
           actual.faceId!=0 || actual.riserFlip || actual.bothPolaritiesExposed){
            std::fprintf(stderr,"fog cut route %d\n",route);return 4;
        }
    }
    const VoxelFaceSelect top=selectVoxelFace(4,false,0u,1u<<6,
        position,1,0.f,receive,quaternion);
    if(top.keepFace||top.isCutFace)return 5;
    std::printf("face cases=%d cardinal flips=%d duals=%d\n",
        cases,cardinalFlips,cardinalDuals);
}
"""


@unittest.skipUnless(COMPILER, "voxel face selection requires a C++ compiler")
class VoxelFaceSelectionTest(unittest.TestCase):
    def test_route_guard_and_exposure_matrix(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            for remove_route_guard in (False, True):
                with self.subTest(backend=suffix, mutation=remove_route_guard):
                    with tempfile.TemporaryDirectory() as temporary:
                        path = Path(temporary)
                        source = path / "selector.cpp"
                        executable = path / "selector"
                        source.write_text(PREAMBLE + selection_source(
                            suffix, directory, remove_route_guard) + CHECKS)
                        build = subprocess.run(
                            [COMPILER, "-std=c++17", str(source), "-o", str(executable)],
                            capture_output=True, text=True)
                        self.assertEqual(build.returncode, 0, build.stderr)
                        result = subprocess.run([str(executable)], capture_output=True,
                                                text=True)
                        if remove_route_guard:
                            self.assertNotEqual(result.returncode, 0)
                            self.assertIn("route leak", result.stderr)
                        else:
                            self.assertEqual(result.returncode, 0, result.stderr)
                            self.assertIn("face cases=9216", result.stdout)


if __name__ == "__main__":
    unittest.main()

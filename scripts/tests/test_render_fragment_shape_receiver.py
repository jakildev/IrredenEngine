"""Execute the fragment receiver diagnostic with checked query and shadow adapters."""

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
struct vec2 {float x,y; vec2(float a,float b):x(a),y(b){} };
struct ivec2 {int x,y; explicit ivec2(vec2 v):x(int(v.x)),y(int(v.y)){}
 explicit ivec2(int v):x(v),y(v){} ivec2(int a,int b):x(a),y(b){} };
ivec2 operator-(vec2 v,int n){return {int(v.x)-n,int(v.y)-n};}
ivec2 clamp(ivec2 v,ivec2 lo,ivec2 hi){return {
 v.x<lo.x?lo.x:(v.x>hi.x?hi.x:v.x), v.y<lo.y?lo.y:(v.y>hi.y?hi.y:v.y)};}
vec2 floor(vec2 v){return {std::floor(v.x),std::floor(v.y)};}
struct vec3 {float x,y,z; explicit vec3(float v):x(v),y(v),z(v){}
 vec3(float a,float b,float c):x(a),y(b),z(c){} };
struct Color {vec3 rgb{.2f,.3f,.4f};float a=.7f;};
Color color;
vec2 originRaw{1.25f,2.75f},displayOrigin=originRaw,textureSize{8,6};
ivec2 sampleCoord{floor(displayOrigin)}, expectedOwner{1,2};
int shadowsEnabled=1,queryCalls=0,shadowCalls=0;
bool hit=true;
bool selectedShapeBoxReceiver(ivec2 owner,int width,vec2 query,vec3& p,vec3& n){
 ++queryCalls;
 if(owner.x!=expectedOwner.x||owner.y!=expectedOwner.y||width!=8||query.x!=originRaw.x||query.y!=originRaw.y)std::exit(11);
 p={query.x,query.y,1};n={0,0,-1};return hit;
}
float pos3DtoDistance(vec3 p){return p.x+p.y+p.z;}
float worldSunShadowFactor(vec3 p,vec3 n,float depth){
 ++shadowCalls;
 if(p.x!=originRaw.x||p.y!=originRaw.y||p.z!=1||n.z!=-1||depth!=originRaw.x+originRaw.y+1)std::exit(12);
 return .5f;
}
"""
CASES = r"""
int main(){
 run();if(color.rgb.x!=1||color.rgb.y!=0||color.rgb.z!=1||color.a!=.7f)return 1;
 if(queryCalls!=1||shadowCalls!=1)return 2;
 color=Color{};hit=false;run();
 if(color.rgb.x!=.2f||color.rgb.y!=.3f||color.rgb.z!=.4f||color.a!=.7f)return 3;
 if(shadowCalls!=1)return 4;
 hit=true;shadowsEnabled=0;run();
 if(color.rgb.x!=0||color.rgb.y!=0||color.rgb.z!=0||color.a!=.7f)return 5;
 if(shadowCalls!=1)return 6;
 color.a=0;int calls=queryCalls;run();if(calls!=queryCalls)return 7;
 color=Color{};shadowsEnabled=1;originRaw={-.25f,2.75f};displayOrigin=originRaw;
 expectedOwner={0,2};sampleCoord=expectedOwner;run();
 originRaw={8.f,6.f};displayOrigin=originRaw;expectedOwner={7,5};sampleCoord=expectedOwner;run();
 return 0;
}
"""


@unittest.skipUnless(COMPILER, "fragment receiver controls require a C++ compiler")
class FragmentShapeReceiverTest(unittest.TestCase):
    def test_borrowed_bindings_restore_canvas_resources(self):
        path = ROOT / "engine/prefabs/irreden/render/shape_receiver_bindings.hpp"
        source = "\n".join(line for line in path.read_text().splitlines()
                           if not line.startswith("#"))
        harness = r"""
#include <cstdlib>
#include <cstddef>
#include <utility>
#define IR_ASSERT(condition, message) do {if(!(condition))std::exit(42);}while(false)
namespace IRMath {
struct ivec2 {int x,y;bool operator==(ivec2 b)const{return x==b.x&&y==b.y;}};
}
namespace IRRender {
enum class BufferTarget {UNIFORM,SHADER_STORAGE};
constexpr int kBufferIndex_ShapesFrameData=23,kBufferIndex_ShapeDescriptors=20,
 kBufferIndex_ShapeSampleOwners=22,kBufferIndex_ShapeTileDescriptors=30,kBufferIndex_AnimationParams=22;
struct GPUShapesFrameData {int stamp;};
int uniforms[32]{},storage[32]{};int uploaded=0;
struct Buffer {
 int id;
 void subData(int offset,std::size_t bytes,const void* data){
  if(offset!=0||bytes!=sizeof(GPUShapesFrameData))std::exit(11);
  uploaded=static_cast<const GPUShapesFrameData*>(data)->stamp;
 }
 void bindBase(BufferTarget t,int slot){(t==BufferTarget::UNIFORM?uniforms:storage)[slot]=id;}
};
}
namespace IRComponents {
struct CanvasShapeGeometry {
 bool valid=true;IRMath::ivec2 ownerSize_{7,9};IRRender::GPUShapesFrameData frameData_{73};
 std::pair<int,IRRender::Buffer*> descriptors_,sampleOwners_,tiles_;
 bool samplesValid()const{return valid;}
};
}
"""
        checks = r"""
int main(int argc,char**){
 using namespace IRRender;
 Buffer frame{1},descriptor{2},owners{3},tiles{4},fallback{5},producer{6},animation{7};
 IRComponents::CanvasShapeGeometry geometry;
 geometry.descriptors_={0,&descriptor};geometry.sampleOwners_={0,&owners};geometry.tiles_={0,&tiles};
 if(argc==2)geometry.valid=false;
 if(argc==3)geometry.ownerSize_={9,7};
 IRPrefab::detail::bindShapeReceiver(geometry,{7,9},frame);
 if(uploaded!=73||uniforms[23]!=1||storage[20]!=2||storage[22]!=3||storage[30]!=4)return 1;
 IRPrefab::detail::restoreShapeReceiver(fallback,&producer,&animation);
 if(uniforms[23]!=6||storage[20]!=5||storage[22]!=7||storage[30]!=5)return 2;
 IRPrefab::detail::bindShapeReceiver(geometry,{7,9},frame);
 IRPrefab::detail::restoreShapeReceiver(fallback,nullptr,nullptr);
 if(uniforms[23]!=1||storage[20]!=5||storage[22]!=5||storage[30]!=5)return 3;
 return 0;
}
"""
        variants = {
            "production": source,
            "lost_restore": source.replace(
                "fallback.bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_ShapeDescriptors);",
                ""),
            "lost_gate": source.replace("geometry.samplesValid() &&", "true &&"),
        }
        for name, body in variants.items():
            with self.subTest(variant=name), tempfile.TemporaryDirectory() as tmp:
                if name != "production":
                    self.assertNotEqual(body, source)
                cpp, exe = Path(tmp) / "bindings.cpp", Path(tmp) / "bindings"
                cpp.write_text(harness + body + checks)
                build = subprocess.run([COMPILER, "-std=c++17", str(cpp), "-o", str(exe)],
                                       capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stderr)
                codes = [subprocess.run([str(exe), *args]).returncode
                         for args in ([], ["invalid"], ["wrong", "extent"])]
                if name == "production":
                    self.assertEqual(codes, [0, 42, 42])
                else:
                    self.assertNotEqual(codes, [0, 42, 42])

    def test_actual_fragment_receiver_block(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            path = ROOT / "engine/render/src/shaders" / folder
            source = (path / f"ir_trixel_to_framebuffer_body.{suffix}").read_text()
            start = source.index("    if (color.a >= 0.1)")
            block = source[start:source.index("#endif", start)]
            block = block.replace(
                "receiverFrame, receiverShapes, receiverOwners, receiverTiles,", "")
            block = block.replace(", sunFrameData, sunDepthBuf", "")
            block = block.replace("sunFrameData.shadowsEnabled", "shadowsEnabled")
            for a, b in (("float3", "vec3"), ("float2", "vec2"), ("int2", "ivec2")):
                block = block.replace(a, b)
            variants = {
                "production": block,
                "snapped_query": block.replace("originRaw,", "floor(originRaw),"),
                "wrong_owner": block.replace("floor(displayOrigin)", "vec2(0,0)")
                if suffix == "glsl" else block.replace("ivec2(sampleCoord)", "ivec2(vec2(0,0))"),
                "lost_miss_fallback": block.replace(
                    "if (selectedShapeBoxReceiver", "if (true || selectedShapeBoxReceiver"),
                "shadow_toggle_ignored": block.replace("shadowsEnabled == 0", "false"),
                "transparent_query": block.replace("color.a >= 0.1", "true"),
            }
            for name, body in variants.items():
                with (
                    self.subTest(backend=suffix, variant=name),
                    tempfile.TemporaryDirectory() as tmp,
                ):
                    if name != "production":
                        self.assertNotEqual(body, block)
                    cpp, exe = Path(tmp) / "probe.cpp", Path(tmp) / "probe"
                    cpp.write_text(HARNESS + "void run(){" + body + "}\n" + CASES)
                    build = subprocess.run([COMPILER, "-std=c++17", str(cpp), "-o", str(exe)],
                                           capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    result = subprocess.run([str(exe)], capture_output=True, text=True)
                    if name == "production":
                        self.assertEqual(result.returncode, 0, result.stderr)
                    else:
                        self.assertIn(result.returncode, (1, 2, 3, 4, 5, 6, 7, 11, 12))


if __name__ == "__main__":
    unittest.main()

"""Execute legacy bake routing, frame restoration, and shader depth controls."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_block, extract_function

ROOT = Path(__file__).resolve().parents[2]
PREFABS = ROOT / "engine/prefabs/irreden/render"
SHADERS = ROOT / "engine/render/src/shaders"
COMPILER = shutil.which("c++")


def uncomment(source):
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)



CPU = r"""
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <utility>
#include <vector>
struct ivec2 {int x=0,y=0;};
struct vec4 {vec4()=default;vec4(ivec2,float){};};
enum class BufferTarget {UNIFORM,SHADER_STORAGE};
enum class TextureAccess {READ_ONLY,WRITE_ONLY};
enum class TextureFormat {R32I};
enum class BarrierType {SHADER_STORAGE,ALL};
constexpr int kBufferIndex_FrameDataVoxelToCanvas=7,kBufferIndex_FrameDataSun=29;
constexpr int kBufferIndex_SunShadowDepthMap=28,kBufferIndex_PerAxisResolveScratch=16;
constexpr int kBufferIndex_RevoxelizeDetachedParams=22,kBakeSunShadowGroupSize=16;
#define IR_PROFILE_FUNCTION(x)
struct FrameDataVoxelToCanvas {
 int perAxisRoute_=97;
 ivec2 canvasSizePixels_,trixelCanvasOffsetZ1_,voxelRenderOptions_;
 float visualYaw_=0,rasterYaw_=0,residualYaw_=0;
};
struct FrameDataSun {int shadowsEnabled_=1;float sunSplatMaxTexels_=6;};
struct Buffer {
 unsigned char bytes[256]{};
 void subData(std::size_t offset,std::size_t count,const void* source){
  if(offset+count>sizeof(bytes))std::exit(90);
  std::memcpy(bytes+offset,source,count);
 }
 void bindBase(BufferTarget,int){}
 template<class T>T read()const {T value;std::memcpy(&value,bytes,sizeof(T));return value;}
};
int image=0,program=0,builds=0;
float cameraYaw=0;
struct Texture2D {
 int id;
 void bindAsImage(int slot,TextureAccess,TextureFormat){if(slot==0)image=id;}
};
struct ShaderProgram {int id;void use(){program=id;}};
struct C_TriangleCanvasTextures {
 ivec2 size_{32,48};Texture2D texture{10};int renderedSubdivisions_=2;
 Texture2D* getTextureDistances()const{return const_cast<Texture2D*>(&texture);}
};
struct C_CanvasSunShadow {};
struct C_TrixelCanvasRenderBehavior {bool useCameraPositionIso_=true;};
struct C_VoxelPool {int getLiveVoxelCount()const{return 0;}};
struct C_CanvasLocalRotation {};
struct C_PerAxisTrixelCanvases {
 bool allocated=true;Texture2D texture{11};
 std::pair<int,Texture2D*> resolveDepth_{0,&texture};
 bool isAllocated()const{return allocated;}
};
struct WorldPlacedCaster {ivec2 worldCellOffset_;C_TriangleCanvasTextures* textures_;};
struct DetachedShadowFrame {vec4 offset,rotation;};
struct Dispatch {int image,route;float residual,radius;int x,y;};
Buffer voxel,sun,other;
std::vector<Dispatch> bakes;
struct Device {
 void dispatchCompute(int x,int y,int){
  if(program==1){
   auto frame=voxel.read<FrameDataVoxelToCanvas>();auto light=sun.read<FrameDataSun>();
   bakes.push_back({image,frame.perAxisRoute_,frame.residualYaw_,light.sunSplatMaxTexels_,x,y});
  }
 }
 void memoryBarrier(BarrierType){}
};
namespace IREntity {using EntityId=int;}
namespace IRMath {
using ::ivec2;int max(int a,int b){return a>b?a:b;}int divCeil(int a,int b){return (a+b-1)/b;}
}
namespace IRRender {
using ::Buffer;using ::FrameDataVoxelToCanvas;
Device* device(){static Device value;return &value;}
}
namespace IRPrefab::Camera {
float getYaw(){return cameraYaw;}
std::pair<float,float> computeYawSplit(float yaw){return {0,yaw};}
vec4 getRotationQuat(){return {};}
}
void restoreVoxelCompactionSlots(Buffer*&,Buffer*&){}
"""

ADAPTER = r"""
struct Adapter {
 Buffer *voxelFrameDataBuf_=&voxel,*sunShadowFrameDataBuf_=&sun,*sunShadowDepthMap_=&other;
 Buffer *detachedShadowFrameBuf_=&other,*revoxelizeParamsBuf_=&other,*worldPlacedScratch_=&other;
 ShaderProgram bake{1},scatter{2},blit{3};
 ShaderProgram *bakeProgram_=&bake,*worldPlacedScatterProgram_=&scatter;
 ShaderProgram *worldPlacedBlitProgram_=&blit;
 Texture2D resolved{12};std::pair<int,Texture2D*> worldPlacedResolveDepth_{0,&resolved};
 FrameDataSun frameData_;FrameDataVoxelToCanvas voxelFrameScratch_;
 bool frameUsesFiniteCoverage_=false;int perAxisCanvasEntity_=1;
 C_PerAxisTrixelCanvases* perAxisCanvases_=nullptr;
 const C_TriangleCanvasTextures* mainTextures_=nullptr;
 const C_VoxelPool* mainPool_=nullptr;const C_CanvasLocalRotation* mainRotation_=nullptr;
 std::vector<WorldPlacedCaster> worldPlacedCasters_;
 void clearDepthMap(){}
 void ensureWorldPlacedResolve(ivec2){}
"""

CPU_CASES = r"""
int main(){
 C_TriangleCanvasTextures canvas;C_VoxelPool pool;C_CanvasLocalRotation rotation;
 FrameDataVoxelToCanvas frameData_;Buffer *frameDataBuf_=&voxel;
 const ivec2 mainOffsetZ1{16,24},mainCanvasSize{32,48};const int uncappedSub=4;
 for(int poison:{1,2,3,4096}){
  frameData_.perAxisRoute_=poison;
  STORE_RESET
  if(voxel.read<FrameDataVoxelToCanvas>().perAxisRoute_!=0)return 1;
  frameData_.perAxisRoute_=poison;bool perAxisSplit=true;
  COMPACT_RESET
  if(voxel.read<FrameDataVoxelToCanvas>().perAxisRoute_!=0)return 2;
  FrameDataVoxelToCanvas scratch;scratch.perAxisRoute_=poison;
  restoreMainCanvasVoxelFrame(scratch,&voxel,&canvas,&pool,&rotation);
  if(voxel.read<FrameDataVoxelToCanvas>().perAxisRoute_!=0)return 3;
  Buffer *compacted=&other,*indirect=&other;
  {LightingRouteScope scope(&voxel,compacted,indirect,{64,64},mainCanvasSize);
   if(voxel.read<FrameDataVoxelToCanvas>().perAxisRoute_!=1)return 4;}
  const auto restored=voxel.read<FrameDataVoxelToCanvas>();
  if(restored.perAxisRoute_!=0||restored.canvasSizePixels_.x!=32)return 5;
 }
 for(float yaw:{0.f,.35f})for(bool axes:{false,true})for(bool detached:{false,true})
 for(bool finite:{false,true})for(bool shadows:{false,true})for(bool world:{false,true})
 for(bool main:{false,true})for(float radius:{0.f,6.f}){
  cameraYaw=yaw;Adapter adapter;C_PerAxisTrixelCanvases perAxis;
  adapter.perAxisCanvases_=axes?&perAxis:nullptr;
  adapter.mainTextures_=&canvas;adapter.mainPool_=&pool;adapter.mainRotation_=&rotation;
  if(detached)adapter.worldPlacedCasters_.push_back({{},&canvas});
  adapter.frameUsesFiniteCoverage_=finite;adapter.frameData_.shadowsEnabled_=shadows;
  adapter.frameData_.sunSplatMaxTexels_=radius;
  FrameDataVoxelToCanvas scratch;
  restoreMainCanvasVoxelFrame(scratch,&voxel,&canvas,&pool,&rotation);
  bakes.clear();builds=0;
  adapter.tick(main?1:2,canvas,{},C_TrixelCanvasRenderBehavior{world});
  const bool active=world&&shadows&&!finite;
  const int count=active?(1+int(main&&axes)+int(main&&detached)):0;
  if(int(bakes.size())!=count)return 10;
  int index=0;
  if(active){
   auto d=bakes[index++];
   if(d.image!=10||d.residual!=yaw||d.radius!=radius)return 11;
   if(main&&axes){d=bakes[index++];if(d.image!=11||d.residual!=0||d.radius!=0)return 12;}
   if(main&&detached){d=bakes[index++];if(d.image!=12||d.residual!=0||d.radius!=radius)return 13;}
  }
  for(auto d:bakes)if(d.route!=0||d.x!=2||d.y!=3)return 14;
  if(builds!=int(active&&main&&detached))return 15;
  const auto restored=voxel.read<FrameDataVoxelToCanvas>();
  if(restored.residualYaw_!=yaw||restored.visualYaw_!=yaw)return 16;
  if(world&&sun.read<FrameDataSun>().sunSplatMaxTexels_!=radius)return 17;
  if(active&&image!=10)return 18;
 }
}
"""


def cpu_source():
    driver = uncomment((PREFABS / "systems/system_bake_sun_shadow_map.hpp").read_text())
    frames = uncomment((PREFABS / "voxel_frame_data.hpp").read_text())
    producer = uncomment((PREFABS / "systems/system_voxel_to_trixel.hpp").read_text())
    per_axis = uncomment((PREFABS / "per_axis_canvas.hpp").read_text())
    reset = re.search(r"frameData\.perAxisRoute_\s*=\s*0;", frames)[0]
    build = ("void buildVoxelFrameData(FrameDataVoxelToCanvas& frameData,"
             "const C_TriangleCanvasTextures&,int,const C_CanvasLocalRotation&){"
             + reset + "frameData.visualYaw_=cameraYaw;"
             "frameData.residualYaw_=cameraYaw;++builds;}\n")
    store = extract_function(producer, "dispatchPerAxisCanvases")
    store = store[store.rindex("frameData_.perAxisRoute_ = 0;"):store.rindex("}")]
    compact = re.search(r"if \(perAxisSplit\) \{\s*frameData_\.perAxisRoute_ = 0;.*?\n\s*\}",
                        producer, re.S)[0]
    methods = "\n".join(extract_function(driver, name) for name in
                        ("patchFrameYawSplit", "patchSunSplatRadius", "tick"))
    guards = "\n".join(extract_block(driver, "struct " + name) + ";" for name in
                       ("FrameYawRestoreGuard", "SunSplatRestoreGuard"))
    guards = guards.replace("System<BAKE_SUN_SHADOW_MAP>", "Adapter")
    cases = CPU_CASES.replace("STORE_RESET", store).replace("COMPACT_RESET", compact)
    return (CPU + build + extract_function(frames, "restoreMainCanvasVoxelFrame")
            + extract_block(per_axis, "class LightingRouteScope") + ";\n"
            + ADAPTER + guards + methods + "};\n" + cases)


SHADER = r"""
#include <cmath>
#include <cstdlib>
#include <vector>
struct ivec2 {int x,y;};
struct vec2 {float x,y;};
struct vec3 {float x=0,y=0,z=0;};
ivec2 inputPixel{3,5},inputSize{32,48},trixelCanvasOffsetZ1{16,24},voxelRenderOptions{1,4};
vec2 frameCanvasOffset{.25f,-.75f};
int inputEncoded=0;
float residualYaw=0,visualYaw=.35f,rasterYaw=1.5707964f,sunSplatMaxTexels=0;
vec3 sunBasisU{},sunBasisV{},sunDirection{};
vec2 cascadeOriginUV_0{1,2},cascadeOriginUV_1{3,4};
vec2 cascadeTexelSize_0{.5f,.25f},cascadeTexelSize_1{2,4};
constexpr int kCascadeTexelCount=4096;
struct Call {vec3 position;vec2 origin,texel;int offset,radius;};
std::vector<Call> calls;
void checkInput(ivec2 p,ivec2 z,vec2 offset,ivec2 options){
 if(p.x!=inputPixel.x||p.y!=inputPixel.y||z.x!=16||z.y!=24||
    offset.x!=.25f||offset.y!=-.75f||options.x!=1||options.y!=4)std::exit(25);
}
vec3 trixelCanvasPixelToWorld3D(ivec2 p,int depth,ivec2 z,vec2 offset,ivec2 options,float yaw){
 checkInput(p,z,offset,options);return {float(depth),yaw,0};
}
vec3 trixelCanvasPixelToWorld3DSmoothYaw(
 ivec2 p,int depth,ivec2 z,vec2 offset,ivec2 options,float yaw){
 checkInput(p,z,offset,options);return {float(depth),yaw,1};
}
vec3 sunSpaceProject(vec3 p,vec3,vec3,vec3){return p;}
void bakeCascadeBox(vec3 p,vec2 origin,vec2 texel,int offset,int radius){
 calls.push_back({p,origin,texel,offset,radius});
}
"""

SHADER_CASES = r"""
int main(){
 for(int encoded:{-262144,-262143,-9,-8,-7,-1,0,1,7,8,65528,65534,65535,65536,2147483647})
 for(float residual:{0.f,.35f,-.2f})for(float radius:{0.f,1.f,6.f})
 for(ivec2 pixel:{ivec2{3,5},ivec2{31,47},ivec2{32,5},ivec2{3,48}}){
  inputEncoded=encoded;inputPixel=pixel;residualYaw=residual;sunSplatMaxTexels=radius;
  calls.clear();run();
  const bool valid=encoded<65535&&pixel.x<32&&pixel.y<48;
  if(calls.size()!=(valid?2u:0u))return 20;
  if(!valid)continue;
  for(int i=0;i<2;++i){const auto c=calls[i];
   if(c.position.x!=float(std::floor(double(encoded)/8.)))return 21;
   if(c.position.y!=(residual!=0?visualYaw:rasterYaw)||c.position.z!=float(residual!=0))return 22;
   if(c.radius!=(residual==0?int(radius):0))return 23;
   const vec2 origin=i==0?cascadeOriginUV_0:cascadeOriginUV_1;
   const vec2 texel=i==0?cascadeTexelSize_0:cascadeTexelSize_1;
   if(c.offset!=i*kCascadeTexelCount||c.origin.x!=origin.x||c.origin.y!=origin.y||
      c.texel.x!=texel.x||c.texel.y!=texel.y)return 24;
  }
 }
}
"""


def shader_source(suffix, folder):
    source = uncomment((SHADERS / folder / f"c_bake_sun_shadow_map.{suffix}").read_text())
    common = uncomment((SHADERS / folder / f"ir_iso_common.{suffix}").read_text())
    function = extract_block(source, "void main(" if suffix == "glsl" else "kernel void c_bake")
    body = function[function.index("{") + 1:-1]
    body = body.replace("frameData.", "").replace("sunFrameData.", "")
    body = re.sub(r"ivec2 pixel = ivec2\(gl_GlobalInvocationID.xy\);|"
                  r"int2 pixel = int2\(globalId.xy\);", "ivec2 pixel = inputPixel;", body)
    body = re.sub(r"ivec2 size = imageSize\(trixelDistances\);|"
                  r"int2 size = int2\(.*?\);", "ivec2 size = inputSize;", body, flags=re.S)
    body = body.replace("imageLoad(trixelDistances, pixel).x", "inputEncoded")
    body = body.replace("trixelDistances.read(uint2(pixel)).x", "inputEncoded")
    body = body.replace("sunDepthBuf,", "").replace(".xyz", "").replace("float3", "vec3")
    sentinel = re.search(r"(?:const|constant) int kEmptyDistanceEncoded = \d+;", source)[0]
    sentinel = sentinel.replace("constant", "const")
    return (SHADER + sentinel + extract_function(common, "decodeDepthSingle")
            + "\nvoid run(){\n" + body + "}\n" + SHADER_CASES)


@unittest.skipUnless(COMPILER, "legacy bake controls require a C++ compiler")
class LegacyShadowBakeTest(unittest.TestCase):
    def execute(self, source, expected=0):
        with tempfile.TemporaryDirectory() as tmp:
            cpp, exe = Path(tmp) / "bake.cpp", Path(tmp) / "bake"
            cpp.write_text(source)
            build = subprocess.run([COMPILER, "-std=c++17", str(cpp), "-o", str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, expected, run.stderr)

    def test_dispatches_and_route_restoration(self):
        source = cpu_source()
        self.execute(source)
        mutations = (
            ("builder_route", "frameData.perAxisRoute_ = 0;", "", 3),
            ("store_route", "frameData_.perAxisRoute_ = 0;", "", 1),
            ("scope_route", "const int kSingleCanvasRoute = 0;",
             "const int kSingleCanvasRoute = 1;", 5),
            ("per_axis_splat", "patchSunSplatRadius(0.0f);", "", 12),
            ("yaw_restore", "sys_.patchFrameYawSplit(visualYaw_, residualYaw_);", "", 16),
            ("splat_restore", "sys_.patchSunSplatRadius(radiusTexels_);", "", 17),
        )
        for name, before, after, expected in mutations:
            with self.subTest(mutation=name):
                self.assertIn(before, source)
                self.execute(source.replace(before, after, 1), expected)
        with self.subTest(mutation="compact_route"):
            before = "frameData_.perAxisRoute_ = 0;"
            left, right = source.rsplit(before, 1)
            self.execute(left + right, 2)
        with self.subTest(mutation="detached_frame_restore"):
            candidate, count = re.subn(
                r"restoreMainCanvasVoxelFrame\(\s*voxelFrameScratch_,.*?\);", "", source,
                flags=re.S)
            self.assertEqual(count, 1)
            self.execute(candidate, 15)
        with self.subTest(mutation="resolve_yaw"):
            before = "patchFrameYawSplit(cameraRasterYaw, 0.0f);"
            self.assertEqual(source.count(before), 2)
            self.execute(source.replace(before, "", 1), 12)

    def test_shader_single_depth_and_splat_controls(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal")):
            with self.subTest(backend=suffix):
                source = shader_source(suffix, folder)
                self.execute(source)
                mutations = (
                    ("sentinel", "encoded >= kEmptyDistanceEncoded",
                     "encoded > kEmptyDistanceEncoded", 20),
                    ("depth_encoding", "return encoded >> 3;", "return encoded >> 15;", 21),
                    ("smooth_recovery", "if (residualYaw != 0.0)", "if (false)", 22),
                    ("splat_on_smooth", "residualYaw == 0.0 &&", "true &&", 23),
                    ("lost_splat", "radius = int(sunSplatMaxTexels);", "radius = 0;", 23),
                    ("second_cascade", "kCascadeTexelCount, radius", "0, radius", 24),
                )
                for name, before, after, expected in mutations:
                    with self.subTest(mutation=name):
                        self.assertEqual(source.count(before), 1)
                        self.execute(source.replace(before, after), expected)


if __name__ == "__main__":
    unittest.main()

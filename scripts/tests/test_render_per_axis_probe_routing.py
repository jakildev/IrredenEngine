"""Execute CPU probe discovery and draw routing with checked resource adapters."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")

HARNESS = r"""
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>
enum class DebugOverlayMode {NONE, SURFACE_SHADOW, OTHER};
enum class BufferTarget {UNIFORM, SHADER_STORAGE};
constexpr int COMPUTE_SUN_SHADOW=1,SHAPES_TO_TRIXEL=2,kNullSystemId=-1;
constexpr int kBufferIndex_FrameDataSun=29,kBufferIndex_SunShadowDepthMap=28;
struct Scene {
 bool compute=false,shapes=false,shadow=false;
 int canvas=-1;DebugOverlayMode mode=DebugOverlayMode::NONE;
} scene;
int findSystem(int name){return (name==COMPUTE_SUN_SHADOW?scene.compute:scene.shapes)?name:-1;}
int uniforms[32]{},storage[32]{},binds=0,used=0,lookups=0;
struct Buffer {
 int id;
 void bindBase(BufferTarget target,int slot){
  ++binds;(target==BufferTarget::UNIFORM?uniforms:storage)[slot]=id;
 }
};
struct ShaderProgram {
 int id;
 void use(){
  if(id==2&&(uniforms[29]!=29||storage[28]!=28))std::exit(20);
  used=id;
 }
};
struct C_CanvasSunShadow {};
namespace IREntity {
constexpr int kNullEntity=-1;
template<class T> std::optional<T*> getComponentOptional(int entity){
 if(entity!=scene.canvas||entity==kNullEntity)std::exit(21);
 static T component;
 return scene.shadow?std::optional<T*>{&component}:std::nullopt;
}
}
namespace IRRender {
DebugOverlayMode getDebugOverlay(){return scene.mode;}
template<class T> T* getNamedResource(const char* name){
 ++lookups;
 static Buffer frame{29},depth{28},shape{23};
 const std::string resource{name};
 if(resource=="ComputeSunShadowFrameData"||resource=="SunShadowDepthMap"){
  if(!scene.compute||!scene.shadow||scene.canvas<0)std::exit(22);
  return resource=="ComputeSunShadowFrameData"?&frame:&depth;
 }
 if(!scene.shapes)std::exit(23);
 return &shape;
}
}
struct Axes {bool allocated;bool isAllocated()const{return allocated;}};
struct Canvas {int size_=0;};
struct Adapter {
 Buffer *sunFrameBuf_=nullptr,*sunDepthBuf_=nullptr,*shapeProbeFrameBuf_=nullptr;
 Buffer *shapeProbeFallbackBuf_=nullptr,*shapeProducerFrameBuf_=nullptr;
 Buffer *animationParamsBuf_=nullptr;
 bool scatterProbeEnabled_=false,shapeProbeEnabled_=false,shapeLightingEnabled_=false;
 int perAxisCanvasEntity_=-1;
 Axes *perAxisCanvases_=nullptr;
 ShaderProgram beauty{1},probe{2};
 ShaderProgram *scatterProgram_=&beauty,*scatterProbeProgram_=&probe;
"""

CASES = r"""
};
int main(){
 int cases=0;
 for(bool compute:{false,true})for(bool shapes:{false,true})
 for(bool canvas:{false,true})for(bool shadow:{false,true})
 for(auto mode:{DebugOverlayMode::NONE,DebugOverlayMode::SURFACE_SHADOW,DebugOverlayMode::OTHER}){
  scene={compute,shapes,shadow,canvas?7:-1,mode};
  Adapter adapter;adapter.perAxisCanvasEntity_=scene.canvas;
  lookups=0;adapter.resolve();
  const bool expected=compute&&canvas&&shadow&&mode==DebugOverlayMode::SURFACE_SHADOW;
  if(adapter.scatterProbeEnabled_!=expected||
     adapter.shapeProbeEnabled_!=(expected&&shapes))return 1;
  if(lookups!=(expected?(shapes?6:2):0))return 2;
  const int firstLookups=lookups;adapter.resolve();
  if(lookups!=firstLookups)return 3;
  for(bool allocated:{false,true})for(bool main:{false,true})for(bool present:{false,true}){
   Axes axes{allocated};adapter.perAxisCanvases_=present?&axes:nullptr;
   uniforms[29]=-29;storage[28]=-28;uniforms[23]=123;storage[25]=125;
   used=binds=0;adapter.draw(main&&canvas?scene.canvas:9);
   const bool drawn=canvas&&main&&present&&allocated;
   if(used!=(drawn?(expected?2:1):0)||binds!=(drawn&&expected?2:0))return 4;
   if(uniforms[23]!=123||storage[25]!=125)return 5;
   if(!(drawn&&expected)&&(uniforms[29]!=-29||storage[28]!=-28))return 6;
  }
  scene.mode=DebugOverlayMode::NONE;adapter.resolve();
  if(adapter.scatterProbeEnabled_||adapter.shapeProbeEnabled_||lookups!=firstLookups)return 7;
  ++cases;
 }
 return cases==48?0:8;
}
"""


def adapter_source():
    source = (ROOT / "engine/prefabs/irreden/render/systems/"
              "system_trixel_to_framebuffer.hpp").read_text()
    start = source.index("        const bool sunReceiverAvailable =")
    gates = source[start:source.index("        lighting_ = nullptr;", start)]
    start = source.index("        if ((shapeProbeEnabled_ || shapeLightingEnabled_)", start)
    resources = source[start:source.index("        program_->use();", start)]
    start = source.index("    void bindSunShadowResources()")
    binding = source[start:source.index("\n    }", start) + len("\n    }")]
    start = source.index("        if (scatterProbeEnabled_) {")
    selection = source[start:source.index("        IRRender::device()->setPolygonMode", start)]
    route = re.search(
        r"        if \(entity == perAxisCanvasEntity_ &&.*?drawPerAxisScatter\(.*?\);",
        source, re.DOTALL)
    if route is None:
        raise ValueError("missing main-canvas per-axis draw gate")
    return (HARNESS + "void resolve(){\n" + gates + resources + "}\n" + binding
            + "\nvoid drawPerAxisScatter(int,Axes&,int,int){\n" + selection + "}\n"
            + "void draw(int entity){int frameData=0,framebufferResolution=0;"
            + "Canvas triangleCanvasTextures;\n" + route[0] + "\n}}\n" + CASES)


@unittest.skipUnless(COMPILER, "probe routing controls require a C++ compiler")
class PerAxisProbeRoutingTest(unittest.TestCase):
    def test_discovery_binding_and_draw_gates(self):
        source = adapter_source()
        variants = {
            "production": source,
            "requires_shapes": source.replace(
                "scatterProbeEnabled_ = sunReceiverAvailable",
                "scatterProbeEnabled_ = shapeReceiverAvailable"),
            "lost_sun_gate": source.replace(
                "scatterProbeEnabled_ = sunReceiverAvailable &&", "scatterProbeEnabled_ ="),
            "lost_overlay_gate": source.replace(
                "IRRender::getDebugOverlay() == DebugOverlayMode::SURFACE_SHADOW", "true"),
            "lost_binding": source.replace("            bindSunShadowResources();", ""),
            "wrong_depth_slot": source.replace(
                "BufferTarget::SHADER_STORAGE, kBufferIndex_SunShadowDepthMap",
                "BufferTarget::SHADER_STORAGE, 27"),
            "wrong_program": source.replace(
                "scatterProbeEnabled_ ? scatterProbeProgram_ : scatterProgram_", "scatterProgram_"),
            "lost_cache": source.replace("sunFrameBuf_ == nullptr", "true"),
            "lost_cardinal_gate": source.replace("perAxisCanvases_->isAllocated()", "true"),
            "foreign_canvas": source.replace("entity == perAxisCanvasEntity_ &&", ""),
        }
        for name, body in variants.items():
            with self.subTest(variant=name), tempfile.TemporaryDirectory() as tmp:
                if name != "production":
                    self.assertNotEqual(body, source)
                cpp, executable = Path(tmp) / "routing.cpp", Path(tmp) / "routing"
                cpp.write_text(body)
                build = subprocess.run(
                    [COMPILER, "-std=c++17", str(cpp), "-o", str(executable)],
                    capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stderr)
                run = subprocess.run([str(executable)], capture_output=True, text=True)
                if name == "production":
                    self.assertEqual(run.returncode, 0, run.stderr)
                else:
                    self.assertIn(run.returncode, (1, 2, 3, 4, 5, 6, 7, 20, 21, 22, 23))


if __name__ == "__main__":
    unittest.main()

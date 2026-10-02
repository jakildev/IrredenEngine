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
int uniforms[32]{},storage[32]{},binds=0,used=0,lookups=0,surfaceBinds=0;
int scatterCanvasSize=-1;
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
struct Canvas {int size_=2568;};
struct Adapter {
 Buffer *sunFrameBuf_=nullptr,*sunDepthBuf_=nullptr,*shapeProbeFrameBuf_=nullptr;
 Buffer *shapeProbeFallbackBuf_=nullptr,*shapeProducerFrameBuf_=nullptr;
 Buffer *animationParamsBuf_=nullptr;
 bool scatterProbeEnabled_=false,shapeProbeEnabled_=false,shapeLightingEnabled_=false;
 bool scatterLightingEnabled_=false,visibilityPrepass=false;
 int perAxisCanvasEntity_=-1;
 Axes *perAxisCanvases_=nullptr;
 ShaderProgram beauty{1},probe{2},surfaceLighting{3},visibleLighting{4};
 ShaderProgram *scatterProgram_=&beauty,*scatterProbeProgram_=&probe;
 ShaderProgram *scatterLightingProgram_=&surfaceLighting;
 ShaderProgram *visibleLightingProgram_=&visibleLighting;
 void bindSurfaceLightingResources(){++surfaceBinds;}
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
   scatterCanvasSize=-1;
   used=binds=surfaceBinds=0;adapter.draw(main&&canvas?scene.canvas:9);
   const bool drawn=canvas&&main&&present&&allocated;
   if(used!=(drawn?(expected?2:1):0)||binds!=(drawn&&expected?2:0)||surfaceBinds)return 4;
   if(scatterCanvasSize!=(drawn?642:-1))return 24;
   if(uniforms[23]!=123||storage[25]!=125)return 5;
   if(!(drawn&&expected)&&(uniforms[29]!=-29||storage[28]!=-28))return 6;
  }
  scene.mode=DebugOverlayMode::NONE;adapter.resolve();
  if(adapter.scatterProbeEnabled_||adapter.shapeProbeEnabled_||lookups!=firstLookups)return 7;
  ++cases;
 }
 for(bool probe:{false,true})for(bool lighting:{false,true})for(bool visible:{false,true}){
  Adapter adapter;Buffer frame{29},depth{28};Axes axes{true};
  adapter.sunFrameBuf_=&frame;adapter.sunDepthBuf_=&depth;
  adapter.scatterProbeEnabled_=probe;adapter.scatterLightingEnabled_=lighting;
  adapter.visibilityPrepass=visible&&lighting;
  used=binds=surfaceBinds=0;adapter.drawPerAxisScatter(0,axes,0,0);
  const int expected=visible&&lighting?4:lighting?3:probe?2:1;
  if(used!=expected||surfaceBinds!=int(lighting)||binds!=((probe||lighting)?2:0))return 4;
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
    start = re.search(
        r"if\s*\(scatterProbeEnabled_\s*\|\|\s*scatterLightingEnabled_\)", source).start()
    selection_end = re.search(r"if\s*\(visibilityPrepass\)", source[start:]).start()
    selection = source[start:start + selection_end]
    route = re.search(
        r"        if \(entity == perAxisCanvasEntity_ &&.*?drawPerAxisScatter\(.*?\);",
        source, re.DOTALL)
    if route is None:
        raise ValueError("missing main-canvas per-axis draw gate")
    return (HARNESS + "void resolve(){\n" + gates + resources + "}\n" + binding
            + "\nvoid drawPerAxisScatter(int,Axes&,int mainCanvasSize,int){\n"
            + "scatterCanvasSize=mainCanvasSize;\n" + selection + "}\n"
            + "void draw(int entity){int frameData=0,framebufferResolution=0;"
            + "const int logicalCanvasSize=642;"
            + "Canvas triangleCanvasTextures;\n" + route[0] + "\n}}\n" + CASES)


@unittest.skipUnless(COMPILER, "probe routing controls require a C++ compiler")
class PerAxisProbeRoutingTest(unittest.TestCase):
    def test_discovery_binding_and_draw_gates(self):
        source = adapter_source()
        variants = {
            "production": None,
            "requires_shapes": (
                r"(\bscatterProbeEnabled_\s*=\s*)sunReceiverAvailable\b",
                r"\g<1>shapeReceiverAvailable"),
            "lost_sun_gate": (
                r"(\bscatterProbeEnabled_\s*=\s*)sunReceiverAvailable\s*&&\s*", r"\g<1>"),
            "lost_overlay_gate": (
                r"(\bscatterProbeEnabled_\s*=\s*sunReceiverAvailable\s*&&\s*)"
                r"IRRender::getDebugOverlay\(\s*\)\s*==\s*DebugOverlayMode::SURFACE_SHADOW",
                r"\g<1>true"),
            "lost_binding": (r"\bbindSunShadowResources\(\s*\)\s*;", ""),
            "wrong_depth_slot": (
                r"(BufferTarget::SHADER_STORAGE\s*,\s*)kBufferIndex_SunShadowDepthMap\b",
                r"\g<1>27"),
            "wrong_program": (
                r"\bscatterProbeEnabled_\s*\?\s*scatterProbeProgram_\s*:\s*scatterProgram_\b",
                "scatterProgram_"),
            "lost_cache": (r"\bsunFrameBuf_\s*==\s*nullptr\b", "true"),
            "lost_cardinal_gate": (r"\bperAxisCanvases_\s*->\s*isAllocated\(\s*\)", "true"),
            "foreign_canvas": (r"\bentity\s*==\s*perAxisCanvasEntity_\s*&&\s*", ""),
            "backing_extent": (
                r"(\bframeData\s*,\s*\*perAxisCanvases_\s*,\s*)logicalCanvasSize\b",
                r"\g<1>triangleCanvasTextures.size_"),
        }
        for name, mutation in variants.items():
            with self.subTest(variant=name), tempfile.TemporaryDirectory() as tmp:
                body = source
                if mutation is not None:
                    body, count = re.subn(*mutation, source)
                    self.assertEqual(count, 1, f"{name}: expected exactly one mutation target")
                    _, inline_count = re.subn(*mutation, re.sub(r"\s+", " ", source))
                    self.assertEqual(
                        inline_count, 1, f"{name}: expected one inline mutation target")
                cpp, executable = Path(tmp) / "routing.cpp", Path(tmp) / "routing"
                cpp.write_text(body)
                build = subprocess.run(
                    [COMPILER, "-std=c++17", str(cpp), "-o", str(executable)],
                    capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stderr)
                run = subprocess.run([str(executable)], capture_output=True, text=True)
                if name == "production":
                    self.assertEqual(run.returncode, 0, run.stderr)
                elif name == "backing_extent":
                    self.assertEqual(run.returncode, 24, run.stderr)
                else:
                    self.assertIn(run.returncode, (1, 2, 3, 4, 5, 6, 7, 20, 21, 22, 23, 24))


if __name__ == "__main__":
    unittest.main()

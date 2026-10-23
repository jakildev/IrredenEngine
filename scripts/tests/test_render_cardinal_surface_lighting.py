"""Execute production cardinal lighting gates and the producer/consumer handoff.

Shader predicates run as scalar C++; geometry, resource calls, and draw execution
are spies. These controls do not certify native shader compilation or shading.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_block, extract_function, replace_once
from test_render_cardinal_voxel_receiver import COMPILER, ROOT, shader_directory

PREAMBLE = r"""
#include <cstdio>
#include <cstdlib>
#include <optional>
using uint=unsigned;
struct uvec2 {uint x,y;};
uvec2 id;
struct {int y;} normalOptions;
struct {float a;} color;
int lightingEnabled=1,scatterDebugMode=1,perAxisRoute=0;
bool sourceMode=false,glitchPresentationOverride=false;
float isDetachedCanvas=0,residualYaw=0;
void require(bool okay,const char* label){
 if(!okay){std::fprintf(stderr,"%s\n",label);std::exit(1);}
}
struct C_TrixelCanvasRenderBehavior {bool useCameraPositionIso_=true;} behavior;
struct C_PerAxisSurfaceLighting {bool enabled_=true;} surfaceLighting;
bool hasBehavior=true;
namespace IREntity {
template<class T> T* singletonOrNull(){return &surfaceLighting;}
template<class T> std::optional<T*> getComponentOptional(int){
 if(hasBehavior)return &behavior;return std::nullopt;
}}
enum {COMPUTE_SUN_SHADOW,TRIXEL_TO_FRAMEBUFFER,FOG_TO_TRIXEL,TRIXEL_GLITCH,kNullSystemId=-1};
bool systems[]={true,true,false,false};
int findSystem(int n){return systems[n]?n:kNullSystemId;}
enum class DebugOverlayMode {NONE,OTHER};
namespace IRRender {
DebugOverlayMode overlay=DebugOverlayMode::NONE;bool depthDebug=false;
DebugOverlayMode getDebugOverlay(){return overlay;}
bool getDepthColorDebugMode(){return depthDebug;}
}
struct Axis {bool allocated=false;bool isAllocated(){return allocated;}} axes;
int resource;
int *mainCanvasTextures_=&resource,*mainVoxelPool_=&resource,*mainCanvasAO_=&resource;
int *mainCanvasSunShadow_=&resource,*mainCanvasLightVolume_=&resource;
Axis* perAxisCanvases_=nullptr;
int perAxisCanvasEntity_=1;
struct Producer {
 bool cardinalSurfaceLightingEligible_=false,cardinalSurfaceLightingReady_=false;
 bool cardinalGlitchPresentationActive_=false,cardinalGlitchSourceReady_=false;
 bool perAxisSurfaceLightingEligible_=false;
 void begin(){RESET GATE}
 void tick(int entity){PUBLISH}
 void prepare(int entity){PREPARE}
} producer;
Producer* lighting_=&producer;
bool cardinalLightingEnabled_=false,cardinalGlitchOverrideEnabled_=false;
bool shapeProbeEnabled_=false,shapeLightingEnabled_=false;
int selected=0,shapeBindings=0,sunBindings=0;
struct Program {int id;void use(){selected=id;}} shapeProgram{2},probeProgram{3},cardinalProgram{1};
Program *shapeLightingProgram_=&shapeProgram,*shapeProbeProgram_=&probeProgram;
Program *cardinalLightingProgram_=&cardinalProgram;
struct Geometry {bool valid=false;bool samplesValid()const{return valid;}};
struct {Geometry shapeGeometry_;} triangleCanvasTextures;
void bindShapeProbe(decltype(triangleCanvasTextures)&){++shapeBindings;}
void bindSunShadowResources(){++sunBindings;}
void consume(){CONSUME}
void select(int entity){
 selected=shapeBindings=sunBindings=0;
 SELECT_FLAG
 SELECT_PROGRAM
}
"""

CHECKS = r"""
int main(){
 for(int bad=-1;bad<14;++bad){
  mainCanvasTextures_=mainVoxelPool_=mainCanvasAO_=mainCanvasSunShadow_=mainCanvasLightVolume_=&resource;
  perAxisCanvases_=nullptr;axes.allocated=true;
  systems[0]=systems[1]=true;systems[2]=systems[3]=false;
  IRRender::overlay=DebugOverlayMode::NONE;IRRender::depthDebug=false;
  hasBehavior=true;behavior.useCameraPositionIso_=true;
  if(bad==0)mainCanvasTextures_=nullptr;if(bad==1)mainVoxelPool_=nullptr;
  if(bad==2)mainCanvasAO_=nullptr;if(bad==3)mainCanvasSunShadow_=nullptr;
  if(bad==4)mainCanvasLightVolume_=nullptr;if(bad==5)perAxisCanvases_=&axes;
  if(bad==6)systems[0]=false;if(bad==7)systems[1]=false;
  if(bad==8)systems[2]=true;if(bad==9)systems[3]=true;
  if(bad==10)IRRender::overlay=DebugOverlayMode::OTHER;
  if(bad==11)IRRender::depthDebug=true;if(bad==12)behavior.useCameraPositionIso_=false;
  if(bad==13)hasBehavior=false;
  producer.cardinalSurfaceLightingEligible_=producer.cardinalSurfaceLightingReady_=true;
  producer.begin();consume();
  const bool eligible=bad<0||bad==9;
  require(producer.cardinalSurfaceLightingEligible_==eligible,"producer eligibility");
  require(!cardinalLightingEnabled_,"begin resets readiness before main tick");
  producer.tick(2);consume();require(!cardinalLightingEnabled_,"secondary tick cannot publish");
  producer.tick(1);consume();
  require(cardinalLightingEnabled_==eligible,"main tick publishes readiness");
 }
 lighting_=nullptr;consume();require(!cardinalLightingEnabled_,"missing producer is not ready");
 lighting_=&producer;
 for(bool ready:{false,true})for(int entity:{1,2})for(bool shapes:{false,true})
 for(bool valid:{false,true})for(bool probe:{false,true}){
  producer.cardinalSurfaceLightingEligible_=!ready;
  producer.cardinalSurfaceLightingReady_=ready;consume();
  producer.prepare(entity);
  require(normalOptions.y==int(entity==1&&!ready),"compute skip flag is main-canvas only");
  shapeLightingEnabled_=shapes;shapeProbeEnabled_=probe;
  triangleCanvasTextures.shapeGeometry_.valid=valid;select(entity);
  const bool main=entity==1,shape=main&&valid&&(shapes||probe);
  require(scatterDebugMode==int(main&&ready),"consumer flag follows ready main canvas");
  require(selected==(shape?(shapes?2:3):(main&&ready?1:0)),
          "mixed shape/cardinal program selection");
  require(shapeBindings==int(shape)&&sunBindings==int(!shape&&main&&ready),
          "selected resource route");
 }
 for(int enabled:{0,1})for(int deferred:{0,1})for(int axis:{0,1})
 for(float detached:{0.f,1.f})for(float yaw:{0.f,.001f})for(bool source:{false,true})
 for(uint tag:{0u,1u<<18u,1u<<19u})for(bool cut:{false,true}){
  lightingEnabled=enabled;normalOptions.y=deferred;perAxisRoute=axis;
  isDetachedCanvas=detached;residualYaw=yaw;sourceMode=source;
  id={17,tag|(cut?1u<<29u:0u)};
  const bool cardinal=(tag&(1u<<18u))!=0;
  require(skip()==bool(enabled&&deferred&&!axis&&!detached&&!yaw&&!source&&cardinal&&!cut),
          "compute preserves only eligible cardinal albedo");
  for(int flag:{0,1,2})for(float alpha:{0.f,1.f})for(bool override:{false,true}){
   scatterDebugMode=flag;color.a=alpha;
   glitchPresentationOverride=override;
   require(fragment()==bool(alpha&&enabled&&flag&&cardinal&&!cut&&!override),
           "fragment lighting gate");
  }
 }
 std::puts("cardinal surface lighting predicates and handoff passed");
}
"""


def source_for(backend):
    directory = shader_directory(backend)
    compute = (directory / f"c_lighting_to_trixel_body.{backend}").read_text()
    fragment = (directory / f"ir_trixel_to_framebuffer_body.{backend}").read_text()
    common = (directory / f"ir_iso_common.{backend}").read_text()
    systems = ROOT / "engine/prefabs/irreden/render/systems"
    producer = (systems / "system_lighting_to_trixel.hpp").read_text()
    consumer = (systems / "system_trixel_to_framebuffer.hpp").read_text()
    gate = re.search(r"bool worldSurfaceLightingEligible = false;.*?"
                     r"perAxisSurfaceLightingEligible_\s*=[^;]+;", producer, re.DOTALL).group()
    active_scan = extract_block(
        gate,
        "if (cardinalSurfaceLightingEligible_ && findSystem(TRIXEL_GLITCH) != kNullSystemId) {",
    )
    gate = gate.replace(active_scan, "")
    publish = extract_block(producer, "if (entity == perAxisCanvasEntity_) {")
    reset = re.search(r"cardinalSurfaceLightingEligible_ = false;\s*"
                      r"cardinalSurfaceLightingReady_ = false;", producer).group()
    consume = re.search(r"cardinalLightingEnabled_ = lighting_ != nullptr[^;]+;", consumer).group()
    prepare = re.search(r"frameData_\.normalOptions_\.y =[^;]+;", producer).group()
    prepare = prepare.replace("frameData_.normalOptions_.y", "normalOptions.y")
    start = consumer.index("const bool cardinalLighting =")
    flag = consumer[start:consumer.index("if (cardinalLighting)", start)]
    flag = flag.replace("frameData.frameData_.scatterDebugMode_", "scatterDebugMode")
    start = consumer.index("const bool probe = (shapeProbeEnabled_")
    selection = consumer[start:consumer.index("if ((probe && shapeLightingEnabled_)", start)]
    host = PREAMBLE
    for token, code in (("RESET", reset), ("GATE", gate), ("PUBLISH", publish),
                        ("PREPARE", prepare), ("CONSUME", consume), ("SELECT_FLAG", flag),
                        ("SELECT_PROGRAM", selection)):
        host = host.replace(token, code)
    helpers = "\n".join(re.findall(r"(?:const|constant) uint kEntityId(?:CardinalVoxel|CutFace)"
                                    r"MaskInHighWord = [^;]+;", common))
    helpers += extract_function(common, "decodeEntityIdCardinalVoxel")
    helpers += extract_function(common, "decodeCutFace")
    helpers += extract_function(common, "isCardinalVoxelSurface")
    skip = re.search(r"if \((?:frameData\.)?normalOptions.y != 0 && !sourceMode.*?\) return;",
                     compute, re.DOTALL).group()
    disabled = re.search(r"if \((?:frameData\.)?lightingEnabled == 0\) \{\s*return;\s*\}",
                         compute).group()
    # The shape branch also has a lighting gate; the cardinal branch owns the provenance read.
    condition = next(match.group() for match in re.finditer(
        r"if \(!glitchPresentationOverride[^\{]+\{", fragment)
        if "isCardinalVoxelSurface" in match.group())
    predicates = (helpers + "\nbool skip(){" + disabled.replace("return;", "return false;")
                  + skip.replace("return;", "return true;") + "return false;}\n"
                  + "bool fragment(){" + condition + "return true;}return false;}\n")
    for prefix in ("voxelFrameData.", "frameData.", "lighting."):
        predicates = predicates.replace(prefix, "")
    predicates = predicates.replace("cardinalLightingEnabled", "scatterDebugMode")
    predicates = re.sub(r"imageLoad\(trixelEntityIds, pixel\)\.xy|"
                        r"trixelEntityIds.read\(uint2\(pixel\)\)\.xy|"
                        r"texelFetch\(triangleEntityIds, sampleCoord, 0\)\.rg|"
                        r"triangleEntityIds.read\(sampleCoord\)\.rg", "id", predicates)
    predicates = predicates.replace("constant uint", "const uint").replace("uint2", "uvec2")
    return host + predicates + CHECKS


@unittest.skipUnless(COMPILER, "cardinal routing controls require a C++ compiler")
class CardinalSurfaceLightingTest(unittest.TestCase):
    def test_production_predicates_readiness_and_selection(self):
        for backend in ("glsl", "metal"):
            source = source_for(backend)
            variants = {
                "production": source,
                "lighting_disabled": replace_once(source, "lightingEnabled != 0", "true"),
                "stale_ready": replace_once(source, "cardinalSurfaceLightingReady_ = false;", ""),
                "eligible_not_ready": replace_once(
                    source, "lighting_->cardinalSurfaceLightingReady_",
                    "lighting_->cardinalSurfaceLightingEligible_"),
                "glitch_exclusion": replace_once(
                    source,
                    "worldSurfaceLightingEligible && mainVoxelPool_ != nullptr &&",
                    "worldSurfaceLightingEligible && "
                    "findSystem(TRIXEL_GLITCH) == kNullSystemId && "
                    "mainVoxelPool_ != nullptr &&",
                ),
                "destination_relighting": replace_once(
                    source, "!glitchPresentationOverride &&", "true &&"
                ),
                "cut_face": source.replace("!decodeCutFace(rawId)", "true"),
            }
            messages = {"lighting_disabled": "fragment lighting gate",
                        "stale_ready": "begin resets readiness before main tick",
                        "eligible_not_ready": "begin resets readiness before main tick",
                        "glitch_exclusion": "producer eligibility",
                        "destination_relighting": "fragment lighting gate",
                        "cut_face": "fragment lighting gate"}
            for name, candidate in variants.items():
                with (self.subTest(backend=backend, variant=name),
                      tempfile.TemporaryDirectory() as temp):
                    path = Path(temp)
                    (path / "routing.cpp").write_text(candidate)
                    build = subprocess.run(
                        [COMPILER, "-std=c++17", "-O2", str(path / "routing.cpp"),
                         "-o", str(path / "routing")], capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(path / "routing")], capture_output=True, text=True)
                    if name == "production":
                        self.assertEqual(run.returncode, 0, run.stderr)
                    else:
                        self.assertNotEqual(candidate, source)
                        self.assertEqual(run.returncode, 1, run.stderr)
                        self.assertIn(messages[name], run.stderr)

    def test_glitch_source_route_lifetime_and_bindings(self):
        systems = ROOT / "engine/prefabs/irreden/render/systems"
        producer = (systems / "system_lighting_to_trixel.hpp").read_text()
        create = extract_block(producer, "static SystemId create() {")
        ensure_programs = extract_function(producer, "ensureGlitchSourcePrograms")
        self.assertNotIn("LightingToTrixelGlitchSourceProgram", create)
        self.assertIn("LightingToTrixelGlitchSourceProgram", ensure_programs)
        self.assertIn("ensureGlitchSourcePrograms();", producer)
        self.assertIn("ensureGlitchPresentationImages(textures);", producer)
        self.assertIn("IRPlatform::kIsOpenGL ? 7 : 8", producer)
        selection = re.search(
            r"ShaderProgram \*lightingProgram\s*=.*?;",
            producer,
            re.DOTALL,
        ).group()
        self.assertIn("glitchSource ? shapeGlitchSourceProgram_ : shapeProgram_", selection)
        self.assertIn("glitchSource ? glitchSourceProgram_ : program_", selection)

        component = (ROOT / "engine/prefabs/irreden/render/components/"
                     "component_triangle_canvas_textures.hpp").read_text()
        ensure_images = extract_function(component, "ensureGlitchPresentationImages")
        self.assertIn("if (hasGlitchPresentationImages())", ensure_images)
        self.assertIn("return false;", ensure_images)

        for backend in ("glsl", "metal"):
            directory = shader_directory(backend)
            glitch = (directory / f"c_trixel_glitch.{backend}").read_text()
            lighting = (directory / f"c_lighting_to_trixel_body.{backend}").read_text()
            gather = (directory / f"ir_trixel_to_framebuffer_body.{backend}").read_text()
            effect = extract_block(
                glitch,
                "uvec4 loadEffectColour(" if backend == "glsl"
                else "static uint4 glitchLoadEffectColour(",
            )
            self.assertIn("isCardinalVoxelSurface", effect)
            mutated_effect = effect.replace("isCardinalVoxelSurface", "alwaysSourceLit", 1)
            self.assertNotEqual(mutated_effect, effect)
            self.assertNotIn("isCardinalVoxelSurface", mutated_effect)

            refresh = (
                "imageStore(glitchSourceLitColors, pixel, imageLoad(trixelColors, pixel));"
                if backend == "glsl"
                else "glitchSourceLitColors.write(trixelColors.read(uint2(pixel)), uint2(pixel));"
            )
            empty = "if (encoded >="
            self.assertLess(lighting.index(refresh), lighting.index(empty))
            mutated_lighting = lighting.replace(refresh, "removedSourceRefresh", 1)
            self.assertNotIn(refresh, mutated_lighting[:mutated_lighting.index(empty)])

            shape_gate = re.search(
                r"#if IR_SHAPE_LIGHTING\s*if \(!glitchPresentationOverride",
                gather,
            )
            self.assertIsNotNone(shape_gate)

        glsl = (shader_directory("glsl") / "c_lighting_to_trixel_body.glsl").read_text()
        metal = (shader_directory("metal") / "c_lighting_to_trixel_body.metal").read_text()
        self.assertIn("layout(rgba8, binding = 7) writeonly", glsl)
        self.assertIn("glitchSourceLitColors [[texture(8)]]", metal)

        glitch_system = (systems / "system_trixel_glitch.hpp").read_text()
        dispatch = extract_block(glitch_system, "void dispatch() override {")
        self.assertIn("memoryBarrier(BarrierType::ALL);", dispatch)


if __name__ == "__main__":
    unittest.main()

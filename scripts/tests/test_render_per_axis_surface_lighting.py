"""Execute per-axis fragment inputs and the albedo producer/consumer handshake."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_render_per_axis_caster_position import COMPILER, ROOT, functions
from test_render_per_axis_receiver import host
from test_render_per_axis_surface import CHECKS as SURFACE_CHECKS
from test_render_per_axis_surface import harness as surface_harness

FRAGMENT_ADAPTER = r"""
vec2 clamp(vec2 point,vec2 low,vec2 high){
    return {std::clamp(point.x,low.x,high.x),std::clamp(point.y,low.y,high.y)};
}
int aoReads=0,lightingCalls=0;
float aoInput=0,capturedAO=0;
ivec2 aoPixel;
vec3 capturedAlbedo,capturedPosition,capturedNormal,capturedLocalPosition;
vec4 capturedRotation;
struct AOSample {float r;};
struct AOTexture {
    AOSample read(ivec2 pixel)const {++aoReads;aoPixel=pixel;return {aoInput};}
} surfaceAO;
AOSample texelFetch(AOTexture texture,ivec2 pixel,int lod){
    if(lod!=0)std::exit(20);
    return texture.read(pixel);
}
vec3 worldSurfaceLighting(vec3 albedo,float ao,vec3 position,vec3 normal,
                         vec4 rotation,vec3 localLightPosition){
    ++lightingCalls;capturedAlbedo=albedo;capturedAO=ao;
    capturedPosition=position;capturedNormal=normal;
    capturedRotation=rotation;capturedLocalPosition=localLightPosition;
    return vec3(.25f,.5f,.75f);
}
"""

FRAGMENT_CASES = r"""
int main(int argc,char** argv){
    if(argc>1)mutation=argv[1];
    const ivec2 canvasSize(512,768),base(293,370);
    const vec2 points[]={vec2(0,0),vec2(1,0),vec2(0,1),vec2(1,1),
        vec2(.125f,.75f),vec2(.625f,.25f),vec2(-.25f,.5f),vec2(1.25f,.5f),
        vec2(.25f,-.125f),vec2(.5f,1.5f),vec2(-.5f,1.25f),vec2(1.5f,-.25f)};
    const vec4 rotations[]={vec4(0,0,0,1),vec4(0,0,.6f,.8f),vec4(.36f,.48f,0,.8f)};
    int checks=0;
    for(int x:{0,1,8,15})for(int y:{0,1,8,15})for(int z:{0,1,8,15})
    for(int faceId=0;faceId<6;++faceId)for(int flip:{0,1}){
        const int axis=faceId/2,slot=(axis+1)%3;
        const vec3 center((x&1?-16.f:8.f)+x/16.f-.5f,-3+y/16.f-.5f,4+z/16.f-.5f);
        int encoded=0;
        const ivec3 stored=perAxisStoreFacePos({center,1},faceId,slot,axis,flip,encoded);
        const ivec2 cell=base+pos3DtoPos2DIso(stored);
        int visibleFaceIds[]={5,0,3};visibleFaceIds[slot]=faceId^flip;
        const vec4 rotation=rotations[(x+y+z+faceId+flip)%3];
        sunCasterViewToWorld=rotation;sunFrameData.sunCasterViewToWorld=rotation;
        vec3 normal(0);normal[axis]=(faceId&1)?1.f:-1.f;
        for(bool overflow:{false,true}){
            const FaceFields face=scatterFace(overflow,cell,encoded,canvasSize,base,visibleFaceIds);
            const ivec2 expectedOwner=overflow?ivec2(-1):cell;
            if(face.ownerPixel.x!=expectedOwner.x||face.ownerPixel.y!=expectedOwner.y){
                std::fprintf(stderr,"owner pixel forwarding\n");return 1;
            }
            for(vec2 param:points)for(float ao:{0.f,.3125f,1.f}){
                // Project the infinite plane point onto the independently specified cube bounds.
                vec3 expected=cubeSurface(center,faceId,param);
                for(int coordinate=0;coordinate<3;++coordinate){
                    expected[coordinate]=std::max(center[coordinate]-.5f,
                        std::min(center[coordinate]+.5f,expected[coordinate]));
                }
                aoReads=lightingCalls=0;aoInput=ao;
                const vec4 albedo(.2f,.3f,.4f,.37f);
                const vec4 result=fragmentBeauty(face,param,albedo);
                if(lightingCalls!=1||!same(capturedPosition,expected)){
                    std::fprintf(stderr,"closest finite receiver\n");return 2;
                }
                if(!same(capturedNormal,normal)||!same(capturedRotation,rotation)){
                    std::fprintf(stderr,"normal and caster rotation\n");return 3;
                }
                const vec3 expectedLocal=cubeSurface(center,faceId,vec2(0))+vec3(.5f);
                if(!same(capturedLocalPosition,expectedLocal)){
                    std::fprintf(stderr,"preserved local-light origin\n");return 4;
                }
                if(capturedAO!=(overflow?1.f:ao)||aoReads!=(overflow?0:1)||
                   (!overflow&&(aoPixel.x!=cell.x||aoPixel.y!=cell.y))){
                    std::fprintf(stderr,"AO owner and overflow\n");return 5;
                }
                if(!same(capturedAlbedo,albedo.xyz)||!same(result,vec4(.25f,.5f,.75f,.37f))){
                    std::fprintf(stderr,"material and output alpha\n");return 6;
                }
                ++checks;
            }
        }
    }
    std::printf("%d finite fragment inputs\n",checks);
}
"""


def beauty_controls(source):
    match = re.search(r"#elif\s+IR_PER_AXIS_SURFACE_LIGHTING\s*\n(.*?)^#else\b",
                      source, re.DOTALL | re.MULTILINE)
    if match is None:
        raise ValueError("missing fragment surface lighting branch")
    body = host(match[1]).replace("uint2(", "ivec2(")
    for field in ("faceOrigin", "faceId", "ownerPixel"):
        body = body.replace("in." + field, "face." + field)
        body = body.replace("v" + field[0].upper() + field[1:], "face." + field)
    for shader, adapter in (("in.quadParam", "quadParam"), ("vQuadParam", "quadParam"),
                            ("in.color", "color"), ("vColor", "color"),
                            ("out.color", "result"), ("FragColor", "result")):
        body = body.replace(shader, adapter)
    body = re.sub(r",\s*lighting\s*,\s*volumeParams\s*,\s*sunFrameData\s*,\s*sunDepthBuf\s*,"
                  r"\s*lights\s*,\s*paletteLUT\s*,\s*lightVolume\s*,\s*lightVolumeId", "", body)
    body = body.replace("color.rgb", "color.xyz").replace("color.a", "color.w")
    variants = {
        "unclamped-margin": body.replace("perAxisFaceClosestPoint(", "perAxisFaceSurfacePoint("),
        "center-only": re.sub(r"(perAxisFaceClosestPoint\([^;]+,)\s*quadParam\)",
                              r"\1 vec2(.5))", body),
        "moving-local-light": re.sub(r"(sunCasterViewToWorld\s*,)\s*face\.faceOrigin",
                                    r"\1 position", body),
        "wrong-normal": re.sub(r"faceOutwardNormal6\(\s*face.faceId\s*\)",
                               "faceOutwardNormal6(face.faceId ^ 1)", body),
        "wrong-rotation": re.sub(r"(?:sunFrameData\.)?sunCasterViewToWorld",
                                 "vec4(0,0,0,1)", body),
        "ignored-ao": re.sub(r"const float ao\s*=\s*[^;]+;", "const float ao = 1.0;", body),
        "overflow-reads-ao": re.sub(r"face.ownerPixel.x\s*<\s*0", "false", body),
        "lost-alpha": body.replace("color.w", "1.0"),
    }
    header = "vec4 fragmentBeauty(FaceFields face,vec2 quadParam,vec4 color){\n"
    generated, dispatch = "", ""
    for index, (name, candidate) in enumerate(variants.items()):
        if candidate == body:
            raise ValueError(f"missing mutation target: {name}")
        function = f"fragmentBeautyMutation{index}"
        generated += (header.replace("fragmentBeauty", function) + "vec4 result;\n"
                      + candidate + "\nreturn result;\n}\n")
        dispatch += (f'if(std::strcmp(mutation,"{name}")==0)'
                     f"return {function}(face,quadParam,color);\n")
    return generated + header + dispatch + "vec4 result;\n" + body + "\nreturn result;\n}\n"


def fragment_harness(suffix, directory):
    root = ROOT / "engine/render/src/shaders" / directory
    source = (root / f"ir_peraxis_scatter_fragment_body.{suffix}").read_text()
    surface = (root / f"ir_per_axis_surface.{suffix}").read_text()
    return ("#include <cstdlib>\n" + surface_harness(suffix, directory).removesuffix(SURFACE_CHECKS)
            + FRAGMENT_ADAPTER + host(functions(surface, "perAxisFaceClosestPoint"))
            + beauty_controls(source) + FRAGMENT_CASES)


def braced_block(source, pattern):
    match = re.search(pattern, source)
    if match is None:
        raise ValueError(f"missing block: {pattern}")
    opening = source.index("{", match.start())
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


ROUTING_ADAPTER = r"""
#include <cstdio>
#include <cstring>
#include <optional>
enum System {COMPUTE_SUN_SHADOW,TRIXEL_TO_FRAMEBUFFER,FOG_TO_TRIXEL};
constexpr int kNullSystemId=-1;
bool systems[3];
int findSystem(System s){return systems[s]?int(s):kNullSystemId;}
enum class DebugOverlayMode {NONE,AO};
namespace IRRender {
bool overlay=false,depth=false;
DebugOverlayMode getDebugOverlay(){return overlay?DebugOverlayMode::AO:DebugOverlayMode::NONE;}
bool getDepthColorDebugMode(){return depth;}
}
struct C_TrixelCanvasRenderBehavior {bool useCameraPositionIso_;};
namespace IREntity {
C_TrixelCanvasRenderBehavior behavior;
bool hasBehavior;
template<class T>std::optional<T*> getComponentOptional(int){
    return hasBehavior?std::optional<T*>(&behavior):std::nullopt;
}
}
struct Axes {bool allocated;bool isAllocated()const{return allocated;}};
struct Program {int calls=0;void use(){++calls;}};
struct Producer {
    bool perAxisSurfaceLightingEnabled_=false;
    bool perAxisSurfaceLightingEligible_=false,perAxisSurfaceLightingReady_=false;
    Axes* perAxisCanvases_=nullptr;
    const int *mainCanvasTextures_=nullptr,*mainCanvasAO_=nullptr;
    const int *mainCanvasSunShadow_=nullptr,*mainCanvasLightVolume_=nullptr;
    int perAxisCanvasEntity_=7,dispatches=0;
    Program* program_;
    void dispatchPerAxisLighting(Axes&,int,int,int){++dispatches;}
"""

ROUTING_CASES = r"""
int main(){
    const char* values[]={nullptr,"","0","1","01","true","1 ","11"};
    for(const char* value:values){
        const bool expected=value && std::strcmp(value,"1")==0;
        if(enabledFromEnv(value)!=expected){std::fprintf(stderr,"explicit opt-in\n");return 1;}
    }
    int resource=1;
    for(unsigned bits=0;bits<(1u<<14);++bits){
        const auto on=[bits](unsigned bit){return (bits&(1u<<bit))!=0;};
        Axes axes{on(2)};Program program;Producer producer;
        producer.program_=&program;
        producer.perAxisSurfaceLightingEnabled_=on(0);
        producer.perAxisCanvases_=on(1)?&axes:nullptr;
        producer.mainCanvasTextures_=on(3)?&resource:nullptr;
        producer.mainCanvasAO_=on(4)?&resource:nullptr;
        producer.mainCanvasSunShadow_=on(5)?&resource:nullptr;
        producer.mainCanvasLightVolume_=on(6)?&resource:nullptr;
        systems[0]=on(7);systems[1]=on(8);systems[2]=!on(9);
        IRRender::overlay=!on(10);IRRender::depth=!on(11);
        IREntity::hasBehavior=on(12);IREntity::behavior.useCameraPositionIso_=on(13);
        producer.perAxisSurfaceLightingEligible_=true;
        producer.perAxisSurfaceLightingReady_=true;
        producer.begin();
        const bool expectedEligible=bits==(1u<<14)-1;
        if(producer.perAxisSurfaceLightingEligible_!=expectedEligible){
            std::fprintf(stderr,"eligibility %u\n",bits);return 2;
        }
        if(producer.perAxisSurfaceLightingReady_ || consume(&producer)){
            std::fprintf(stderr,"unpublished or stale readiness\n");return 3;
        }
        producer.tick(8,&resource);
        producer.tick(7,nullptr);
        if(producer.dispatches || producer.perAxisSurfaceLightingReady_ || consume(&producer)){
            std::fprintf(stderr,"non-owner tick published\n");return 4;
        }
        if(IREntity::hasBehavior)producer.tick(7,&resource);
        const bool active=on(1)&&on(2)&&on(12)&&on(13);
        const int expectedDispatch=active&&!expectedEligible?1:0;
        if(producer.dispatches!=expectedDispatch || program.calls!=expectedDispatch){
            std::fprintf(stderr,"compute albedo preservation\n");return 5;
        }
        if(producer.perAxisSurfaceLightingReady_!=expectedEligible ||
           consume(&producer)!=expectedEligible){
            std::fprintf(stderr,"published readiness\n");return 6;
        }
    }
    if(consume(nullptr)){std::fprintf(stderr,"missing producer\n");return 7;}
    std::printf("16384 readiness combinations\n");
}
"""


def routing_harness(variant):
    systems = ROOT / "engine/prefabs/irreden/render/systems"
    producer = (systems / "system_lighting_to_trixel.hpp").read_text()
    consumer = (systems / "system_trixel_to_framebuffer.hpp").read_text()
    begin = functions(producer, "beginTick")
    reset = "\n".join(re.findall(
        r"perAxisSurfaceLighting(?:Eligible|Ready)_\s*=\s*false\s*;", begin))
    gate = braced_block(begin, r"if\s*\(\s*perAxisSurfaceLightingEnabled_\b")
    tick = functions(producer, "tick")
    skip_detached = braced_block(tick, r"if\s*\(\s*!behavior\.useCameraPositionIso_\b")
    publish = braced_block(tick, r"if\s*\(\s*entity\s*==\s*perAxisCanvasEntity_\b")
    consume = re.search(r"scatterLightingEnabled_\s*=\s*[^;]+;",
                        functions(consumer, "beginTick"))[0]
    enabled = re.search(r"p->perAxisSurfaceLightingEnabled_\s*=\s*([^;]+);", producer)[1]
    if variant == "stale-ready":
        reset = re.sub(r"perAxisSurfaceLightingReady_\s*=\s*false\s*;", "", reset)
    elif variant == "ready-before-tick":
        gate += "\nperAxisSurfaceLightingReady_ = perAxisSurfaceLightingEligible_;"
    elif variant == "consumer-eligibility":
        consume = consume.replace("perAxisSurfaceLightingReady_", "perAxisSurfaceLightingEligible_")
    elif variant == "fog-accepted":
        gate = re.sub(r"findSystem\(FOG_TO_TRIXEL\)\s*==\s*kNullSystemId", "true", gate)
    elif variant == "relight-eligible":
        publish = re.sub(r"if\s*\(perAxisSurfaceLightingEligible_\)", "if (false)", publish)
    elif variant == "presence-enables":
        enabled = "surfaceLighting != nullptr"
    return (ROUTING_ADAPTER + "void begin(){" + reset + gate + "}\n"
            "void tick(int entity,const int* shadow){int canvasTextures=0,ao=0;"
            "const auto& behavior=IREntity::behavior;" + skip_detached + publish + "}\n};\n"
            "bool consume(const Producer* lighting_){bool scatterLightingEnabled_=false;"
            + consume + "return scatterLightingEnabled_;}\n"
            "bool enabledFromEnv(const char* surfaceLighting){return " + enabled + ";}\n"
            + ROUTING_CASES)


BINDING_ADAPTER = r"""
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
enum class BufferTarget {UNIFORM,SHADER_STORAGE};
enum class TextureAccess {READ_ONLY,WRITE_ONLY};
enum class TextureFormat {RGBA8,RGBA32F};
constexpr unsigned kMaxMetalBindings=32,kMaxMetalTextureBindings=32;
namespace MTL {
struct Buffer {int id;};
struct Texture {int id;};
struct RenderCommandEncoder {
    std::array<Buffer*,32> buffers{};
    std::array<Texture*,32> textures{};
    void setVertexBuffer(Buffer*,unsigned,unsigned){}
    void setFragmentBuffer(Buffer* buffer,unsigned,unsigned slot){buffers[slot]=buffer;}
    void setVertexTexture(Texture*,unsigned){}
    void setFragmentTexture(Texture* texture,unsigned slot){textures[slot]=texture;}
};
}
struct MetalBufferBinding {MTL::Buffer* buffer_=nullptr;unsigned offset_=0;};
struct Runtime {
    std::array<MTL::Texture*,32> textures_{},imageTextures_{};
    std::array<MetalBufferBinding,32> uniforms{},storage{};
    TextureAccess access=TextureAccess::WRITE_ONLY;
    TextureFormat format=TextureFormat::RGBA32F;
} runtime;
Runtime& g_runtime(){return runtime;}
bool metal=false;
const MetalBufferBinding& boundMetalBuffer(BufferTarget target,unsigned slot){
    return (target==BufferTarget::UNIFORM?runtime.uniforms:runtime.storage)[slot];
}
MTL::Texture* boundMetalTexture(unsigned slot){return runtime.textures_[slot];}
void markMetalBufferEncoded(MTL::Buffer*){}
"""

BINDING_RESOURCES = r"""
struct Buffer {
    MTL::Buffer value;
    void bindBase(BufferTarget target,unsigned slot){
        (target==BufferTarget::UNIFORM?runtime.uniforms:runtime.storage)[slot]={&value,0};
    }
};
struct Texture {
    MTL::Texture value;
    void bind(unsigned slot){
        if(metal)bindMetalTexture(slot,&value);
        else runtime.textures_[slot]=&value;
    }
    void bindAsImage(unsigned slot,TextureAccess access,TextureFormat format){
        runtime.access=access;runtime.format=format;
        if(metal)bindMetalImageTexture(slot,&value);
        else runtime.imageTextures_[slot]=&value;
    }
};
struct Lighting {
    Buffer *frameDataBuf_,*lightVolumeParamsBuf_,*lightSourceBuf_;
    Texture *paletteLUT_;
};
struct Volume {
    Texture *read,*id;
    Texture* getReadTexture(){return read;}
    Texture* getIdReadTexture(){return id;}
};
struct Binding {
    Lighting* lighting_;
    Volume* surfaceLightVolume_;
"""

BINDING_CASES = r"""
int main(int argc,char** argv){
    const char* mutation=argc>1?argv[1]:"";
    for(bool backend:{false,true})for(int frame:{0,100,200}){
        metal=backend;runtime={};
        Buffer lighting{{frame+27}},params{{frame+107}},lights{{frame+204}};
        Texture palette{{frame+303}},volume{{frame+405}},ids{{frame+507}},stale{{999}};
        runtime.textures_.fill(&stale.value);runtime.imageTextures_.fill(&stale.value);
        Lighting producer{&lighting,&params,&lights,&palette};Volume canvas{&volume,&ids};
        Binding binding{&producer,&canvas};
        if(std::strcmp(mutation,"omit-image")==0)binding.omitImage();
        else if(std::strcmp(mutation,"omit-texture")==0)binding.omitTexture();
        else if(std::strcmp(mutation,"reverse-order")==0)binding.reverseOrder();
        else binding.bindSurfaceLightingResources();
        MTL::RenderCommandEncoder encoder;
        if(metal)bindRenderResources(&encoder);
        MTL::Buffer* frameBuffer=metal?encoder.buffers[27]:runtime.uniforms[27].buffer_;
        MTL::Buffer* volumeParams=metal?encoder.buffers[7]:runtime.uniforms[7].buffer_;
        MTL::Buffer* lightSources=metal?encoder.buffers[4]:runtime.storage[4].buffer_;
        MTL::Texture* paletteTexture=metal?encoder.textures[3]:runtime.textures_[3];
        MTL::Texture* volumeTexture=metal?encoder.textures[5]:runtime.textures_[5];
        MTL::Texture* idTexture=metal?encoder.textures[7]:runtime.imageTextures_[7];
        if(frameBuffer!=&lighting.value||volumeParams!=&params.value||lightSources!=&lights.value||
           paletteTexture!=&palette.value||volumeTexture!=&volume.value){
            std::fprintf(stderr,"surface resource identity/slot\n");return 1;
        }
        if(idTexture!=&ids.value){
            std::fprintf(stderr,"%s fragment ID volume unavailable\n",metal?"Metal":"GL");
            return metal?12:11;
        }
        if(!metal&&(runtime.access!=TextureAccess::READ_ONLY||runtime.format!=TextureFormat::RGBA8)){
            std::fprintf(stderr,"GL image access/format\n");return 2;
        }
    }
    std::printf("6 backend/frame binding cases\n");
}
"""


def binding_harness():
    source = (ROOT / "engine/prefabs/irreden/render/systems/"
              "system_trixel_to_framebuffer.hpp").read_text()
    helper = functions(source, "bindSurfaceLightingResources")
    image = re.search(r"lightVolumeId\s*->\s*bindAsImage\s*\([^;]+;", helper)[0]
    texture = re.search(r"lightVolumeId\s*->\s*bind\s*\([^;]+;", helper)[0]
    reversed_helper = helper.replace(image, "IMAGE_BIND").replace(texture, image)
    reversed_helper = reversed_helper.replace("IMAGE_BIND", texture)
    variants = (helper.replace(image, "").replace("bindSurfaceLightingResources", "omitImage"),
                helper.replace(texture, "").replace("bindSurfaceLightingResources", "omitTexture"),
                reversed_helper.replace("bindSurfaceLightingResources", "reverseOrder"))
    types = (ROOT / "engine/render/include/irreden/render/ir_render_types.hpp").read_text()
    constants = "\n".join(re.search(r"constexpr std::uint32_t " + name + r"\s*=[^;]+;", types)[0]
                          for name in ("kBufferIndex_FrameDataVoxelToCanvas",
                                       "kBufferIndex_SurfaceLightVolumeParams",
                                       "kBufferIndex_FrameDataLightingToTrixel",
                                       "kBufferIndex_LightSourceBuffer"))
    runtime = (ROOT / "engine/render/src/metal/metal_runtime.cpp").read_text()
    render = (ROOT / "engine/render/src/metal/metal_render_impl.cpp").read_text()
    metal = (functions(runtime, "bindMetalTexture") + functions(runtime, "bindMetalImageTexture")
             + functions(render, "bindRenderResources"))
    return (BINDING_ADAPTER + constants + metal + BINDING_RESOURCES + helper
            + "\n".join(variants) + "\n};\n" + BINDING_CASES)


@unittest.skipUnless(COMPILER, "surface lighting controls require a C++ compiler")
class PerAxisSurfaceLightingTest(unittest.TestCase):
    def test_surface_resource_binding_namespaces(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            cpp, executable = path / "binding.cpp", path / "binding"
            cpp.write_text(binding_harness())
            build = subprocess.run([COMPILER, "-std=c++17", str(cpp), "-o", str(executable)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            for variant, expected in (("production", 0), ("omit-image", 11),
                                      ("omit-texture", 12), ("reverse-order", 12)):
                with self.subTest(variant=variant):
                    run = subprocess.run([str(executable), variant], capture_output=True, text=True)
                    self.assertEqual(run.returncode, expected, run.stderr)
                    if variant == "production":
                        self.assertIn("6 backend/frame binding cases", run.stdout)

    def test_closest_surface_and_fragment_inputs(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            with self.subTest(backend=suffix), tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary)
                cpp, executable = path / "fragment.cpp", path / "fragment"
                cpp.write_text(fragment_harness(suffix, directory))
                build = subprocess.run([COMPILER, "-std=c++17", str(cpp), "-o", str(executable)],
                                       capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stderr)
                result = subprocess.run([str(executable)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("55296 finite fragment inputs", result.stdout)
                for variant in ("unclamped-margin", "center-only", "moving-local-light",
                                "wrong-normal", "wrong-rotation", "ignored-ao",
                                "overflow-reads-ao", "lost-alpha"):
                    with self.subTest(variant=variant):
                        result = subprocess.run([str(executable), variant],
                                                capture_output=True, text=True)
                        self.assertIn(result.returncode, range(1, 7), result.stderr)

    def test_albedo_producer_consumer_readiness(self):
        original = routing_harness("production")
        for variant in ("production", "stale-ready", "ready-before-tick", "consumer-eligibility",
                        "fog-accepted", "relight-eligible", "presence-enables"):
            with self.subTest(variant=variant), tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary)
                source = routing_harness(variant)
                if variant != "production":
                    self.assertNotEqual(source, original)
                cpp, executable = path / "routing.cpp", path / "routing"
                cpp.write_text(source)
                build = subprocess.run([COMPILER, "-std=c++17", str(cpp), "-o", str(executable)],
                                       capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stderr)
                result = subprocess.run([str(executable)], capture_output=True, text=True)
                if variant == "production":
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn("16384 readiness combinations", result.stdout)
                else:
                    self.assertIn(result.returncode, range(1, 8), result.stderr)


if __name__ == "__main__":
    unittest.main()

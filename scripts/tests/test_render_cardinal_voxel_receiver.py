"""Connect cardinal voxel publication to exact microface-center reconstruction.

Both shader backends execute their production raster statements and face emitter
in a scalar C++ adapter. Independent cube vertices supply the surface oracle;
these controls do not emulate GPU ownership, rasterization, or shadow sampling.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import replace_once
from test_render_per_axis_caster_position import PREAMBLE, cpp, functions

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")


def shader_directory(backend):
    return ROOT / "engine/render/src/shaders" / ("metal" if backend == "metal" else "")

ADAPTER = r"""
#include <vector>
#include <cstdlib>
using std::min;using std::max;using std::ceil;
template<class T> V2<T> operator*(V2<T> a,T b){return {a.x*b,a.y*b};}
vec3 operator*(float a,vec3 b){return b*a;}
float length(vec2 a){return std::sqrt(a.x*a.x+a.y*a.y);}
vec2 normalize(vec2 a){const float n=length(a);return {a.x/n,a.y/n};}
struct mat2 {
    vec2 columns[2]={{1,0},{0,1}};
    vec2 operator[](int i)const{return columns[i];}
    vec2 operator*(vec2 a)const{return columns[0]*a.x+columns[1]*a.y;}
};
struct Tap {ivec2 pixel;int encoded;};
std::vector<Tap> taps;
template<class... T> void writeDistanceTap(ivec2 pixel,int encoded,T...){
    taps.push_back({pixel,encoded});
}
int isoDepthAlongAxis(ivec3,vec3){std::abort();}
ivec2 voxelRenderOptions,trixelCanvasOffsetZ1,localId,canvasSize(4096,4096);
vec2 frameCanvasOffset;
vec4 voxelPosition,voxelDepthAxis{{1,1,1},0};
int cardinalIndex,slot,faceId,riserFlip,zIdx;
float isDetachedCanvas=0;
bool dilateRevox=false,bothPolaritiesExposed=false;
void* distanceScratch=nullptr;
struct {ivec2 xy;} gl_LocalInvocationID;
mat2 D;
"""

CHECKS = r"""
// Matrices map cardinal view axes into world axes, independently of shader helpers.
const int basis[4][3][3]={
 {{1,0,0},{0,1,0},{0,0,1}},{{0,-1,0},{1,0,0},{0,0,1}},
 {{-1,0,0},{0,-1,0},{0,0,1}},{{0,1,0},{-1,0,0},{0,0,1}}
};
int worldFace(int axis,int positive,int cardinal){
 for(int w=0;w<3;++w)if(basis[cardinal][w][axis])
  return 2*w+int(basis[cardinal][w][axis]*(positive?1:-1)>0);
 std::abort();
}
vec3 expectedCenter(vec3 authored,int mode,int density,int axis,int positive,
                    int u,int v,int cardinal){
 double center[3];
 for(int w=0;w<3;++w){
  double p=authored[w];
  if(mode && std::abs(p-std::round(p))<=.0001)p=std::round(p);
  center[w]=std::floor(p*(mode?density:1)+.5)/(mode?density:1);
 }
 double sum[3]={0,0,0};
 for(int a=0;a<2;++a)for(int b=0;b<2;++b){
  double viewCorner[3]={0,0,0};
  viewCorner[axis]=positive?.5:-.5;
  const int eu=axis==0?1:0,ev=axis==2?1:2;
  viewCorner[eu]=mode?(u+a)/double(density)-.5:a-.5;
  viewCorner[ev]=mode?(v+b)/double(density)-.5:b-.5;
  for(int w=0;w<3;++w){
   double vertex=center[w];
   for(int c=0;c<3;++c)vertex+=basis[cardinal][w][c]*viewCorner[c];
   sum[w]+=vertex;
  }
 }
 return {float(sum[0]/4),float(sum[1]/4),float(sum[2]/4)};
}
int main(int argc,char** argv){
 const int mutation=argc>1?std::atoi(argv[1]):0;
 const vec3 positions[]={
  {0,0,0},{-4,3,-2},{2.5f,-3.5f,.5f},{-.5f,.25f,-.75f},
  {.30f,-.20f,.49f},{-.499f,.501f,-1.501f},
  {1.00005f,-.99995f,-.00005f}
 };
 unsigned count=0,duals=0,scaledDepthDisagreements=0;
 for(int mode:{0,1})for(int density:{1,2,3,4,8,16})
 for(int cardinal=0;cardinal<4;++cardinal)for(int axis=0;axis<3;++axis)
 for(int positive:{0,1})for(vec3 position:positions)
 for(ivec2 canvas:{ivec2(642,722),ivec2(643,723)})
 for(vec2 camera:{vec2(0),vec2(3.75f,-2.25f),vec2(-.3f,.7f)}){
  voxelRenderOptions={mode,density};cardinalIndex=cardinal;slot=axis;
  faceId=worldFace(axis,positive,cardinal);riserFlip=positive;
  voxelPosition={position,1};frameCanvasOffset=camera;
  trixelCanvasOffsetZ1=trixelOriginOffsetZ1(canvas);
  const int samples=mode?density:1;
  for(int u=0;u<samples;++u)for(int v=0;v<samples;++v){
   zIdx=u*density+v;
   vec3 halves[2];
   for(int h=0;h<2;++h){
    // Independent invocation coordinates of the two published pixels per slot.
    const int offsets[3][2][2]={{{1,1},{1,2}},{{0,1},{0,2}},{{0,0},{1,0}}};
    localId={offsets[axis][h][0],offsets[axis][h][1]};
    gl_LocalInvocationID.xy=localId;
    bothPolaritiesExposed=mode && !positive && u==0 && v==0;
    taps.clear();produce();
    if(taps.size()!=(bothPolaritiesExposed?2u:1u))return 10;
    for(unsigned i=0;i<taps.size();++i){
     const int sign=i==0?positive:1-positive;
     const vec3 expected=expectedCenter(position,mode,density,axis,sign,u,v,cardinal);
     const vec3 actual=receive(mutation,taps[i].pixel,taps[i].encoded,
        trixelCanvasOffsetZ1,frameCanvasOffset,voxelRenderOptions,cardinal);
     if(std::abs(actual.x-expected.x)>2.e-6f ||
        std::abs(actual.y-expected.y)>2.e-6f ||
        std::abs(actual.z-expected.z)>2.e-6f)
      return fail("cardinal face vertices",actual,expected);
     const float worldDepth=expected.x+expected.y+expected.z;
     for(float split:{-10.f,0.f,1.f,5.f,10.f}){
      if(std::abs(worldDepth-split)<1.e-5f)continue;
      if((pos3DtoDistance(actual)>split)!=(worldDepth>split))
       return fail("world-depth split",actual,expected);
      scaledDepthDisagreements+=
       (float(decodeDepthSingle(taps[i].encoded))>split)!=(worldDepth>split);
     }
     if(i==0)halves[h]=actual;else ++duals;
     ++count;
    }
   }
   if(!same(halves[0],halves[1]))return fail("paired pixels",halves[0],halves[1]);
  }
 }
 std::printf("surface_centers=%u dual_polarity_centers=%u scaled_depth_split_disagreements=%u\n",
             count,duals,scaledDepthDisagreements);
 return count>100000 && duals>0 && scaledDepthDisagreements>0?0:11;
}
"""


def harness(backend):
    shaders = shader_directory(backend)
    common = (shaders / f"ir_iso_common.{backend}").read_text()
    producer = (shaders / f"c_voxel_to_trixel_stage_1_body.{backend}").read_text()
    receiver = cpp(functions((shaders / f"ir_cardinal_voxel_receiver.{backend}").read_text(),
                             "cardinalVoxelSurfaceCenter"))
    constants = "\n".join(re.findall(
        r"^(?:const|constant) (?:int|vec3|float3) (?:k\w+Face\w*|kFace\w+|"
        r"kDepthEncodeShift|kVoxelRasterCellAnchor) = [^;]+;", common, re.MULTILINE))
    helpers = constants.replace("constant ", "const ") + "\n"
    helpers += "\n".join(functions(common, name) for name in (
        "roundHalfUp", "snapNearIntegerVoxelPosition", "pos3DtoDistance",
        "pos3DtoPos2DIso", "isoPositionToPos3D", "isoPixelToPos3D",
        "faceOutwardNormal", "faceOutOfPlaneUnitAxis", "faceInPlaneUnitAxes", "faceInPlaneIsoSteps",
        "faceMicroPositionFixed6", "faceOffset_2x3", "encodeDepthWithFace",
        "decodeSlot", "decodeFlipSingle", "decodeDepthSingle",
        "effectiveTrixelSubdivisionScale", "trixelOriginOffsetX1",
        "trixelOriginOffsetZ1", "trixelFrameOffset", "trixelCanvasPixelToIsoRel",
        "rotateCardinalZ", "rotateCardinalZInv", "rotateFaceIdCardinalZ"))
    emitter = functions(producer, "emitDeformedFace")
    prefix = "frameData." if backend == "metal" else ""
    start = producer.index(f"    if ({prefix}voxelRenderOptions.x == 0)")
    body = producer[start:producer.rindex("\n}")]
    producer_code = emitter + "\nvoid produce(){\n" + body + "\n}\n"
    producer_code = producer_code.replace("frameData.", "")
    producer_code = producer_code.replace("device atomic_int*", "void*")
    producer_code = producer_code.replace("float2x2", "mat2").replace("uint2", "ivec2")
    variants = {
        "no_pixel_offset": ("iso - faceOffset_2x3(slot, emittedHalf)", "iso"),
        "wrong_y_parity": (" ^ (slot == kYFace ? 1 : 0)", ""),
        "packed_depth": ("float(decodeDepthSingle(encoded))", "float(encoded)"),
        "no_micro_center": ("origin + 0.5 * (vec3(1.0) - axis)", "origin"),
        "no_density": (" / density - kVoxelRasterCellAnchor", " - kVoxelRasterCellAnchor"),
        "no_anchor": (" - kVoxelRasterCellAnchor", ""),
        "anchor_in_world": (
            "(origin + 0.5 * (vec3(1.0) - axis)) / density - kVoxelRasterCellAnchor,\n"
            "        cardinalIndex)",
            "(origin + 0.5 * (vec3(1.0) - axis)) / density,\n"
            "        cardinalIndex) - kVoxelRasterCellAnchor"),
        "none_as_microface": ("voxelRenderOptions.x == 0", "false"),
        "none_wrong_polarity": (
            "decodeFlipSingle(encoded) != 0 ? -1.0 : 1.0", "1.0"),
    }
    dispatch = "\nvec3 receive(int mutation,ivec2 p,int e,ivec2 z,vec2 c,ivec2 o,int r){\n"
    for number, (name, (old, new)) in enumerate(variants.items(), 1):
        if receiver.count(old) != 1:
            raise ValueError(f"mutation {name} must replace exactly one expression")
        mutant = receiver.replace(old, new).replace("cardinalVoxelSurfaceCenter", f"mutant{number}")
        mutant = re.sub(r"^#(?:ifndef|define|endif).*\n?", "", mutant, flags=re.MULTILINE)
        helpers += "\n" + mutant
        dispatch += f"if(mutation=={number})return mutant{number}(p,e,z,c,o,r);\n"
    dispatch += "return cardinalVoxelSurfaceCenter(p,e,z,c,o,r);\n}\n"
    source = (PREAMBLE + ADAPTER + "#define IR_FEEDER_PASS 0\n"
              "#define IR_STORE_WINNER_ELECTION 0\n" + cpp(helpers)
              + "\n" + cpp(producer_code) + "\n" + receiver + dispatch + CHECKS)
    return source, list(variants)


ROUTE_ADAPTER = r"""
#include <cstdlib>
using uint=unsigned;using uvec2=V2<uint>;
struct uvec4 {uvec2 xy;uvec4(uvec2 a,uint,uint):xy(a){}};
int idReads=0,publications=0,outputWrites=0,centerCalls=0,surfaceCalls=0;
int legacyCalls=0,axisCalls=0;
uvec2 publishedId(0);int storedDepth=83;
vec4 output{{0,0,0},0};vec3 sampledPosition(0),sampledNormal(0);
vec4 sampledRotation{{0,0,0},0};float sampledDepth=0;
struct Sample {int x;uvec2 xy;};
struct Texture {
 int kind;
 Sample read(ivec2)const{if(kind==2)++idReads;return {storedDepth,publishedId};}
 void write(vec4 value,ivec2)const{if(kind==1){output=value;++outputWrites;}}
 void write(ivec4,ivec2)const{}
 void write(uvec4 value,ivec2)const{publishedId=value.xy;++publications;}
} canvasSunShadow{1},trixelEntityIds{2},triangleCanvasEntityIds{2},
  triangleCanvasDistances{0},triangleCanvasColors{3};
ivec2 imageSize(Texture){return {8,8};}
Sample imageLoad(Texture texture,ivec2 p){return texture.read(p);}
template<class T>void imageStore(Texture texture,ivec2 p,T value){texture.write(value,p);}
bool isInsideCanvas(ivec2 p,ivec2 size){return p.x>=0&&p.y>=0&&p.x<size.x&&p.y<size.y;}
const int memory_order_relaxed=0;
template<class T>T atomic_load_explicit(const T* p,int){return *p;}
int distanceScratch[64];uint perAxisWinnerIds[64];
ivec2 pixel(1,1),size(8,8),trixelCanvasOffsetZ1(3,3),voxelRenderOptions(1,4);
vec2 frameCanvasOffset(1.25f,-.75f);
int shadowsEnabled=1,perAxisRoute=0,face=0,flip=0,encoded=672,rawDepth=84;
int cardinalIndex=0,visibleFaceIds[3]={0,2,4},perAxisStoreFrame=0;
int sunFrameData=0,sunDepthBuf=0,receiverFrame=0,receiverShapes=0,receiverOwners=0,receiverTiles=0;
float residualYaw=0,isDetachedCanvas=0,receiverFace=.875f;
vec4 sunBasisU{{1,0,0},1},sunCasterViewToWorld{{.1f,.2f,.3f},.9f};
vec3 pos3D(41,42,43),normal(0,0,-1),surfacePoint(7,8,9);
bool sphere=false;
vec3 perAxisCellToWorld3DSubCell(ivec2,int,int,int){return {51,52,53};}
float perAxisSunShadowFactor(vec3,int,int=0,int=0){++axisCalls;return .125f;}
vec3 cardinalVoxelSurfaceCenter(ivec2 p,int e,ivec2 z,vec2 offset,ivec2 options,int cardinal){
 if(p.x!=pixel.x||p.y!=pixel.y||e!=encoded||z.x!=trixelCanvasOffsetZ1.x||
    z.y!=trixelCanvasOffsetZ1.y||offset.x!=frameCanvasOffset.x||offset.y!=frameCanvasOffset.y||
    options.x!=voxelRenderOptions.x||options.y!=voxelRenderOptions.y||cardinal!=cardinalIndex)
  std::exit(21);
 ++centerCalls;return surfacePoint;
}
bool selectedShapeAnalyticShadowReceiver(ivec2,int,vec3& p){p={31,32,33};return sphere;}
bool selectedShapeAnalyticShadowReceiver(ivec2 p,int w,int,int,int,int,vec3& out){
 return selectedShapeAnalyticShadowReceiver(p,w,out);
}
float worldSurfaceSunShadowFactor(vec3 p,vec3 n,float depth,vec4 rotation,int=0,int=0){
 ++surfaceCalls;sampledPosition=p;sampledNormal=n;sampledDepth=depth;sampledRotation=rotation;
 return .25f;
}
float worldSunShadowFactor(vec3 p,vec3 n,float depth,int=0,int=0){
 ++legacyCalls;sampledPosition=p;sampledNormal=n;sampledDepth=depth;return .75f;
}
void resetSamples(){idReads=outputWrites=centerCalls=surfaceCalls=legacyCalls=axisCalls=0;}
enum TextureAccess {READ_ONLY};enum TextureFormat {RG32UI};
struct BoundTexture {
 int slot=-1;
 void bindAsImage(int s,TextureAccess,TextureFormat){slot=s;}
} boundIds;
struct {BoundTexture* getTextureEntityIds(){return &boundIds;}} canvasTextures;
bool voxelFaceCoverage_=false,frameUsesFiniteCoverage_=false;
bool statelessParticles=false,gpuParticles=false;
const int RENDER_STATELESS_PARTICLES_TO_TRIXEL=1,RENDER_GPU_PARTICLES_TO_TRIXEL=2,kNullSystemId=0;
int findSystem(int kind){return kind==1?int(statelessParticles):int(gpuParticles);}
struct {vec4 sunBasisU_{{0,0,0},0};int shadowsEnabled_=1;} frameData_;
vec3 sunDir(0,0,1);
namespace IRMath {void buildOrthonormalBasis(vec3,vec3& u,vec3& v){u={1,0,0};v={0,1,0};}}
"""

ROUTE_CHECKS = r"""
int check(bool okay,const char* contract){
 if(!okay){std::fprintf(stderr,"route contract: %s\n",contract);return 1;}return 0;
}
int main(int argc,char** argv){
 const int mutation=argc>1?std::atoi(argv[1]):0;
 applyBindings(mutation);
 if(check(boundIds.slot==2&&shaderEntityBinding==2,"entity image binding"))return 1;
 for(bool coverage:{false,true})for(bool stateless:{false,true})for(bool gpu:{false,true})
 for(int enabled:{0,1}){
  voxelFaceCoverage_=coverage;statelessParticles=stateless;gpuParticles=gpu;
  frameData_.shadowsEnabled_=enabled;frameData_.sunBasisU_.w=2.f;
  applyFrame(mutation);
  if(check(frameData_.sunBasisU_.w==float(coverage&&!stateless&&!gpu),"finite frame flag"))return 1;
 }
 for(float detached:{0.f,1.f})for(float yaw:{0.f,.001f})for(bool winner:{false,true}){
  isDetachedCanvas=detached;residualYaw=yaw;
  const uvec2 tag=tagVoxel(mutation,uvec2(0x12345678u,3u));
  if(check(tag.x==0x12345678u&&(tag.y&0x3ffffu)==3u&&
     decodeEntityIdCardinalVoxel(tag)==(detached==0&&yaw==0),"positive voxel provenance"))return 1;
  for(bool guarded:{false,true})for(bool elected:{false,true}){
   for(int i=0;i<64;++i){distanceScratch[i]=storedDepth;perAxisWinnerIds[i]=elected?7u:8u;}
   publishedId=uvec2(0);publications=0;
   publish(mutation,guarded,winner?storedDepth:storedDepth+1,tag);
   if(check(publications==int(winner&&(!guarded||elected)),"winner-only publication"))return 1;
   if(publications&&check(publishedId.x==tag.x&&publishedId.y==tag.y,
                          "published provenance"))return 1;
  }
 }
 unsigned cases=0;
 for(int enabled:{0,1})for(int axis:{0,1})for(float yaw:{0.f,.00001f})
 for(float finite:{0.f,1.f})for(uint provenance:{0u,1u<<18u,1u<<19u,0xfff00000u})
 for(bool isSphere:{false,true})for(int density:{1,2,4,8,16})
 for(int cardinal=0;cardinal<4;++cardinal){
  shadowsEnabled=enabled;perAxisRoute=axis;residualYaw=yaw;sunBasisU.w=finite;
  publishedId=uvec2(0x1234u,provenance|3u);sphere=isSphere;
  voxelRenderOptions.y=density;cardinalIndex=cardinal;
  // The query spies distinguish world surface depth from scaled, rotated raster depth.
  rawDepth=density*(84+cardinal*17);encoded=rawDepth*8+2;
  resetSamples();route(mutation);
  const bool eligible=enabled&&!axis&&yaw==0&&finite!=0&&(provenance&(1u<<18u));
  const bool shape=IR_SHAPE_RECEIVER&&enabled&&!axis&&!eligible&&sphere;
  if(check(outputWrites==1,"one write and per-axis early return")||
     check(centerCalls==int(eligible),"cardinal eligibility")||
     check(idReads==int(enabled&&!axis&&yaw==0&&finite!=0),"provenance read gating")||
     check(axisCalls==int(enabled&&axis),"per-axis route")||
     check(surfaceCalls==int(eligible||shape),"surface route")||
     check(legacyCalls==int(enabled&&!axis&&!eligible&&!shape),"legacy route"))return 1;
  const float expected=!enabled?1.f:axis?.125f:(eligible||shape)?.25f:.75f;
  if(check(output.xyz.x==expected&&output.w==(axis?0:receiverFace),"shadow output"))return 1;
  if(enabled&&!axis){
   const vec3 expectedPosition=eligible?surfacePoint:shape?vec3(31,32,33):pos3D;
   if(check(same(sampledPosition,expectedPosition)&&same(sampledNormal,normal),"query geometry")||
      check(sampledDepth==(eligible?24.f:float(rawDepth)),"cascade depth units"))return 1;
   if((eligible||shape)&&check(same(sampledRotation.xyz,sunCasterViewToWorld.xyz)&&
      sampledRotation.w==sunCasterViewToWorld.w,"caster quaternion"))return 1;
  }
  ++cases;
 }
 std::printf("route_cases=%u shape_specialization=%d\n",cases,IR_SHAPE_RECEIVER);
 return 0;
}
"""


def routing_harness(backend, shape_receiver):
    shaders = shader_directory(backend)
    common = (shaders / f"ir_iso_common.{backend}").read_text()
    compute = (shaders / f"c_compute_sun_shadow_body.{backend}").read_text()
    producer = (shaders / f"c_voxel_to_trixel_stage_2_body.{backend}").read_text()
    per_axis = re.search(
        r"    if \((?:frameData\.)?perAxisRoute != 0\) \{\s*const int faceId\b.*?\n    \}",
        compute, re.DOTALL).group()
    final = compute[compute.index("    float factor = 1.0;"):compute.rindex("\n}")]
    route = per_axis + "\n" + final
    tag = re.search(r"    if \((?:frameData\.)?isDetachedCanvas <= 0.5.*?\n    \}",
                    producer, re.DOTALL).group()
    writer = functions(producer, "writeColorTap")
    elected_writer = functions(producer, "writeColorTapCardinalWinner")
    helpers = re.search(r"(?:const|constant) uint kEntityIdCardinalVoxelMaskInHighWord = [^;]+;",
                        common).group().replace("constant ", "const ")
    helpers += "\n" + "\n".join(functions(common, name) for name in (
        "decodeEntityIdCardinalVoxel", "encodeEntityIdCardinalVoxel", "pos3DtoDistance"))
    systems = ROOT / "engine/prefabs/irreden/render/systems"
    bake = (systems / "system_bake_sun_shadow_map.hpp").read_text()
    policy = re.search(r"frameUsesFiniteCoverage_ =\s*voxelFaceCoverage_.*?;",
                       bake, re.DOTALL).group()
    stamp = bake.index("frameData_.sunBasisU_.w =")
    basis = re.search(r"frameData_\.sunBasisU_ = [^;]+;", bake)
    frame = bake[stamp:basis.end()]
    system = (systems / "system_compute_sun_shadow.hpp").read_text()
    binding = re.search(r"canvasTextures\.getTextureEntityIds\(\)\s*->bindAsImage\([^;]+;",
                        system).group()

    def adapt(source):
        source = cpp(source).replace("frameData.", "").replace("sunFrameData.", "")
        source = source.replace("uint2", "ivec2").replace("uint4", "uvec4")
        # Entity carriers stay unsigned; coordinate-only uint2 casts become ivec2.
        source = source.replace("ivec2 packedEntityId", "uvec2 packedEntityId")
        source = source.replace("ivec2 packedId", "uvec2 packedId")
        source = source.replace("ivec2 rawId", "uvec2 rawId")
        source = source.replace("ivec2 encodeEntityId", "uvec2 encodeEntityId")
        source = source.replace("return ivec2(packedId.x", "return uvec2(packedId.x")
        source = source.replace("device const atomic_int*", "const int*")
        source = source.replace("device const atomic_uint*", "const uint*")
        source = re.sub(r"texture2d<\w+, access::write>", "Texture", source)
        return source

    route, tag, writer, elected_writer, helpers = map(
        adapt, (route, tag, writer, elected_writer, helpers))
    definitions = {
        "route": ("void", "", route),
        "tagVoxel": ("uvec2", "uvec2 packedEntityId", tag + "\nreturn packedEntityId;"),
        "applyFrame": ("void", "", policy + "\n" + frame),
        "applyBindings": ("void", "", binding),
    }
    tail = (", size, distanceScratch, perAxisWinnerIds, triangleCanvasColors, "
            "triangleCanvasDistances, triangleCanvasEntityIds" if backend == "metal" else "")
    plain_tail = tail.replace(", perAxisWinnerIds", "")
    publish = ("if(guarded)writeColorTapCardinalWinner(pixel,distance,vec4(1,1,1,1),id,7u"
               + tail + ");\nelse writeColorTap(pixel,distance,vec4(1,1,1,1),id"
               + plain_tail + ");")
    definitions["publish"] = ("void", "bool guarded,int distance,uvec2 id", publish)
    variants = {
        "no_yaw_gate": ("route", "residualYaw == 0.0", "true"),
        "no_coverage_gate": ("route", "sunBasisU.w != 0.0", "true"),
        "no_provenance_gate": ("route", "decodeEntityIdCardinalVoxel(",
                               "(true || decodeEntityIdCardinalVoxel("),
        "no_per_axis_return": ("route", "        return;", ""),
        "no_disabled_gate": ("route", "if (shadowsEnabled != 0)", "if (true)"),
        "scaled_cascade_depth": ("route", "pos3DtoDistance(surface)", "float(rawDepth)"),
        "legacy_surface_position": ("route", "Factor(surface, normal", "Factor(pos3D, normal"),
        "wrong_surface_normal": ("route", "Factor(surface, normal", "Factor(surface, vec3(1)"),
        "identity_caster_rotation": (
            "route", "pos3DtoDistance(surface),\n                sunCasterViewToWorld",
            "pos3DtoDistance(surface),\n                vec4(0,0,0,1)"),
        "no_detached_gate": ("tagVoxel", "isDetachedCanvas <= 0.5", "true"),
        "no_producer_yaw_gate": ("tagVoxel", "residualYaw == 0.0", "true"),
        "no_voxel_tag": ("tagVoxel", "encodeEntityIdCardinalVoxel(packedEntityId)",
                         "packedEntityId"),
        "force_finite_frame": ("applyFrame", "frameUsesFiniteCoverage_ ? 1.0f : 0.0f", "1.0f"),
        "disabled_frame_stale": (
            "applyFrame", "frameData_.sunBasisU_.w = frameUsesFiniteCoverage_",
            "if(frameData_.shadowsEnabled_ != 0) "
            "frameData_.sunBasisU_.w = frameUsesFiniteCoverage_"),
        "stateless_particles_finite": (
            "applyFrame", "findSystem(RENDER_STATELESS_PARTICLES_TO_TRIXEL) == kNullSystemId",
            "true"),
        "gpu_particles_finite": (
            "applyFrame", "findSystem(RENDER_GPU_PARTICLES_TO_TRIXEL) == kNullSystemId", "true"),
        "wrong_entity_binding": ("applyBindings", "bindAsImage(2,", "bindAsImage(3,"),
        "no_depth_winner": ("publish", "else writeColorTap(", "else writeColorTapNoDepth("),
        "no_elected_winner": ("publish", "writeColorTapCardinalWinner(",
                              "writeColorTapNoElection("),
    }
    if backend == "metal":
        entity_binding = re.search(
            r"texture2d<uint, access::read> trixelEntityIds \[\[texture\((\d+)\)\]\]",
            compute).group(1)
    else:
        entity_binding = re.search(
            r"layout\(rg32ui, binding = (\d+)\) readonly uniform uimage2D trixelEntityIds",
            compute).group(1)
    generated = (f"const int shaderEntityBinding={entity_binding};\n"
                 + helpers + "\n" + writer + "\n" + elected_writer)
    no_depth = replace_once(writer, "voxelDistance " + ("!=" if backend == "metal" else "==")
                            + " canvasDistance", "false" if backend == "metal" else "true")
    generated += "\n" + no_depth.replace("writeColorTap(", "writeColorTapNoDepth(")
    election_gate = re.search(r"if \((?:atomic_load_explicit\([^;]+?|perAxisWinnerIds\[cell\])"
                              r" !=\s*voxelIndex\)", elected_writer).group()
    no_election = replace_once(elected_writer, election_gate, "if (false)")
    generated += "\n" + no_election.replace("writeColorTapCardinalWinner(",
                                             "writeColorTapNoElection(")
    for name, (result, args, body) in definitions.items():
        generated += f"\n{result} {name}Production({args}){{\n{body}\n}}\n"
        dispatch = f"\n{result} {name}(int mutation" + ("," + args if args else "") + "){\n"
        forwarded = ",".join(arg.split()[-1] for arg in args.split(",")) if args else ""
        for number, (label, (subject, old, new)) in enumerate(variants.items(), 1):
            if subject != name:
                continue
            mutant = replace_once(body, old, new)
            if label == "no_provenance_gate":
                mutant = mutant.replace(").xy)) {", ").xy))) {")
            generated += f"\n{result} mutant{number}({args}){{\n{mutant}\n}}\n"
            dispatch += f"if(mutation=={number})return mutant{number}({forwarded});\n"
        generated += dispatch + f"return {name}Production({forwarded});\n}}\n"
    preamble = PREAMBLE.replace(
        "struct vec4 {vec3 xyz; float w;};",
        "struct vec4 {vec3 xyz;float w;vec4(vec3 a,float b):xyz(a),w(b){}"
        "vec4(float a,float b,float c,float d):xyz(a,b,c),w(d){}};")
    preamble = preamble.replace("ivec4(ivec2 a,int b,int c)",
                                "ivec4(int a,int b,int c,int d):xy(a,b),z(c),w(d){} "
                                "ivec4(ivec2 a,int b,int c)")
    return (preamble + ROUTE_ADAPTER + f"\n#define IR_SHAPE_RECEIVER {shape_receiver}\n"
            + generated + ROUTE_CHECKS), list(variants)


# Staged neighbour texels for the fragment receiver: every unstaged texel is far
# from any owner depth, so only what an arm stages can read as coplanar.
POINT_ADAPTER = r"""
#include <map>
#include <utility>
using uint=unsigned;using uvec2=V2<uint>;
vec2 operator/(vec2 a,float b){return {a.x/b,a.y/b};}
float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
template<class T> bool anyProvenance(T){return true;}
struct Texel {int encoded;uvec2 id;};
std::map<std::pair<int,int>,Texel> staged;
ivec2 stagedSize(4096,4096);
Texel stagedAt(ivec2 p){
 const auto found=staged.find({p.x,p.y});
 return found==staged.end()?Texel{1<<28,uvec2(0u)}:found->second;
}
struct IntTexel {int r;};
struct UintTexel {uvec2 rg;};
struct isampler2D {} stagedDistances;
struct usampler2D {} stagedEntityIds;
ivec2 textureSize(isampler2D,int){return stagedSize;}
IntTexel texelFetch(isampler2D,ivec2 p,int){return {stagedAt(p).encoded};}
UintTexel texelFetch(usampler2D,ivec2 p,int){return {stagedAt(p).id};}
template<class T> struct texture2d {
 uint get_width()const{return uint(stagedSize.x);}
 uint get_height()const{return uint(stagedSize.y);}
 auto read(uvec2 p)const{
  const Texel texel=stagedAt(ivec2(int(p.x),int(p.y)));
  if constexpr(std::is_same<T,int>::value)return IntTexel{texel.encoded};
  else return UintTexel{texel.id};
 }
};
"""

# Arm 0 stages a coplanar continuation on all four in-plane sides, so points past
# the micro-square keep the ray/plane intersection. Arms 1-6 each break one
# coplanarity condition (absent, slot, flip, one plane off, provenance, off
# canvas), so the point stops at the owner micro-square on that axis.
POINT_CHECKS = r"""
     const int eu=axis==0?1:0,ev=axis==2?1:2;
     // Independent iso steps of the unit view axes.
     const ivec2 viewIso[3]={ivec2(-1,-1),ivec2(1,-1),ivec2(0,2)};
     // The producer publishes the next micro-square along each in-plane axis
     // one iso step on, at the same half, one depth unit deeper.
     if(i==0)for(int k=0;k<2;++k){
      const int nu=u+(k==0),nv=v+(k==1);
      if(nu>=samples||nv>=samples)continue;
      const std::vector<Tap> saved=taps;const int savedZ=zIdx;
      const bool savedBoth=bothPolaritiesExposed;
      zIdx=nu*density+nv;bothPolaritiesExposed=false;taps.clear();produce();
      const Tap next=taps[0];taps=saved;zIdx=savedZ;bothPolaritiesExposed=savedBoth;
      const ivec2 step=viewIso[k==0?eu:ev];
      const vec3 actualNext(next.pixel.x,next.pixel.y,next.encoded);
      const vec3 expectedNext(taps[i].pixel.x+step.x,taps[i].pixel.y+step.y,taps[i].encoded+8);
      if(!same(actualNext,expectedNext))
       return fail("coplanar neighbour publication",actualNext,expectedNext);
     }
     for(int arm=0;arm<7;++arm){
      staged.clear();stagedSize=ivec2(arm==6?0:4096,arm==6?0:4096);
      if(arm!=1)for(int k=0;k<2;++k)for(int d:{-1,1}){
       const ivec2 step=viewIso[k==0?eu:ev];
       int encoded=taps[i].encoded+8*d;
       uint high=kEntityIdCardinalVoxelMaskInHighWord|3u;
       if(arm==2)encoded=(encoded&~3)|((encoded&3)+1)%3;
       if(arm==3)encoded^=4;
       if(arm==4)encoded+=8;
       if(arm==5)high=3u;
       staged[{taps[i].pixel.x+step.x*d,taps[i].pixel.y+step.y*d}]={encoded,uvec2(0x1234u,high)};
      }
      for(float fu : {-.9f,-.49f,-.23f,0.f,.19f,.49f,.9f})
      for(float fv : {-.9f,-.49f,0.f,.49f,.9f}) {
       const float cu=arm==0?fu:std::max(-.5f,std::min(.5f,fu));
       const float cv=arm==0?fv:std::max(-.5f,std::min(.5f,fv));
       vec3 rayPoint=expected,expectedPoint=expected;
       for(int w=0;w<3;++w){
        rayPoint[w]+=(basis[cardinal][w][eu]*fu+basis[cardinal][w][ev]*fv)/samples;
        expectedPoint[w]+=(basis[cardinal][w][eu]*cu+basis[cardinal][w][ev]*cv)/samples;
       }
       // Project the independent cube-face ray point to a continuous canvas query.
       double view[3]={0,0,0};
       for(int a=0;a<3;++a)for(int w=0;w<3;++w)
        view[a]+=basis[cardinal][w][a]*rayPoint[w];
       const vec2 query(
        float((-view[0]+view[1])*samples+trixelCanvasOffsetZ1.x+
              std::floor(frameCanvasOffset.x*samples)+1),
        float((-view[0]-view[1]+2*view[2])*samples+trixelCanvasOffsetZ1.y+
              std::floor(frameCanvasOffset.y*samples)+1));
       const vec3 actualPoint=cardinalVoxelSurfacePoint(query,taps[i].pixel,taps[i].encoded,
         trixelCanvasOffsetZ1,frameCanvasOffset,voxelRenderOptions,cardinal,
         STAGED_TEXTURES);
       if(std::abs(actualPoint.x-expectedPoint.x)>8.e-5f ||
          std::abs(actualPoint.y-expectedPoint.y)>8.e-5f ||
          std::abs(actualPoint.z-expectedPoint.z)>8.e-5f){
        if(arm==0)return fail("fragment plane intersection",actualPoint,expectedPoint);
        char label[48];std::snprintf(label,sizeof label,"micro-square edge limit arm=%d",arm);
        return fail(label,actualPoint,expectedPoint);
       }
      }
     }
"""


@unittest.skipUnless(COMPILER, "shader controls require a C++ compiler")
class CardinalVoxelReceiverTest(unittest.TestCase):
    def check_harness(self, backend, source, mutations, rejection):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            source_path, executable = path / "receiver.cpp", path / "receiver"
            source_path.write_text(source)
            build = subprocess.run(
                [COMPILER, "-std=c++17", "-O2", str(source_path), "-o", str(executable)],
                capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            print(f"{backend}: {run.stdout}", end="")
            for number, mutation in enumerate(mutations, 1):
                with self.subTest(mutation=mutation):
                    rejected = subprocess.run([str(executable), str(number)],
                                              capture_output=True, text=True)
                    self.assertNotEqual(rejected.returncode, 0, mutation)
                    self.assertIn(rejection, rejected.stderr)

    def test_compute_routes_and_winner_provenance(self):
        for backend in ("glsl", "metal"):
            for shape_receiver in (0, 1):
                with self.subTest(backend=backend, shape_receiver=shape_receiver):
                    source, mutations = routing_harness(backend, shape_receiver)
                    self.check_harness(backend, source, mutations, "route contract:")

    def test_fragment_ray_intersects_the_published_plane(self):
        for backend in ("glsl", "metal"):
            with self.subTest(backend=backend):
                source, _ = harness(backend)
                receiver = (shader_directory(backend) /
                            f"ir_cardinal_voxel_receiver.{backend}").read_text()
                point = cpp("\n".join(functions(receiver, name) for name in (
                    "cardinalVoxelCoplanarNeighbour", "cardinalVoxelEdgeLimit",
                    "cardinalVoxelSurfacePoint")))
                point = replace_once(point, "point += vec3(dot(center - point, axis));",
                                     "point = point + vec3(dot(center - point, axis));")
                point = point.replace("uint2", "uvec2")
                common = (shader_directory(backend) / f"ir_iso_common.{backend}").read_text()
                provenance = re.search(
                    r"(?:const|constant) uint kEntityIdCardinalVoxelMaskInHighWord = [^;]+;",
                    common).group().replace("constant ", "const ")
                provenance += "\n" + cpp(functions(common, "decodeEntityIdCardinalVoxel"))
                provenance = provenance.replace("uint2", "uvec2")
                textures = ("texture2d<int>{},texture2d<uint>{}" if backend == "metal"
                            else "stagedDistances,stagedEntityIds")
                prelude = (POINT_ADAPTER + f"#define STAGED_TEXTURES {textures}\n"
                           + provenance + "\n")
                source = source.replace(CHECKS, "@POINT@" + CHECKS.replace(
                    "     const float worldDepth=", POINT_CHECKS + "     const float worldDepth="))
                variants = {
                    "production": (None, None, None),
                    "no_pixel_center": ("- vec2(1.0)) / density", ") / density",
                                        "fragment plane intersection"),
                    "off_plane": ("dot(center - point, axis)", "0.0",
                                  "fragment plane intersection"),
                    "unconditional_clamp": ("if (cardinalVoxelCoplanarNeighbour(",
                                            "if (false && cardinalVoxelCoplanarNeighbour(",
                                            "fragment plane intersection"),
                    "opposite_neighbour": ("ownerPixel + isoStep", "ownerPixel - isoStep",
                                           "fragment plane intersection"),
                    "unconditional_extrapolation": (
                        "if (abs(s) <= halfExtent + 1.0e-4) return point;", "return point;",
                        "micro-square edge limit arm=1"),
                    "ignores_slot": ("decodeSlot(neighbour) == decodeSlot(encoded) &&", "",
                                     "micro-square edge limit arm=2"),
                    "ignores_flip": (
                        "decodeFlipSingle(neighbour) == decodeFlipSingle(encoded) &&", "",
                        "micro-square edge limit arm=3"),
                    "ignores_depth": (
                        "decodeDepthSingle(neighbour) == decodeDepthSingle(encoded) + direction &&",
                        "", "micro-square edge limit arm=4"),
                    "ignores_provenance": ("decodeEntityIdCardinalVoxel(", "anyProvenance(",
                                           "micro-square edge limit arm=5"),
                    "no_canvas_bounds": (
                        "if (texel.x < 0 || texel.y < 0 || texel.x >= size.x || "
                        "texel.y >= size.y) return false;", "",
                        "micro-square edge limit arm=6"),
                }
                for name, (old, new, rejection) in variants.items():
                    mutant = point if old is None else replace_once(point, old, new)
                    with self.subTest(variant=name), \
                            tempfile.TemporaryDirectory() as directory:
                        path = Path(directory)
                        (path / "point.cpp").write_text(
                            source.replace("@POINT@", prelude + mutant + "\n"))
                        build = subprocess.run([COMPILER, "-std=c++17", "-O2",
                                                str(path / "point.cpp"), "-o", str(path / "point")],
                                               capture_output=True, text=True)
                        self.assertEqual(build.returncode, 0, build.stderr)
                        run = subprocess.run([str(path / "point")], capture_output=True, text=True)
                        if old is None:
                            self.assertEqual(run.returncode, 0, run.stderr)
                        else:
                            self.assertNotEqual(run.returncode, 0, name)
                            self.assertIn(rejection, run.stderr)

    def test_production_publication_to_finite_face_center(self):
        for backend in ("glsl", "metal"):
            with self.subTest(backend=backend):
                source, mutations = harness(backend)
                self.check_harness(backend, source, mutations, "cardinal face vertices")


if __name__ == "__main__":
    unittest.main()

"""Execute conservative visibility filtering and fragment side-effect ordering.

Depth-format oracles use independent D24 quantization and raw D32 comparisons.
Host atomics are serial adapters; GPU scheduling and raster parity need native tests.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_render_per_axis_caster_position import COMPILER, PREAMBLE, ROOT, functions
from test_render_per_axis_receiver import host
from test_render_shape_surface_lighting import function_body

NUMERIC = r"""
unsigned numericChecks=0,d24Ties=0;
uint d24(float depth,int rounding){
    const double value=std::clamp(double(depth),0.,1.)*16777215.;
    return uint(rounding==0?std::floor(value):rounding==1?std::floor(value+.5):std::ceil(value));
}
int pairCheck(float a,float b){
    const uint first=scatterVisibilityCode(a),second=scatterVisibilityCode(b);
    const uint winner=std::min(first,second);
    for(int rounding=0;rounding<3;++rounding){
        const uint qa=d24(a,rounding),qb=d24(b,rounding);
        d24Ties+=qa==qb && first!=second;
        if((qa<=qb&&scatterVisibilityReject(first,winner))||
           (qb<=qa&&scatterVisibilityReject(second,winner))){
            std::fprintf(stderr,"D24 winner lost\n");return 1;
        }
    }
    if((a<=b&&scatterVisibilityReject(first,winner))||
       (b<=a&&scatterVisibilityReject(second,winner))){
        std::fprintf(stderr,"D32 winner lost\n");return 2;
    }
    ++numericChecks;
    return 0;
}
int numericControls(){
    if(scatterVisibilityCode(-1)!=0||scatterVisibilityCode(0)!=0||
       scatterVisibilityCode(.5f)!=8388608u||scatterVisibilityCode(1)!=16777216u||
       scatterVisibilityCode(2)!=16777216u)return 3;
    uint state=0x39a01bcdu;
    for(uint bin=0;bin<4096;++bin){
        state=1664525u*state+1013904223u;
        const uint cell=state%16777216u;
        const float center=float(double(cell)/16777215.);
        const float lo=std::nextafter(center,0.f),hi=std::nextafter(center,1.f);
        const float values[]={std::nextafter(lo,0.f),lo,center,hi,std::nextafter(hi,1.f)};
        for(float a:values)for(float b:values)if(pairCheck(a,b))return 1;
        if(pairCheck(float(double(state)/4294967295.),float(double(cell)/16777215.)))return 1;
    }
    const float values[]={0.f,std::numeric_limits<float>::denorm_min(),.5f,
        std::nextafter(.5f,0.f),std::nextafter(.5f,1.f),std::nextafter(1.f,0.f),1.f};
    for(float a:values)for(float b:values)if(pairCheck(a,b))return 1;
    if(!d24Ties)return 4;
    for(uint winner:{0u,1u,8388608u,16777214u})for(uint delta:{0u,1u,2u}){
        if(scatterVisibilityReject(winner+delta,winner)){
            std::fprintf(stderr,"two-code halo lost\n");return 5;
        }
    }
    if(!scatterVisibilityReject(103,100)||scatterVisibilityReject(99,100)||
       scatterVisibilityReject(123,0xffffffffu)){
        std::fprintf(stderr,"far rejection or unsigned ordering\n");return 6;
    }
    std::printf("depth_pairs=%u d24_cross_code_ties=%u\n",numericChecks,d24Ties);
    return 0;
}
"""

ADAPTER = r"""
#include <array>
#include <limits>
using uint=unsigned;
using bvec2=V2<bool>;
template<class T>bvec2 lessThan(V2<T> a,V2<T> b){return {a.x<b.x,a.y<b.y};}
template<class T>bvec2 greaterThanEqual(V2<T> a,V2<T> b){return {a.x>=b.x,a.y>=b.y};}
template<class T>bvec2 operator<(V2<T> a,V2<T> b){return lessThan(a,b);}
template<class T>bvec2 operator>=(V2<T> a,V2<T> b){return greaterThanEqual(a,b);}
bvec2 greaterThan(vec2 a,vec2 b){return {a.x>b.x,a.y>b.y};}
bvec2 operator>(vec2 a,vec2 b){return greaterThan(a,b);}
bool any(bvec2 a){return a.x||a.y;}
vec2 operator-(vec2 a){return {-a.x,-a.y};}
vec2 max(vec2 a,vec2 b){return {std::max(a.x,b.x),std::max(a.y,b.y)};}
vec2 min(vec2 a,vec2 b){return {std::min(a.x,b.x),std::min(a.y,b.y)};}
vec2 mix(vec2 a,vec2 b,vec2 t){return {a.x+(b.x-a.x)*t.x,a.y+(b.y-a.y)*t.y};}
vec2 step(float edge,vec2 v){return {v.x<edge?0.f:1.f,v.y<edge?0.f:1.f};}
using bool2=bvec2;
float max(float a,float b){return std::max(a,b);}
float clamp(float d,float a,float b){return std::clamp(d,a,b);}
struct Input {
    vec4 color{1,1,1,1};
    vec2 quadParam{.5f,.5f},position{2.5f,1.5f},depthSlope{0,0};
    ivec3 visibilityExtent{4,3,1};
    vec3 faceOrigin{0,0,0};int faceId=0;ivec2 ownerPixel{-1,-1};
    float depth=.25f,cellTieOffset=0;
};
struct Discard {};
void discard_fragment(){throw Discard{};}
float outputDepth=0;
vec4 outputColor;
vec3 sampledPosition;
int lightingCalls=0,shadowCalls=0,minCalls=0,addCalls=0;
std::array<uint,15> visibilityCodes;
vec2 clamp(vec2 p,vec2 low,vec2 high){
    return {std::clamp(p.x,low.x,high.x),std::clamp(p.y,low.y,high.y)};
}
vec3 faceOutwardNormal6(int){return {0,0,-1};}
template<class... Args>vec3 worldSurfaceLighting(vec3,float,vec3 position,Args...){
    ++lightingCalls;sampledPosition=position;return {.25f,.5f,.75f};
}
float pos3DtoDistance(vec3){return 0.5f;}
template<class... Args>float worldSurfaceSunShadowFactor(vec3 position,Args...){
    ++shadowCalls;sampledPosition=position;return 0.5f;
}
struct AOValue {float r;};
struct AOTexture {AOValue read(ivec2){return {.5f};}} surfaceAO;
AOValue texelFetch(AOTexture,ivec2,int){return {.5f};}
int lighting=0,volumeParams=0,sunDepthBuf=0,lights=0,paletteLUT=0,lightVolume=0,lightVolumeId=0;
int shadowsEnabled=1;
vec4 sunCasterViewToWorld{0,0,0,1};
struct {int shadowsEnabled=1;vec4 sunCasterViewToWorld{0,0,0,1};} sunFrameData;
constexpr int memory_order_relaxed=0;
uint atomicMin(uint& slot,uint value){
    ++minCalls;uint old=slot;slot=std::min(slot,value);return old;
}
uint atomicAdd(uint& slot,uint value){++addCalls;uint old=slot;slot+=value;return old;}
uint atomic_fetch_min_explicit(uint* slot,uint value,int){return atomicMin(*slot,value);}
uint atomic_fetch_add_explicit(uint* slot,uint value,int){return atomicAdd(*slot,value);}
uint atomic_load_explicit(uint* slot,int){return *slot;}
using Fragment=float(*)(Input);
bool run(Fragment fragment,Input in){try{fragment(in);return true;}catch(Discard){return false;}}
void reset(){
    visibilityCodes.fill(0xffffffffu);
    lightingCalls=shadowCalls=minCalls=addCalls=0;outputDepth=-999;
}
"""

EDGE_TIE_ADAPTER = r"""
using bvec2=V2<bool>;
using bool2=bvec2;
bool any(bvec2 a){return a.x||a.y;}
vec2 operator-(vec2 a){return {-a.x,-a.y};}
vec2 min(vec2 a,vec2 b){return {std::min(a.x,b.x),std::min(a.y,b.y)};}
vec2 mix(vec2 a,vec2 b,vec2 t){return {a.x+(b.x-a.x)*t.x,a.y+(b.y-a.y)*t.y};}
vec2 step(float edge,vec2 v){return {v.x<edge?0.f:1.f,v.y<edge?0.f:1.f};}
"""

EDGE_TIE_CHECKS = r"""
int fail(const char* label){std::fprintf(stderr,"%s\n",label);return 1;}
int check(float depth,vec2 slope,vec2 q,float expected,const char* label){
    return scatterEdgeTieDepth(depth,slope,q)==expected?0:fail(label);
}
int main(){
    const float band=kScatterCellTieBand;
    const vec2 receding(0x1p-12f,0x1p-12f),advancing(-0x1p-12f,-0x1p-12f);
    const float edge=0x1p-10f,gate=kScatterEdgeTieWidth;
    for(float depth:{.25f,.5f,.75f}){
        const float yield=depth+band;
        // Interior fragments and fragments exactly at the gate keep their depth.
        for(vec2 q:{vec2(.5f,.5f),vec2(gate,.5f),vec2(1.f-gate,.5f),vec2(.5f,gate),
                    vec2(.5f,1.f-gate)})
            for(vec2 slope:{receding,advancing})
                if(check(depth,slope,q,depth,"interior moved"))return 1;
        // Crossing a low edge toward the interior follows +slope, a high edge -slope.
        if(check(depth,receding,vec2(edge,.5f),yield,"low u edge kept"))return 1;
        if(check(depth,receding,vec2(.5f,edge),yield,"low v edge kept"))return 1;
        if(check(depth,advancing,vec2(1.f-edge,.5f),yield,"high u edge kept"))return 1;
        if(check(depth,advancing,vec2(.5f,1.f-edge),yield,"high v edge kept"))return 1;
        if(check(depth,advancing,vec2(edge,.5f),depth,"advancing low edge yielded"))return 1;
        if(check(depth,receding,vec2(1.f-edge,.5f),depth,"advancing high edge yielded"))return 1;
        // Coverage snapped a hair outside the face still counts as its edge.
        if(check(depth,receding,vec2(-edge,.5f),yield,"outside low edge kept"))return 1;
        if(check(depth,advancing,vec2(1.f+edge,.5f),yield,"outside high edge kept"))return 1;
        // One band moves the final depth up by exactly one band index.
        const float kept=scatterFinalDepth(depth,0.f);
        const float yielded=scatterFinalDepth(yield,0.f);
        if(std::floor(yielded/band)!=std::floor(kept/band)+1.f)return fail("yield is not one band");
        // A yielded edge still beats a face two bands farther.
        if(!(yielded>kept)||!(yielded<scatterFinalDepth(depth+2.f*band,0.f)))
            return fail("yield out of order");
    }
    std::printf("edge tie checks passed\n");
    return 0;
}
"""

COSORT_CHECKS = r"""
const int kMin=-65535,kMax=65535;
int fail(const char* label){std::fprintf(stderr,"%s\n",label);return 1;}
float normalizeDistance(int distance){return float(distance-kMin)/float(kMax-kMin);}
float voxelDepth(vec3 corner,float yaw,int sub,int tieCode){
    const float key=scatterCompositeDepthKey(corner,yaw,float(kDepthEncodeShift*sub));
    const float depth=(key-float(kMin))/float(kMax-kMin);
    return scatterFinalDepth(scatterEdgeTieDepth(depth,vec2(0.f,0.f),vec2(.5f,.5f)),
                             float(tieCode)*kScatterCellTieStep);
}
float sdfDepth(int depth,int slot,int flip){
    return normalizeDistance(compositeCanvasDistance(encodeDepthWithFace(depth,slot,flip),true));
}
int main(){
    int pairs=0;
    for(int depth=-70000;depth<=70000;depth+=997)
        for(int slot=0;slot<3;++slot)for(int flip:{0,1}){
            const int stored=encodeDepthWithFace(depth,slot,flip);
            if(compositeCanvasDistance(stored,false)!=stored)return fail("cardinal key changed");
            if(compositeCanvasDistance(stored,true)!=encodeDepthWithFace(depth,0,0))
                return fail("face bits kept");
        }
    for(float yaw:{.15f,.4f,.7f,-.3f})for(int sub:{1,2,4,8})for(int k=-40;k<=40;++k)
    for(int slot=0;slot<3;++slot)for(int flip:{0,1})for(int cell=0;cell<4;++cell){
        // On the cell anchor's vertical line the yawed depth is k at every yaw,
        // so the SDF's integer depth k * sub is the voxel corner's exact twin.
        const vec3 corner(.5f,.5f,float(k)+.5f);
        const float voxel=voxelDepth(corner,yaw,sub,((flip?3:slot)<<2)|cell);
        const float sdf=sdfDepth(k*sub,slot,flip);
        if(std::abs(voxel-sdf)>kScatterCellTieBand)return fail("coincident faces split");
        if(!(sdfDepth(k*sub-1,slot,flip)<voxel)||!(sdfDepth(k*sub+1,slot,flip)>voxel))
            return fail("one-step neighbour out of order");
        ++pairs;
    }
    std::printf("coincident sdf/voxel face pairs=%d\n",pairs);
    return 0;
}
"""

ROUTING = r"""
int main(int argc,char** argv){
    Fragment prepass=fragmentPrepass,replay=fragmentReplay;
    if(argc>1&&std::strcmp(argv[1],"late-rejection")==0)replay=fragmentLate;
    if(argc>1&&std::strcmp(argv[1],"late-alpha")==0)prepass=fragmentLateAlpha;
    if(argc>1&&std::strcmp(argv[1],"wrong-pass-bit")==0)prepass=fragmentWrongPass;
    if(argc>1&&std::strcmp(argv[1],"stats-pass-bit")==0){
        prepass=fragmentWrongStatsPrepass;replay=fragmentWrongStatsReplay;
    }
    Input in;
    reset();in.color.w=.09f;
    if(run(prepass,in)||minCalls||addCalls||lightingCalls)return 1;
    in.color.w=1;
    for(vec2 position:{vec2(-1.5f,1.5f),vec2(4.5f,1.5f),vec2(2.5f,3.5f)}){
        reset();in.position=position;
        if(run(prepass,in)||minCalls||addCalls||lightingCalls)return 3;
    }
    in.position=vec2(2.5f,1.5f);
    for(vec2 q:{vec2(.5f,.5f),vec2(-0x1p-10f,.5f),vec2(1.0f+0x1p-10f,1.0f+0x1p-10f)}){
        in.quadParam=q;
        const float expected=scatterFinalDepth(in.depth,in.cellTieOffset);
        const vec3 nearest(-.5f,std::clamp(q.x,0.f,1.f)-.5f,
                           std::clamp(q.y,0.f,1.f)-.5f);
        reset();
        if(!run(fragmentLegacy,in)||lightingCalls!=1||outputDepth!=expected||
           !same(sampledPosition,nearest))return 4;
        reset();
        if(!run(fragmentShadow,in)||shadowCalls!=1||lightingCalls||outputDepth!=expected||
           !same(sampledPosition,nearest))return 4;
        reset();
        if(run(prepass,in)||minCalls!=1||lightingCalls||visibilityCodes[6]!=uint(expected*16777216.f)||
           visibilityCodes[5]!=0xffffffffu||visibilityCodes[12]!=0)return 5;
        lightingCalls=0;
        if(!run(replay,in)||lightingCalls!=1||outputDepth!=expected||
           !same(sampledPosition,nearest)||visibilityCodes[13]!=0)return 6;
        in.depth=.75f;lightingCalls=0;
        if(run(replay,in)||lightingCalls||visibilityCodes[14]!=0){
            std::fprintf(stderr,"hidden fragment lit before rejection\n");return 7;
        }
        in.depth=.25f;
    }
    in=Input{};
    for(bool reverse:{false,true}){
        reset();
        for(float depth:reverse?std::array<float,2>{.25f,.75f}:std::array<float,2>{.75f,.25f}){
            in.depth=depth;if(run(prepass,in))return 8;
        }
        if(visibilityCodes[6]!=4194304u||minCalls!=2||lightingCalls)return 8;
    }
    in=Input{};reset();run(prepass,in);in.cellTieOffset=0x1p-23f;
    if(!run(replay,in)||lightingCalls!=1)return 9;
    for(float depth:{-1.f,2.f}){
        in=Input{};in.depth=depth;reset();
        if(run(prepass,in)||minCalls||visibilityCodes[6]!=0xffffffffu)return 10;
        if(!run(replay,in)||lightingCalls!=1)return 10;
    }
    in=Input{};in.visibilityExtent.z=0;reset();run(prepass,in);
    if(addCalls||!run(replay,in)||addCalls||lightingCalls!=1)return 11;
    std::printf("fragment depth/prepass/replay ordering passed\n");
}
"""


def conditional_block(source):
    start = re.search(r"^#if\s+IR_PER_AXIS_VISIBILITY\s*$", source, re.MULTILINE).start()
    depth = 0
    for directive in re.finditer(r"^#(if|ifdef|ifndef|endif)\b[^\n]*",
                                 source[start:], re.MULTILINE):
        depth += -1 if directive[1] == "endif" else 1
        if depth == 0:
            return source[start:start + directive.end()]
    raise ValueError("unclosed visibility block")


def fragment_body(source, suffix):
    body = host(function_body(source, "main" if suffix == "glsl" else "IR_PER_AXIS_FRAGMENT_NAME"))
    fields = ("Color", "QuadParam", "Depth", "DepthSlope", "CellTieOffset",
              "VisibilityExtent", "FaceOrigin", "FaceId", "OwnerPixel")
    for field in sorted(fields, key=len, reverse=True):
        body = body.replace("v" + field, "in." + field[0].lower() + field[1:])
    body = body.replace("in.visibilityExtent.xy",
                        "ivec2(in.visibilityExtent.x,in.visibilityExtent.y)")
    body = body.replace("gl_FragCoord.xy", "in.position").replace("in.position.xy", "in.position")
    body = body.replace("uint2(", "ivec2(")
    body = body.replace("in.color.a", "in.color.w").replace("in.color.rgb", "in.color.rgb()")
    body = body.replace("FragmentOut out;", "").replace("out.color", "outputColor")
    body = body.replace("FragColor", "outputColor").replace("gl_FragDepth", "outputDepth")
    body = body.replace("out.depth", "outputDepth").replace("return out;", "return outputDepth;")
    return re.sub(r"\bdiscard\s*;", "discard_fragment();", body)


def routing_harness(root, suffix):
    body = fragment_body((root / f"ir_peraxis_scatter_fragment_body.{suffix}").read_text(), suffix)
    visibility = conditional_block(body)
    late = body.replace(visibility, "")
    late = late.replace("outputDepth =", visibility + "\noutputDepth =")
    variants = [("fragmentLegacy", 0, body), ("fragmentVisible", 1, body),
                ("fragmentLateBody", 1, late)]
    guard = re.search(r"if\s*\(in\.color\.w\s*<[^)]+\)\s*\{[^{}]+\}", body)[0]
    moved = body.replace(guard, "").replace(visibility, visibility + "\n" + guard)
    variants.append(("fragmentLateAlphaBody", 1, moved))
    wrong_pass, pass_count = re.subn(r"in\.visibilityExtent\.z\s*&\s*2u?\b",
                                   "in.visibilityExtent.z & 1", body)
    wrong_stats, stats_count = re.subn(r"in\.visibilityExtent\.z\s*&\s*1u?\b",
                                     "in.visibilityExtent.z & 2", body)
    if pass_count != 1 or stats_count != 2:
        raise ValueError("expected one runtime pass test and two stats tests")
    variants.extend((("fragmentWrongPassBody", 1, wrong_pass),
                     ("fragmentWrongStatsBody", 1, wrong_stats)))
    generated = "#define IR_PER_AXIS_SURFACE_SHADOW 0\n#define IR_PER_AXIS_SURFACE_LIGHTING 1\n"
    for name, mode, candidate in variants:
        generated += (f"#define IR_PER_AXIS_VISIBILITY {mode}\nfloat {name}(Input in){{\n"
                      + candidate + "\nreturn outputDepth;\n}\n#undef IR_PER_AXIS_VISIBILITY\n")
    for name, target, prepass in (
        ("fragmentPrepass", "fragmentVisible", True),
        ("fragmentReplay", "fragmentVisible", False),
        ("fragmentLate", "fragmentLateBody", False),
        ("fragmentLateAlpha", "fragmentLateAlphaBody", True),
        ("fragmentWrongPass", "fragmentWrongPassBody", True),
        ("fragmentWrongStatsPrepass", "fragmentWrongStatsBody", True),
        ("fragmentWrongStatsReplay", "fragmentWrongStatsBody", False),
    ):
        generated += (f"float {name}(Input in){{in.visibilityExtent.z="
                      f"(in.visibilityExtent.z & ~2) | {2 if prepass else 0};"
                      f"return {target}(in);}}\n")
    generated += ("#undef IR_PER_AXIS_SURFACE_LIGHTING\n"
                  "#define IR_PER_AXIS_SURFACE_LIGHTING 0\n"
                  "#undef IR_PER_AXIS_SURFACE_SHADOW\n"
                  "#define IR_PER_AXIS_SURFACE_SHADOW 1\n"
                  "#define IR_PER_AXIS_VISIBILITY 0\n"
                  "float fragmentShadow(Input in){\n" + body
                  + "\nreturn outputDepth;\n}\n#undef IR_PER_AXIS_VISIBILITY\n")
    constants, depth = depth_helpers(root, suffix)
    common = (root / f"ir_iso_common.{suffix}").read_text()
    anchor = re.search(r"(?:const|constant) (?:vec3|float3) kVoxelRasterCellAnchor\s*=[^;]+;",
                       common)[0]
    axes = functions(common, "faceInPlaneUnitAxes")
    surface = (root / f"ir_per_axis_surface.{suffix}").read_text()
    closest = "\n".join(functions(surface, name) for name in (
        "perAxisFaceSurfacePoint", "perAxisFaceClosestPoint"))
    adapter = PREAMBLE.replace("struct vec4 {vec3 xyz; float w;};", r"""
struct vec4 {
    float x,y,z,w;
    vec4(float a=0):x(a),y(a),z(a),w(a){}
    vec4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}
    vec4(vec3 v,float d):x(v.x),y(v.y),z(v.z),w(d){}
    vec3 rgb()const{return {x,y,z};}
};
""")
    return adapter + ADAPTER + host(constants + depth + anchor + axes + closest) \
        + visibility_helpers(root, suffix) \
        + "\n" + generated + ROUTING


def cosort_helpers(root, suffix):
    common = (root / f"ir_iso_common.{suffix}").read_text()
    constants = "\n".join(
        re.search(pattern, common)[0] for pattern in (
            r"(?:const|constant) int kDepthEncodeShift\s*=[^;]+;",
            r"(?:const|constant) (?:vec3|float3) kVoxelRasterCellAnchor\s*=[^;]+;"))
    names = ("encodeDepthWithFace", "decodeDepthSingle", "compositeCanvasDistance",
             "yawedIsoDistance", "yawedIsoDistanceCellAnchor", "scatterCompositeDepthKey")
    scatter_constants, depth = depth_helpers(root, suffix)
    return host(constants + "\n" + scatter_constants + "\n"
                + "\n".join(functions(common, name) for name in names) + "\n" + depth)


def depth_helpers(root, suffix):
    common = (root / f"ir_iso_common.{suffix}").read_text()
    constants = "\n".join(
        re.search(r"(?:const|constant) float " + name + r"\s*=[^;]+;", common)[0]
        for name in ("kScatterCellTieStep", "kScatterCellTieBand", "kScatterEdgeTieWidth"))
    source = (root / f"ir_scatter_depth.{suffix}").read_text()
    depth = "\n".join(functions(source, name)
                      for name in ("scatterFinalDepth", "scatterEdgeTieDepth"))
    return constants, depth


def visibility_helpers(root, suffix):
    source = (root / f"ir_scatter_visibility.{suffix}").read_text()
    return host("\n".join(functions(source, name) for name in (
        "scatterVisibilityCode", "scatterVisibilityReject")))


@unittest.skipUnless(COMPILER, "scatter visibility controls require a C++ compiler")
class ScatterVisibilityTest(unittest.TestCase):
    def check_variants(self, suffix, variants, basename, program):
        """The production source passes; every regex-mutated variant must fail."""
        for name, candidate in variants.items():
            with (self.subTest(backend=suffix, variant=name),
                  tempfile.TemporaryDirectory() as tmp):
                if name != "production":
                    self.assertNotEqual(candidate, variants["production"])
                path = Path(tmp)
                cpp, executable = path / f"{basename}.cpp", path / basename
                cpp.write_text(program(candidate))
                build = subprocess.run([COMPILER, "-std=c++17", "-O2", str(cpp),
                                        "-o", str(executable)], capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stderr)
                run = subprocess.run([str(executable)], capture_output=True, text=True)
                if name == "production":
                    self.assertEqual(run.returncode, 0, run.stderr)
                    print(suffix, run.stdout.strip())
                else:
                    self.assertNotEqual(run.returncode, 0)

    def test_fragment_prepass_and_replay_order(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            with self.subTest(backend=suffix), tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp)
                cpp, executable = path / "routing.cpp", path / "routing"
                cpp.write_text(routing_harness(ROOT / "engine/render/src/shaders" / folder, suffix))
                build = subprocess.run([COMPILER, "-std=c++17", "-O2", str(cpp),
                                        "-o", str(executable)], capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stderr)
                for variant, expected in (("production", 0), ("late-rejection", 7),
                                          ("late-alpha", 1),
                                          ("wrong-pass-bit", 11), ("stats-pass-bit", 6)):
                    with self.subTest(variant=variant):
                        run = subprocess.run([str(executable), variant],
                                             capture_output=True, text=True)
                        self.assertEqual(run.returncode, expected, run.stderr)

    def test_depth_format_winners_and_rejection(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            root = ROOT / "engine/render/src/shaders" / folder
            helpers = visibility_helpers(root, suffix)
            variants = {
                "production": helpers,
                "no-rejection": re.sub(r"return\s+code\s*>[^;]+;", "return false;", helpers),
                "too-tight": re.sub(r"code\s*-\s*winner\s*>\s*2u", "code - winner > 0u", helpers),
                "unsigned-wrap": re.sub(r"code\s*>\s*winner\s*&&", "", helpers),
            }
            self.check_variants(suffix, variants, "visibility", lambda candidate: (
                "#include <algorithm>\n#include <cmath>\n#include <cstdio>\n"
                "#include <limits>\nusing uint=unsigned;\n"
                "float clamp(float d,float a,float b){return std::clamp(d,a,b);}\n"
                + candidate + NUMERIC + "\nint main(){return numericControls();}\n"))

    def test_edge_tie_yields_receding_face(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            constants, depth = depth_helpers(ROOT / "engine/render/src/shaders" / folder, suffix)
            helpers = host(constants + depth)
            variants = {
                "production": helpers,
                "slope-blind": re.sub(r"&&\s*inwardSlope\.x\s*>\s*0\.0f?", "", helpers,
                                      count=1),
                "outward": re.sub(r"mix\(depthSlope,\s*-depthSlope,",
                                  "mix(-depthSlope, depthSlope,", helpers, count=1),
                "ungated": re.sub(r"edgeDistance\.x\s*<\s*kScatterEdgeTieWidth\s*&&", "", helpers,
                                  count=1),
            }
            self.check_variants(suffix, variants, "edge_tie", lambda candidate:
                                PREAMBLE + EDGE_TIE_ADAPTER + candidate + EDGE_TIE_CHECKS)


    def test_sdf_gather_cosorts_with_scatter_faces(self):
        """A coincident SDF and scattered voxel face co-sort whatever the face."""
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            helpers = cosort_helpers(ROOT / "engine/render/src/shaders" / folder, suffix)
            variants = {
                "production": helpers,
                "gather-keeps-face-bits": re.sub(
                    r"decodeDepthSingle\(rawDist\)\s*\*\s*kDepthEncodeShift", "rawDist", helpers),
                "cardinal-stripped": re.sub(r"faceFree\s*\?", "true ?", helpers),
                "scatter-face-bias": re.sub(r"\*\s*encScale;", "* encScale + 1.0f;", helpers),
            }
            self.check_variants(suffix, variants, "cosort", lambda candidate:
                                PREAMBLE + EDGE_TIE_ADAPTER + candidate + COSORT_CHECKS)


if __name__ == "__main__":
    unittest.main()

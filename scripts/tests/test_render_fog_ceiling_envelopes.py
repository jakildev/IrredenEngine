"""Execute ceiling retention and compact admission without a graphics context.

Only vector syntax and texture reads are adapted. Production GLSL and Metal
helpers and compact predicates run against cell fixtures and an independent
scalar presence oracle; this does not certify GPU bindings or native captures.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_function, replace_once

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")

PREAMBLE = r"""
#define IR_FOG_CEILING_PASS 1
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <utility>
using uint = std::uint32_t;
template<class T> struct V2 {
    T x,y;
    explicit V2(T a=0):x(a),y(a){}
    V2(T a,T b):x(a),y(b){}
    template<class U> explicit V2(V2<U> a):x(T(a.x)),y(T(a.y)){}
};
template<class T> struct V3 {
    T x,y,z;
    explicit V3(T a=0):x(a),y(a),z(a){}
    V3(T a,T b,T c):x(a),y(b),z(c){}
    T& operator[](int i){return i==0?x:(i==1?y:z);}
};
template<class T> struct V4 {
    T x,y,z,w;
    V4(T a=0,T b=0,T c=0,T d=0):x(a),y(b),z(c),w(d){}
    T& operator[](int i){return i==0?x:(i==1?y:(i==2?z:w));}
    T operator[](int i)const{return i==0?x:(i==1?y:(i==2?z:w));}
};
using vec2=V2<float>; using ivec2=V2<int>; using uvec2=V2<uint>;
using ivec3=V3<int>; using vec4=V4<float>; using uvec4=V4<uint>;
template<class T> V2<T> xy(V3<T> a){return {a.x,a.y};}
template<class T> V2<T> xy(V4<T> a){return {a.x,a.y};}
template<class T> V2<T> operator-(V2<T> a,V2<T> b){return {a.x-b.x,a.y-b.y};}
template<class T> V2<T> operator-(V2<T> a,double b){return {T(a.x-b),T(a.y-b)};}
template<class T> V2<T> operator+(V2<T> a,double b){return {T(a.x+b),T(a.y+b)};}
template<class T> V3<T> operator-(V3<T> a,V3<T> b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
template<class T> V3<T> operator+(V3<T> a,V3<T> b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
vec2 clamp(vec2 a,vec2 lo,vec2 hi){return {std::clamp(a.x,lo.x,hi.x),std::clamp(a.y,lo.y,hi.y)};}
float length(vec2 a){return std::sqrt(a.x*a.x+a.y*a.y);}
float max(float a,float b){return std::max(a,b);}
using std::abs;
float fract(float a){return a-std::floor(a);}
float mix(float a,float b,float t){return a*(1-t)+b*t;}
float smoothstep(float a,float b,float x){
    float t=std::clamp((x-a)/(b-a),0.f,1.f);return t*t*(3-2*t);
}
vec4 visionCircles[8],visionCircleHeights[8],visionCircleCeilings[8],revealSurfaceTreatment;
uvec4 visionCircleChannels[2];
int visionCircleCount=1, fogWidth=32;
std::map<std::pair<int,int>,uvec2> cells;
uvec2 fogSelectionCell(ivec2 p){
    auto i=cells.find({p.x,p.y});return i==cells.end()?uvec2(0,1):i->second;
}
struct Gate {bool possible,region;};
"""

CHECKS = r"""
void check(bool ok,const char* name){if(!ok){std::fprintf(stderr,"FAIL %s\n",name);std::exit(1);}}
float presence(ivec3 v,float offset=0){return fogCeilingPresence(v,fogSelectionCell(xy(v)),
    visionCircles,visionCircleHeights,visionCircleCeilings,visionCircleChannels,visionCircleCount,offset);}
bool retained(ivec3 v,float phase){return fogCeilingRetained(v,fogSelectionCell(xy(v)),
    visionCircles,visionCircleHeights,visionCircleCeilings,visionCircleChannels,visionCircleCount,
    revealSurfaceTreatment,phase);}
void reset(){
    cells.clear();visionCircleCount=1;revealSurfaceTreatment={1,.5,0,0};
    for(int i=0;i<8;++i){visionCircles[i]={0,0,20,0};visionCircleHeights[i]={0,0,0,0};
        visionCircleCeilings[i]={5,4,0,0};visionCircleChannels[i/4][i%4]=1;}
}
double oracle(ivec3 v){
    auto cell=fogSelectionCell(xy(v));if(cell.x==255)return 1;
    bool admitted=false;double result=0;
    for(int i=0;i<visionCircleCount;++i){
        if(!(visionCircleChannels[i/4][i%4]&cell.y))continue;
        auto c=visionCircles[i];double a=std::max(double(c.w),.75);
        double dx=std::max(std::abs(double(c.x)-v.x)-.5,0.);
        double dy=std::max(std::abs(double(c.y)-v.y)-.5,0.);
        if(std::hypot(dx,dy)>=c.z+a)continue;
        admitted=true;auto ceiling=visionCircleCeilings[i];
        double height=std::max(double(visionCircleHeights[i].x)-v.z,0.);
        double visibility=1;
        if(ceiling.x>=0){
            if(ceiling.y<=0)visibility=height<=ceiling.x;
            else{double q=std::clamp((ceiling.x+ceiling.y-height)/ceiling.y,0.,1.);
                visibility=q*q*(3-2*q);}
        }
        result=std::max(result,visibility);
    }
    return admitted?result:1;
}
float minimumPhase(ivec3 v){
    uint h=uint(v.x)*0xA24BAED5u ^ uint(v.y)*0x9FB21C65u ^ uint(v.z)*0xC13FA9A9u;
    h^=h>>16;h*=0x7FEB352Du;h^=h>>15;
    return .75f/(.125f+.875f*(float(h&0xFFFFFFu)/16777216.f));
}
int main(){
    reset();visionCircleCeilings[0]={5,0,0,0};
    for(int z:{-6,-7}){
        ivec3 v(0,0,z);check(!retained(v,0),"hard_static_cut");
        check(retained(v,minimumPhase(v)),"hard_phase_revives");
        check(compact(v,2).possible,"upper_envelope_admits_revived");
    }
    reset();ivec3 cut(-1,-4,-6);
    check(retained(cut,0)&&!retained(cut+ivec3(1,0,0),0),"phase_zero_cut_pair");
    check(compact(cut,0).region,"lower_envelope_admits_interior_face");
    reset();cells[{-1,-4}]={255,1};visionCircleCeilings[0]={0,0,0,0};
    check(retained(cut,0)&&!retained(cut+ivec3(1,0,0),0),"neighbor_own_cell_pair");
    check(compact(cut,0).region,"neighbor_cell_admits_face");
    reset();visionCircleCount=2;visionCircleCeilings[0]={0,0,0,0};
    visionCircles[1]={2.1,0,1,0};visionCircleCeilings[1]={-1,0,0,0};
    check(retained(ivec3(0,0,-6),0),"aa_source_union");
    check(kFogCeilingAdmitAa>std::sqrt(.5),"aa_pixel_bound");
    uint rng=0x94762a31;
    auto random=[&](){rng=1664525u*rng+1013904223u;return float(rng>>8)/16777216.f;};
    // A column within 1e-4 of a source's admission edge is a float/double tie the
    // scalar oracle cannot settle; such scenes are skipped, not graded.
    auto nearAdmissionEdge=[&](ivec2 col){
        for(int i=0;i<visionCircleCount;++i){
            auto c=visionCircles[i];double a=std::max(double(c.w),.75);
            double dx=std::max(std::abs(double(c.x)-col.x)-.5,0.);
            double dy=std::max(std::abs(double(c.y)-col.y)-.5,0.);
            if(std::abs(std::hypot(dx,dy)-(c.z+a))<1e-4)return true;
        }
        return false;
    };
    int cuts=0,drops=0,changes=0,checks=0,skipped=0;
    for(int scene=0;scene<500;++scene){
        reset();visionCircleCount=1+scene%8;revealSurfaceTreatment.y=random();
        // Sequenced draws: a parenthesized constructor call evaluates its
        // arguments in an unspecified order, which would make the scene stream
        // compiler-dependent.
        const int vx=int(random()*20)-10;
        const int vy=int(random()*20)-10;
        const int vz=-int(random()*18);
        const ivec3 v(vx,vy,vz);
        for(int i=0;i<visionCircleCount;++i){
            visionCircles[i]={random()*20-10,random()*20-10,random()*20+1,random()*2};
            visionCircleHeights[i]={random()*6,random()*2,random()*2,random()};
            visionCircleCeilings[i]={i==7?-1:random()*10,scene%3?random()*5:0,0,0};
            visionCircleChannels[i/4][i%4]=1u<<(i%3);
        }
        if(nearAdmissionEdge(xy(v))||nearAdmissionEdge({v.x+1,v.y})||
           nearAdmissionEdge({v.x-1,v.y})||nearAdmissionEdge({v.x,v.y+1})||
           nearAdmissionEdge({v.x,v.y-1})){++skipped;continue;}
        cells[{v.x,v.y}]={scene%17==0?255u:(scene%11==0?128u:0u),1u+uint(scene%7)};
        cells[{v.x+1,v.y}]={0,1u<<uint(scene%3)};
        cells[{v.x,v.y-1}]={255,1};
        check(std::abs(presence(v)-oracle(v))<1e-5,"independent_presence");
        check(fogCeilingPhaseHeight(v,0)==0,"phase_zero_exact");
        for(int sample=0;sample<24;++sample){
            float phase=sample*.37f-3,offset=fogCeilingPhaseHeight(v,phase);
            check(std::abs(offset)<=2,"two_voxel_bound");
            check(presence(v,2)<=presence(v,offset)+1e-6,"lower_bound");
            check(presence(v,-2)+1e-6>=presence(v,offset),"upper_bound");
            bool own=retained(v,phase);changes+=own!=retained(v,0);
            for(int axis=0;axis<3;++axis){
                Gate g=compact(v,axis);
                if(!g.possible){check(!own,"compact_drop_sound");++drops;}
                for(int side:{-1,1}){ivec3 n=v;n[axis]+=side;
                    if(own&&!retained(n,phase)){check(g.possible&&g.region,"cut_axis_admitted");++cuts;}}
                auto body=compact(v,axis,true);
                check(body.possible&&!body.region,"body_identity");++checks;
            }
        }
        revealSurfaceTreatment.x=0;
        check(retained(v,9.1f)==(oracle(v)>0),"disabled_phase_identity");
        check(compact(v,0).possible==(presence(v)>0),"disabled_envelope_identity");
        for(int i=0;i<visionCircleCount;++i)visionCircleCeilings[i].x=-1;
        auto none=compact(v,0);check(none.possible&&!none.region,"no_ceiling_identity");
        check(compact(v,0,false,0).region,"authored_exposure_identity");
    }
    check(cuts>0&&drops>0&&changes>0,"nonvacuous_sweep");
    std::printf("checks=%d cuts=%d drops=%d phaseChanges=%d skipped=%d\n",
                checks,cuts,drops,changes,skipped);
}
"""


def shader_source(suffix, directory):
    shaders = ROOT / "engine/render/src/shaders" / directory
    common = (shaders / f"ir_iso_common.{suffix}").read_text()
    compact = (shaders / f"c_voxel_visibility_compact_body.{suffix}").read_text()
    names = ("faceIsExposed", "fogVisionCircleReveal", "fogCeilingVisibility",
             "fogRevealSurfaceHash01", "fogCeilingPhaseHeight", "fogCeilingSourceMask",
             "fogCeilingPresenceFromMask", "fogCeilingPresence", "fogCeilingRetained")
    constants = "\n".join(re.search(
        rf"(?:const|constant) (?:int|float) {name} = [^;]+;", common).group()
        for name in ("kMaxFogVisionCircles", "kFogCeilingAdmitAa", "kFogCeilingFlickerSpan"))
    source = constants + "\n" + "\n".join(extract_function(common, name) for name in names)

    # Each ceiling block sits under its own `#if IR_FOG_CEILING_PASS`; the slice
    # starts at that directive so the `#else` identity arm stays balanced.
    def ceiling_block(marker, stop):
        start = compact.rindex("#if IR_FOG_CEILING_PASS", 0, compact.index(marker))
        return compact[start:compact.index(stop, start)]

    drop = ceiling_block("const bool ceilingActive =", "if (isoPos.x >=")
    invariant = ceiling_block("const float ownMinimum =", "for (int axis = 0; axis < 3; ++axis)")
    region = ceiling_block("bool possibleCut = false;",
                           "if (faceIsExposed(flagsByte, 2 * axis)")
    append = re.search(r"if \((faceIsExposed\(flagsByte, 2 \* axis\).*?)\) \{",
                       compact, re.DOTALL).group(1)
    source += ("\nGate compact(ivec3 voxelPosRaw,int axis,bool fogWholeBodyExempt=false,"
               "uint flagsByte=0xFCu){\n" + drop + "\n" + invariant + "\n" + region
               + "\nreturn {ceilingPossible," + append + "};}\n")
    source = source.replace("fogSelectionCell(canvasFogOfWar, fogObservers, ", "fogSelectionCell(")
    source = source.replace("fogObservers.", "")
    source = source.replace("imageSize(canvasFogOfWar).x", "fogWidth")
    source = source.replace("canvasFogOfWar.get_width()", "fogWidth")
    source = source.replace("constant ", "const ")
    for before, after in (("float2", "vec2"), ("float4", "vec4"), ("int2", "ivec2"),
                          ("int3", "ivec3"), ("uint2", "uvec2"), ("uint4", "uvec4")):
        source = re.sub(rf"\b{before}\b", after, source)
    source = re.sub(r"\b(\w+)\.xy\b", r"xy(\1)", source)
    # Shader literals are float even when the GLSL spelling omits its suffix.
    return re.sub(r"(?<![\w.])(\d+\.\d+)(?![\w.])", r"\1f", source)


@unittest.skipUnless(COMPILER, "ceiling envelope oracle requires a C++ compiler")
class FogCeilingEnvelopesTest(unittest.TestCase):
    def test_compact_retains_every_phase_and_cut_axis(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal")):
            source = shader_source(suffix, directory)
            mutations = {
                "production": (source, None),
                "static_drop": (replace_once(source, "-ceilingSpan) >", "0.0f) >"),
                                "upper_envelope_admits_revived"),
                "upper_region": (source.replace(
                    "visionCircleCount, ceilingSpan)", "visionCircleCount, -ceilingSpan)"),
                    "lower_envelope_admits_interior_face"),
                "lost_cut_append": (replace_once(source, "|| possibleCut", "|| false"),
                                    "lower_envelope_admits_interior_face"),
                "owner_cell": (source.replace("fogSelectionCell(xy(plus))",
                                              "fogSelectionCell(xy(voxelPosRaw))")
                               .replace("fogSelectionCell(xy(minus))",
                                        "fogSelectionCell(xy(voxelPosRaw))"),
                               "neighbor_cell_admits_face"),
                "phase_removed": (replace_once(source,
                    "treated ? fogCeilingPhaseHeight(voxel, phase)",
                    "treated ? 0.0f"), "hard_phase_revives"),
                "own_softness": (replace_once(source,
                    "fogVisionCircleReveal(nearest, circle, kFogCeilingAdmitAa)",
                    "fogVisionCircleReveal(nearest, circle, 0.0f)"), "aa_source_union"),
                "aa_too_small": (replace_once(source, "kFogCeilingAdmitAa = 0.75f",
                                              "kFogCeilingAdmitAa = 0.70f"), "aa_pixel_bound"),
            }
            for name, (variant, failure) in mutations.items():
                with (self.subTest(backend=suffix, mutation=name),
                      tempfile.TemporaryDirectory() as tmp):
                    if failure:
                        self.assertNotEqual(variant, source)
                    cpp, executable = Path(tmp) / "ceiling.cpp", Path(tmp) / "ceiling"
                    cpp.write_text(PREAMBLE + variant + CHECKS)
                    build = subprocess.run([COMPILER, "-std=c++17", "-O2", str(cpp),
                                            "-o", str(executable)], capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(executable)], capture_output=True, text=True)
                    if failure:
                        self.assertEqual(run.returncode, 1, run.stdout + run.stderr)
                        self.assertIn("FAIL " + failure, run.stderr)
                    else:
                        self.assertEqual(run.returncode, 0, run.stderr)
                    print(suffix, name, run.stdout.strip() or run.stderr.strip())


if __name__ == "__main__":
    unittest.main()

"""Execute the finite-face receiver and fallback branches with a ray oracle.

The opt-in reference scans only complete small record pools. These controls
exercise production sampler code through its finite-surface layers; they do
not emulate GPU indexing, legacy PCF, or the displayed receiver geometry.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_function

SHADERS = Path(__file__).resolve().parents[2] / "engine/render/src/shaders"
COMPILER = shutil.which("c++")

PREAMBLE = r"""
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
using uint = std::uint32_t;
using std::abs;
using std::min;
using std::max;
struct vec2 {
    float x,y;
    vec2(float a,float b):x(a),y(b){}
    explicit vec2(float a):x(a),y(a){}
    template<class T> explicit vec2(T a):x(a.x),y(a.y){}
};
struct ivec2 {
    int x,y;
    ivec2(int a,int b):x(a),y(b){}
    explicit ivec2(int a):x(a),y(a){}
    explicit ivec2(vec2 a):x(int(a.x)),y(int(a.y)){}
};
struct vec3 {float x,y,z; vec3(float a,float b,float c):x(a),y(b),z(c){}};
struct vec4 {float x,y,z,w;};
vec2 operator+(vec2 a,vec2 b){return {a.x+b.x,a.y+b.y};}
vec2 operator-(vec2 a,vec2 b){return {a.x-b.x,a.y-b.y};}
vec2 operator*(vec2 a,float b){return {a.x*b,a.y*b};}
vec2 operator*(vec2 a,vec2 b){return {a.x*b.x,a.y*b.y};}
vec2 operator/(vec2 a,vec2 b){return {a.x/b.x,a.y/b.y};}
vec2 operator/(vec2 a,float b){return {a.x/b,a.y/b};}
vec2 operator+(vec2 a,float b){return {a.x+b,a.y+b};}
bool operator>=(ivec2 a,ivec2 b){return a.x>=b.x && a.y>=b.y;}
bool operator<(ivec2 a,ivec2 b){return a.x<b.x && a.y<b.y;}
bool greaterThanEqual(ivec2 a,ivec2 b){return a>=b;}
bool lessThan(ivec2 a,ivec2 b){return a<b;}
bool all(bool a){return a;}
vec2 floor(vec2 a){return {std::floor(a.x),std::floor(a.y)};}
float dot(vec2 a,vec2 b){return a.x*b.x+a.y*b.y;}
float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
vec3 faceOutwardNormal6(int id){if(id!=5)std::abort();return {0,0,1};}
vec3 rotateByQuat(vec3 a,vec4 q){if(q.w!=1)std::abort();return a;}
float uintBitsToFloat(uint a){float b;std::memcpy(&b,&a,4);return b;}
uint floatBitsToUint(float a){uint b;std::memcpy(&b,&a,4);return b;}
template<class T> T as_type(uint a){T b;std::memcpy(&b,&a,4);return b;}
const int kSunShadowMapDim=1024,kCascadeTexelCount=1024*1024;
const float kSunDepthScale=1024,kSunDepthOffset=512;
const float kShadowBiasQuantNoise=4.0/kSunDepthScale;
uint depth(float z){return (uint((z+512)*1024)<<8)|0x89u;}
struct BoundBuffer {
    std::vector<uint> words;
    uint recordOffset=0,readCount=0;
    uint operator[](uint i){
        if(i>=words.size()){std::cerr<<"out-of-bounds read\n";std::exit(90);}
        if(i>=recordOffset)++readCount;
        return words[i];
    }
    int length() const{return int(words.size());}
};
BoundBuffer sunDepthBuf;
"""

CASES = r"""
struct Face {vec3 corner,u,v;};
std::vector<Face> faces;
uint tileBase(uint cascade){return kSourceFaceTileOffset+cascade*128*128*65;}
void reset(uint count,uint tileCount,uint cascade=0) {
    sunDepthBuf.words.assign(kSourceFaceBufferWords,0xffffffffu);
    sunDepthBuf.recordOffset=kSourceFaceRecordOffset;
    sunDepthBuf.readCount=0;
    sunDepthBuf.words[kSourceFaceHeaderOffset]=count;
    sunDepthBuf.words[tileBase(cascade)]=tileCount;
    for(uint i=0;i<64;++i)sunDepthBuf.words[tileBase(cascade)+1+i]=63-i;
    faces.clear();
}
void writeFace(Face f){
    const uint record=kSourceFaceRecordOffset+9*uint(faces.size());
    const float values[]={f.corner.x,f.corner.y,f.corner.z,f.u.x,f.u.y,f.u.z,
                          f.v.x,f.v.y,f.v.z};
    for(uint i=0;i<9;++i)sunDepthBuf.words[record+i]=floatBitsToUint(values[i]);
    faces.push_back(f);
}
float sample(vec2 uv={2.9f,3.5f},float z=10,uint cascade=0,
             bool surface=true,vec3 normal={0,0,1},float maxThrow=100){
    return SAMPLE_CALL;
}
void require(bool ok,const char* label){if(!ok){std::cerr<<label<<"\n";std::exit(1);}}
void fill(uint count,Face f){for(uint i=0;i<count;++i)writeFace(f);}
void fallback(uint cascade,uint primary,uint indexed){
    const uint address=cascade*1024*1024+3*1024+2;
    sunDepthBuf.words[address]=primary;
    sunDepthBuf.words[kSourceFaceFallbackOffset+address]=indexed;
}
struct D3 {double x,y,z;};
D3 toDouble(vec3 a){return {a.x,a.y,a.z};}
D3 operator+(D3 a,D3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
D3 operator-(D3 a,D3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
D3 cross(D3 a,D3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double dot(D3 a,D3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
// A downward ray intersects either of the two finite triangles forming a face.
bool triangle(D3 origin,D3 a,D3 b,D3 c,double maxThrow){
    const D3 direction{0,0,-1},edge1=b-a,edge2=c-a;
    const D3 p=cross(direction,edge2);
    const double determinant=dot(edge1,p);
    if(std::abs(determinant)<1e-12)return false;
    const D3 t=origin-a;
    const double u=dot(t,p)/determinant;
    if(u<0 || u>1)return false;
    const D3 q=cross(t,edge1);
    const double v=dot(direction,q)/determinant;
    if(v<0 || u+v>1)return false;
    const double distance=dot(edge2,q)/determinant;
    return distance>4.0/1024.0 && distance<maxThrow;
}
bool oracle(vec2 uv,float z,float maxThrow){
    const D3 ray{uv.x,uv.y,z};
    for(const Face& f:faces){
        const D3 a=toDouble(f.corner),b=a+toDouble(f.u),d=a+toDouble(f.v);
        const D3 c=b+toDouble(f.v);
        if(triangle(ray,a,b,c,maxThrow)||triangle(ray,a,c,d,maxThrow))return true;
    }
    return false;
}
int main(){
    static_assert(kSourceFaceTileCapacity==64 && kSourceFaceCapacity==65536);
    static_assert(kSourceFaceOverflowReferenceBudget==256);
    const bool enabled=IR_SUN_FACE_OVERFLOW_REFERENCE!=0;
    const uint empty=0xffffffffu;
    const Face tapOnly{{2,3,1},{.75f,0,0},{0,1,0}};
    const Face hit{{2,3,2},{1,0,0},{0,1,0}};
    for(uint cascade:{0u,1u}){
        for(uint count:{64u,65u,129u,256u,257u,65536u,65537u}){
            reset(count,count,cascade);
            fill(std::min(count,256u),tapOnly);
            fallback(cascade,empty,depth(1));
            const bool exact=count<=64 || (enabled && count<=256);
            require(sample({2.9f,3.5f},10,cascade)==(exact?0:1),"exact miss/fallback boundary");
            require(sunDepthBuf.readCount==(exact?count*9:0),"bounded no-hit record reads");
            fallback(cascade,depth(2),depth(1));
            require(sample({2.9f,3.5f},10,cascade)==1,"preserve other surface layer");
        }
        for(uint count:{64u,65u,129u,256u}){
            reset(count,count,cascade);
            fill(count-1,tapOnly);
            writeFace(hit);
            require(sample({2.9f,3.5f},10,cascade)==((count<=64||enabled)?1:0),
                    "real hit at final stored record");
        }
        // A complete list remains authoritative even when the global pool overflowed.
        reset(65537,64,cascade);fill(64,tapOnly);
        fallback(cascade,empty,depth(1));
        require(sample({2.9f,3.5f},10,cascade)==0,"complete tile in exhausted pool");
        require(sunDepthBuf.readCount==64*9,"complete tile uses bounded list");
    }
    reset(257,1);fill(63,tapOnly);writeFace(hit);
    require(sample()==1,"complete list uses stored record IDs");
    require(sunDepthBuf.readCount==9,"ordinary tile query work unchanged");
    reset(0,0);
    fallback(0,empty,depth(1));
    require(sample()==0 && sunDepthBuf.readCount==0,"empty index ignores indexed fallback");
    reset(65,65);fill(65,hit);
    require(sample({2.9f,3.5f},10,0,false)==0 && sunDepthBuf.readCount==0,
            "non-surface receiver bypasses finite branch");
    require(sample({2.9f,3.5f},10,0,true,{0,0,-1})==0 && sunDepthBuf.readCount==0,
            "back-facing receiver bypasses exact query");
    require(sample({-1,3.5f})==0 && sunDepthBuf.readCount==0,"outside-map query");
    if(enabled){
        for(float z:{2.0f,2.00390625f,2.004f,102.0f,102.01f})
            require(sample({2.9f,3.5f},z)==oracle({2.9f,3.5f},z,100),
                    "strict self-bias and throw window");
        for(vec2 uv:{vec2(2,3),vec2(3,4),vec2(3.0001f,4),vec2(1.9999f,3)})
            require(sample(uv)==oracle(uv,10,100),"closed finite footprint");
        uint queries=0,hits=0,misses=0;
        for(uint count:{65u,129u}){
            reset(count,count);
            for(uint i=0;i<count;++i){
                const float angle=.25f+.006f*i;
                const float c=std::cos(angle),s=std::sin(angle);
                vec3 u{2.5f*c,2.5f*s,.1f},v{-1.4f*s,1.4f*c,-.2f};
                vec3 corner{3.9f-(u.x+v.x)*.5f+.001f*i,
                            3.7f-(u.y+v.y)*.5f,2+.003f*i};
                if(i%3==0){corner={corner.x+u.x,corner.y+u.y,corner.z+u.z};
                    u={-u.x,-u.y,-u.z};}
                if(i%5==0)std::swap(u,v);
                writeFace({corner,u,v});
            }
            for(uint y=0;y<173;++y)for(uint x=0;x<179;++x){
                const vec2 uv{.031f+7.92f*x/178,.017f+7.95f*y/172};
                const bool expected=oracle(uv,10,100);
                sunDepthBuf.readCount=0;
                require(sample(uv)==expected,"independent triangle-ray coverage");
                require(sunDepthBuf.readCount<=count*9,"bounded reference query work");
                ++queries;hits+=expected;misses+=!expected;
            }
        }
        require(hits>1000 && misses>1000,"coverage fixture contains hits and misses");
        std::cout<<queries<<" independent ray queries; "<<hits<<" hits; "<<misses<<" misses\n";
    }
}
"""


def host_source(suffix, directory, enabled, mutation=None):
    sampler = (SHADERS / directory / f"ir_sun_shadow_sample.{suffix}").read_text()
    layout = (SHADERS / directory / f"ir_sun_face_query_layout.{suffix}").read_text()
    geometry = (SHADERS / directory / f"ir_projected_face.{suffix}").read_text()
    layout = layout.replace(f'#include "ir_projected_face.{suffix}"', geometry)
    projection = (SHADERS / directory / f"ir_sun_projection.{suffix}").read_text()
    helpers = "\n".join(extract_function(projection, name) for name in (
        "sunWriteIsSurface", "sunVoxelFaceId", "sunVoxelFaceViewAligned", "unpackSunDepth"))
    constants = re.search(
        r"#ifndef IR_SUN_FACE_OVERFLOW_REFERENCE.*?Budget = \d+u;", sampler, re.DOTALL)
    if constants is None:
        raise ValueError("missing overflow reference switch and query budget")
    body = extract_function(sampler, "sampleCascadeShadow")
    body = body[:body.index("    float slope =")] + "    return 0.0;\n}\n"
    if mutation:
        old, new = mutation
        if old not in body:
            raise ValueError(f"mutation subject absent: {old}")
        body = body.replace(old, new)
    call = ("sampleCascadeShadow(uv,z,normal,{0,0,1},{1,0,0},{0,1,0},"
            "{0,0},{1,1},int(cascade)*kCascadeTexelCount,")
    if suffix == "metal":
        call += "sunDepthBuf,"
    call += "maxThrow,surface,{0,0,0,1},{0,0,0})"
    define = "#define IR_SUN_FACE_OVERFLOW_REFERENCE 1\n" if enabled else ""
    code = PREAMBLE + define + constants[0] + "\n" + layout + helpers + body
    code += f"static_assert(IR_SUN_FACE_OVERFLOW_REFERENCE == {int(enabled)});\n"
    code += CASES.replace("SAMPLE_CALL", call)
    return (code.replace("constant uint", "const uint")
            .replace("device const uint *sunDepthBuf", "BoundBuffer& sunDepthBuf")
            .replace("float2", "vec2").replace("float3", "vec3")
            .replace("float4", "vec4").replace("int2", "ivec2"))


@unittest.skipUnless(COMPILER, "finite receiver controls require a C++ compiler")
class SourceFaceOverflowTest(unittest.TestCase):
    def test_reference_and_default_with_independent_geometry(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            for enabled in (False, True):
                with self.subTest(backend=suffix, enabled=enabled):
                    run = self.compile_and_run(host_source(suffix, directory, enabled))
                    self.assertEqual(run.returncode, 0, run.stderr)
                    if enabled:
                        print(f"{suffix}: {run.stdout}", end="")

    def test_mutation_controls(self):
        mutations = {
            "drop_reference": ("sourceQueryComplete || queryAllFaces", "sourceQueryComplete"),
            "truncate_pool": ("candidate < candidateCount", "candidate + 1u < candidateCount"),
            "scan_beyond_budget": ("sourceFaceCount <= kSourceFaceOverflowReferenceBudget", "true"),
            "wrong_record": ("queryAllFaces ? candidate :", "queryAllFaces ? 0u :"),
            "accept_sample_after_miss": ("if (layer == 1 && sourceQueryComplete) continue;", ""),
            "drop_other_layer": ("layer == 1 && sourceQueryComplete", "sourceQueryComplete"),
            "ignore_list": ("queryAllFaces ? candidate : sunDepthBuf[tileBase + 1u + candidate]",
                            "candidate"),
        }
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            for name, mutation in mutations.items():
                with self.subTest(backend=suffix, mutation=name):
                    run = self.compile_and_run(host_source(suffix, directory, True, mutation))
                    self.assertNotEqual(run.returncode, 0, "mutation escaped receiver controls")

    def compile_and_run(self, source):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "receiver.cpp").write_text(source)
            build = subprocess.run(
                [COMPILER, "-std=c++17", "-O2", str(path / "receiver.cpp"),
                 "-o", str(path / "receiver")], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            return subprocess.run([str(path / "receiver")], capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()

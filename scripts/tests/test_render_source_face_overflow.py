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

from shader_contract_helpers import extract_block, extract_function
from test_render_source_face_index import load_shader

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
    vec2()=default;
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
struct vec3 {
    float x,y,z;
    vec3():x(0),y(0),z(0){}
    vec3(float a,float b,float c):x(a),y(b),z(c){}
    explicit vec3(float a):x(a),y(a),z(a){}
    vec2 xy()const{return {x,y};}
    float& operator[](uint i){return i==0?x:(i==1?y:z);}
    float operator[](uint i)const{return i==0?x:(i==1?y:z);}
};
struct vec4 {
    float x,y,z,w;
    vec4():x(0),y(0),z(0),w(0){}
    vec4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}
    explicit vec4(float a):x(a),y(a),z(a),w(a){}
    vec4(vec2 a,float c,float d):x(a.x),y(a.y),z(c),w(d){}
    vec4(vec3 a,float d):x(a.x),y(a.y),z(a.z),w(d){}
    vec2 xy()const{return {x,y};}
    vec3 xyz()const{return {x,y,z};}
    float& operator[](uint i){return i==0?x:(i==1?y:(i==2?z:w));}
    float operator[](uint i)const{return i==0?x:(i==1?y:(i==2?z:w));}
};
vec2 operator+(vec2 a,vec2 b){return {a.x+b.x,a.y+b.y};}
vec2 operator-(vec2 a,vec2 b){return {a.x-b.x,a.y-b.y};}
vec2 operator*(vec2 a,float b){return {a.x*b,a.y*b};}
vec2 operator*(vec2 a,vec2 b){return {a.x*b.x,a.y*b.y};}
vec2 operator/(vec2 a,vec2 b){return {a.x/b.x,a.y/b.y};}
vec2 operator/(vec2 a,float b){return {a.x/b,a.y/b};}
vec2 operator+(vec2 a,float b){return {a.x+b,a.y+b};}
vec2 operator-(vec2 a,float b){return {a.x-b,a.y-b};}
vec2& operator+=(vec2& a,vec2 b){a=a+b;return a;}
ivec2 operator+(ivec2 a,ivec2 b){return {a.x+b.x,a.y+b.y};}
ivec2 operator-(ivec2 a,ivec2 b){return {a.x-b.x,a.y-b.y};}
float mix(float a,float b,float t){return a*(1-t)+b*t;}
bool operator>=(ivec2 a,ivec2 b){return a.x>=b.x && a.y>=b.y;}
bool operator<(ivec2 a,ivec2 b){return a.x<b.x && a.y<b.y;}
bool operator>=(vec2 a,vec2 b){return a.x>=b.x && a.y>=b.y;}
bool operator<=(vec2 a,vec2 b){return a.x<=b.x && a.y<=b.y;}
bool greaterThanEqual(ivec2 a,ivec2 b){return a>=b;}
bool lessThan(ivec2 a,ivec2 b){return a<b;}
bool all(bool a){return a;}
vec2 floor(vec2 a){return {std::floor(a.x),std::floor(a.y)};}
vec2 abs(vec2 a){return {std::abs(a.x),std::abs(a.y)};}
float min(float a,float b){return std::min(a,b);}
float max(float a,float b){return std::max(a,b);}
float clamp(float a,float lo,float hi){return std::min(std::max(a,lo),hi);}
vec3 min(vec3 a,vec3 b){return {min(a.x,b.x),min(a.y,b.y),min(a.z,b.z)};}
vec3 max(vec3 a,vec3 b){return {max(a.x,b.x),max(a.y,b.y),max(a.z,b.z)};}
bool greaterThanEqual(vec2 a,vec2 b){return a.x>=b.x && a.y>=b.y;}
bool lessThanEqual(vec2 a,vec2 b){return a.x<=b.x && a.y<=b.y;}
float dot(vec2 a,vec2 b){return a.x*b.x+a.y*b.y;}
float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
vec3 faceOutwardNormal6(int id){if(id!=5)std::abort();return {0,0,1};}
vec3 rotateByQuat(vec3 a,vec4 q){if(q.w!=1)std::abort();return a;}
float uintBitsToFloat(uint a){float b;std::memcpy(&b,&a,4);return b;}
uint floatBitsToUint(float a){uint b;std::memcpy(&b,&a,4);return b;}
template<class T,class U> T as_type(U a){T b;std::memcpy(&b,&a,4);return b;}
const int kSunShadowMapDim=1024,kCascadeTexelCount=1024*1024;
const float kSunDepthScale=1024,kSunDepthOffset=512;
const float kShadowBiasQuantNoise=4.0/kSunDepthScale;
uint depth(float z){return (uint((z+512)*1024)<<8)|0x89u;}
struct BoundBuffer {
    std::vector<uint> words;
    uint recordOffset=0,recordEnd=0,readCount=0;
    uint spillCounterOffset=0,spillCounterReads=0;
    uint& operator[](uint i){
        if(i>=words.size()){std::cerr<<"out-of-bounds read\n";std::exit(90);}
        if(i>=recordOffset && i<recordEnd)++readCount;
        if(i==spillCounterOffset)++spillCounterReads;
        return words[i];
    }
    int length() const{return int(words.size());}
};
BoundBuffer sunDepthBuf;
// This fixture contains finite faces only; analytic shapes have separate ray controls.
float sourceShapeRaySeparation(vec2,float,uint,vec3,vec3,vec3){std::abort();}

"""

CASES = r"""
struct Face {vec3 corner,u,v;};
std::vector<Face> faces;
uint tileBase(uint cascade){return kSourceFaceTileOffset+cascade*128*128*65;}
void reset(uint count,uint tileCount,uint cascade=0) {
    sunDepthBuf.words.assign(kSourceFaceBufferWords,0xffffffffu);
    sunDepthBuf.recordOffset=kSourceFaceRecordOffset;
    sunDepthBuf.recordEnd=kSourceFaceSpillCounterOffset;
    sunDepthBuf.readCount=0;
    sunDepthBuf.spillCounterOffset=kSourceFaceSpillCounterOffset;
    sunDepthBuf.spillCounterReads=0;
    sunDepthBuf.words[kSourceFaceHeaderOffset]=count;
    sunDepthBuf.words[kSourceFaceCapacityOffset]=kSourceFaceCapacity;
    sunDepthBuf.words[kSourceFaceSpillCounterOffset]=0;
    sunDepthBuf.words[kSourceFaceHierarchyNeededOffset]=0;
    sunDepthBuf.words[kSourceFaceHierarchyReadyOffset]=0;
    sunDepthBuf.words[tileBase(cascade)]=tileCount;
    for(uint i=0;i<64;++i)sunDepthBuf.words[tileBase(cascade)+1+i]=63-i;
    if(tileCount>64 && tileCount<=kSourceFaceTileQueryCapacity){
        const uint tile=cascade*128*128;
        sunDepthBuf.words[kSourceFaceSpillHeadOffset+tile]=tileCount-65;
        for(uint i=64;i<tileCount;++i){
            const uint node=kSourceFaceSpillNodeOffset+(i-64)*kSourceFaceSpillNodeWords;
            sunDepthBuf.words[node]=i;
            sunDepthBuf.words[node+1]=i==64?0xffffffffu:i-65;
        }
    }
    faces.clear();
}
void writeFace(Face f){
    const uint record=kSourceFaceRecordOffset+kSourceFaceRecordWords*uint(faces.size());
    const float values[]={f.corner.x,f.corner.y,f.corner.z,f.u.x,f.u.y,f.u.z,
                          f.v.x,f.v.y,f.v.z};
    for(uint i=0;i<9;++i)sunDepthBuf.words[record+i]=floatBitsToUint(values[i]);
    sunDepthBuf.words[record+9]=0;
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
            const bool exact=count<=kSourceFaceTileQueryCapacity;
            require(sample({2.9f,3.5f},10,cascade)==(exact?0:1),"exact miss/fallback boundary");
            require(sunDepthBuf.readCount==(exact?count*kSourceFaceRecordWords:0),
                    "bounded no-hit record reads");
            fallback(cascade,depth(2),depth(1));
            require(sample({2.9f,3.5f},10,cascade)==1,"preserve other surface layer");
        }
        for(uint count:{64u,65u,129u,256u}){
            reset(count,count,cascade);
            fill(count-1,tapOnly);
            writeFace(hit);
            require(sample({2.9f,3.5f},10,cascade)==1,
                    "real hit at final stored record");
        }
        // A complete list remains authoritative even when the global pool overflowed.
        reset(65537,64,cascade);fill(64,tapOnly);
        fallback(cascade,empty,depth(1));
        require(sample({2.9f,3.5f},10,cascade)==0,"complete tile in exhausted pool");
        require(sunDepthBuf.readCount==64*kSourceFaceRecordWords,"complete tile uses bounded list");
    }
    // Unrelated records must not change a complete local spill query.
    for(uint total:{257u,65537u}){
        reset(total,65);fill(64,tapOnly);writeFace(hit);
        require(sample()==1,"local spill independent of global population");
    }
    // Global spill failure must not pick exact tiles by allocation order.
    for(uint tileCount:{65u,257u}){
        reset(257,tileCount);fill(64,tapOnly);writeFace(hit);
        sunDepthBuf.words[kSourceFaceSpillCounterOffset]=kSourceFaceSpillCapacity+1u;
        require(sample()==0,"spill exhaustion does not select allocation winners");
        require(sunDepthBuf.readCount==0,"spill exhaustion bypasses all partial records");
    }
    reset(257,1);fill(63,tapOnly);writeFace(hit);
    require(sample()==1,"complete list uses stored record IDs");
    require(sunDepthBuf.readCount==kSourceFaceRecordWords,"ordinary tile query work unchanged");
    reset(0,0);
    fallback(0,empty,depth(1));
    require(sample()==0 && sunDepthBuf.readCount==0,"empty index ignores indexed fallback");
    reset(65,65);fill(65,hit);
    require(sample({2.9f,3.5f},10,0,false)==0 && sunDepthBuf.readCount==0,
            "non-surface receiver bypasses finite branch");
    require(sample({2.9f,3.5f},10,0,true,{0,0,-1})==0 && sunDepthBuf.readCount==0,
            "back-facing receiver bypasses exact query");
    require(sample({-1,3.5f})==0 && sunDepthBuf.readCount==0,"outside-map query");
    // Resource failure can make a small complete record pool's tile incomplete.
    reset(65,257);fill(64,tapOnly);writeFace(hit);
    require(sample()==(enabled?1:0),"bounded reference rescues incomplete tile");
    reset(257,257);fill(257,tapOnly);
    fallback(0,empty,depth(1));
    require(sample()==1,"reference never exceeds pool query budget");
    reset(65,65);fill(65,hit);
    {
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
                require(sunDepthBuf.readCount<=count*kSourceFaceRecordWords,
                        "bounded reference query work");
                ++queries;hits+=expected;misses+=!expected;
            }
        }
        require(hits>1000 && misses>1000,"coverage fixture contains hits and misses");
        std::cout<<queries<<" independent ray queries; "<<hits<<" hits; "<<misses<<" misses\n";
    }
}
"""


ORDER_CASES = CASES[:CASES.index("int main(){")] + r"""
int main(){
    const bool enabled=IR_SUN_FACE_OVERFLOW_REFERENCE!=0;
    const uint empty=0xffffffffu;
    const Face miss{{2,3,1},{.25f,0,0},{0,1,0}};
    const Face tapOnly{{2,3,1},{.75f,0,0},{0,1,0}};
    const Face hit{{2.8f,3,2},{.2f,0,0},{0,1,0}};
    uint comparisons=0;
    for(uint cascade:{0u,1u})for(uint count:{257u,513u}){
        for(uint indexed:{empty,depth(1)})for(uint primary:{empty,depth(2)}){
            reset(count,count,cascade);
            writeFace(indexed==empty?miss:tapOnly);
            fill(count-2,miss);writeFace(hit);
            require(oracle({2.9f,3.5f},10,100),"scene contains a real blocker");
            require(oracle({2.5f,3.5f},10,100)==(indexed!=empty),
                    "indexed fallback matches geometry at texel center");
            fallback(cascade,primary,indexed);
            for(uint i=0;i<64;++i)sunDepthBuf.words[tileBase(cascade)+1+i]=i;
            const float excluded=sample({2.9f,3.5f},10,cascade);
            sunDepthBuf.words[tileBase(cascade)+64]=count-1;
            const float retained=sample({2.9f,3.5f},10,cascade);
            require(retained==excluded,"retained subset changes shadow coverage");
            const bool expected=indexed!=empty || primary!=empty;
            require(retained==expected,"complete reference or sampled fallback");
            ++comparisons;
        }
        // A failed global reservation can poison an empty tile without publishing IDs.
        reset(kSourceFaceCapacity+1,kSourceFaceTileRecordLost,cascade);
        for(uint i=0;i<64;++i)sunDepthBuf.words[tileBase(cascade)+1+i]=empty;
        fallback(cascade,empty,depth(1));
        require(sample({2.9f,3.5f},10,cascade)==1,"exhaustion retains fallback");
        require(sunDepthBuf.readCount==0,"exhaustion never reads unpublished records");
    }
    std::cout<<comparisons<<" retained-subset comparisons; poisoned exhaustion safe\n";
}
"""


SPILL_POOL_CASES = CASES[:CASES.index("int main(){")] + r"""
int main(){
    const uint empty=0xffffffffu;
    const Face miss{{2,3,1},{.25f,0,0},{0,1,0}};
    const Face hit{{2.8f,3,2},{.2f,0,0},{0,1,0}};
    uint comparisons=0;
    for(uint cascade:{0u,1u})for(uint count:{1u,64u,65u,256u}){
        for(uint allocation:{kSourceFaceSpillCapacity-1u,kSourceFaceSpillCapacity,
                            kSourceFaceSpillCapacity+1u}){
            reset(257,count,cascade);
            fill(std::max(count,64u)-1u,miss);writeFace(hit);
            sunDepthBuf.words[kSourceFaceSpillCounterOffset]=allocation;
            const bool exact=count<=64 || allocation<=kSourceFaceSpillCapacity;
            require(sample({2.9f,3.5f},10,cascade)==(exact?1:0),
                    "global spill exhaustion guard");
            require((sunDepthBuf.readCount>0)==exact,
                    "global spill exhaustion bypasses geometry");
            require(sunDepthBuf.spillCounterReads==(count<=64?0u:1u),
                    "inline query bypasses spill counter");
            ++comparisons;
        }
    }
    // Winners have complete nodes; losers can have no published IDs at all.
    for(uint cascade:{0u,1u})for(uint indexed:{empty,depth(1)}){
        for(uint primary:{empty,depth(2)}){
            float result[2];
            for(uint winner=0;winner<2;++winner){
                reset(257,winner?65:257,cascade);
                fill(64,miss);writeFace(hit);
                sunDepthBuf.words[kSourceFaceSpillCounterOffset]=kSourceFaceSpillCapacity+1u;
                if(!winner)for(uint i=0;i<64;++i)
                    sunDepthBuf.words[tileBase(cascade)+1+i]=empty;
                fallback(cascade,primary,indexed);
                result[winner]=sample({2.9f,3.5f},10,cascade);
                require(sunDepthBuf.readCount==0,"pool winners and losers skip record reads");
            }
            require(result[0]==result[1],"global allocation order does not select coverage");
            require(result[0]==(primary!=empty||indexed!=empty),
                    "global spill exhaustion preserves both fallback layers");
            ++comparisons;
        }
    }
    std::cout<<comparisons<<" global spill boundary and allocation-order controls passed\n";
}
"""


def host_source(suffix, directory, enabled, mutation=None, cases=CASES, query_stats=False,
                include_pcf=False):
    sampler = (SHADERS / directory / f"ir_sun_shadow_sample.{suffix}").read_text()
    layout = (SHADERS / directory / f"ir_sun_face_query_layout.{suffix}").read_text()
    geometry = (SHADERS / directory / f"ir_projected_face.{suffix}").read_text()
    layout = layout.replace(f'#include "ir_projected_face.{suffix}"', geometry)
    projection = (SHADERS / directory / f"ir_sun_projection.{suffix}").read_text()
    helpers = "\n".join(extract_function(projection, name) for name in (
        "sunWriteIsSurface", "sunVoxelFaceId", "sunVoxelFaceViewAligned", "unpackSunDepth"))
    if include_pcf:
        helpers += extract_function(projection, "sunWriteIsDirect")
        helpers += extract_function(projection, "packSunDepth")
        offset_type = "int2" if suffix == "metal" else "ivec2"
        helpers += extract_block(projection, f"{offset_type} unpackSunSplatOffset(")
        helpers += "\n".join(re.findall(
            r"const(?:ant)? float kShadowBias(?:SlopeMin|TexelScale) = [^;]+;", sampler))
    constants = re.search(
        r"#ifndef IR_SUN_FACE_OVERFLOW_REFERENCE.*?Budget = \d+u;", sampler, re.DOTALL)
    if constants is None:
        raise ValueError("missing overflow reference switch and query budget")
    body = extract_function(sampler, "sampleCascadeShadow")
    if not include_pcf:
        body = body[:body.index("    float slope =")] + "    return 0.0;\n}\n"
    body = "\n".join(extract_function(sampler, name) for name in (
        "sourceSunGeometrySeparation", "sourceSunHierarchyBlocks")) + body
    if include_pcf:
        body = "\n".join(extract_function(sampler, name) for name in (
            "sunStoredTapBlocks", "sunStoredTapBeyondThrow", "sunDepthSlabBlocks")) + body
    if mutation:
        old, new = mutation
        if old not in body:
            raise ValueError(f"mutation subject absent: {old}")
        body = body.replace(old, new)
    call = ("sampleCascadeShadow(uv,z,normal,{0,0,1},{1,0,0},{0,1,0},"
            "{0,0},{1,1},int(cascade)*kCascadeTexelCount,")
    if suffix == "metal":
        call += "sunDepthBuf,"
    call += "maxThrow,surface,{0,0,0,1},{0,0,0}"
    call += ",sunQueryStats)" if query_stats else ")"
    define = "#define IR_SUN_FACE_OVERFLOW_REFERENCE 1\n" if enabled else ""
    if query_stats:
        define += "#define IR_SUN_QUERY_STATS 1\n"
        define += (SHADERS / directory / f"ir_sun_query_stats.{suffix}").read_text()
    storage = load_shader(SHADERS / directory, "ir_sun_geometry_storage", suffix)
    code = PREAMBLE + define + constants[0] + "\n" + layout + storage + helpers + body
    code += f"static_assert(IR_SUN_FACE_OVERFLOW_REFERENCE == {int(enabled)});\n"
    if query_stats:
        code += "SunQueryStats sunQueryStats{};\n"
    code += cases.replace("SAMPLE_CALL", call)
    code = (code.replace("constant uint", "const uint").replace("constant float", "const float")
            .replace("pcfOffset.xy", "vec2(pcfOffset)")
            .replace("inout SunQueryStats sunQueryStats", "SunQueryStats &sunQueryStats")
            .replace("thread SunQueryStats &sunQueryStats", "SunQueryStats &sunQueryStats")
            .replace("device const uint *sunDepthBuf", "BoundBuffer& sunDepthBuf")
            .replace("device const uint* sunDepthBuf", "BoundBuffer& sunDepthBuf")
            .replace("thread ", "")
            .replace("float2", "vec2").replace("float3", "vec3")
            .replace("float4", "vec4").replace("int2", "ivec2"))
    code = re.sub(r"\bout (vec2|float) (\w+)", r"\1 &\2", code)
    code = re.sub(r"\.(xy|xyz)\b(?!\()", r".\1()", code)
    code = code.replace("uint type; };", "uint type; SourceSunGeometry(vec3 c,vec3 u,"
                        "vec3 v,uint t):corner(c),edgeU(u),edgeV(v),type(t){} };")
    return code.replace("vec4 high; };", "vec4 high; SourceSunBounds()=default; "
                        "SourceSunBounds(vec4 l,vec4 h):low(l),high(h){} };")


@unittest.skipUnless(COMPILER, "finite receiver controls require a C++ compiler")
class SourceFaceOverflowTest(unittest.TestCase):
    def test_global_spill_exhaustion_guard_and_mutations(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            with self.subTest(backend=suffix):
                run = self.compile_and_run(host_source(
                    suffix, directory, False, cases=SPILL_POOL_CASES))
                self.assertEqual(run.returncode, 0, run.stderr)
                print(f"{suffix}: {run.stdout}", end="")
            for name, mutation in {
                "missing_global_guard": ("<= kSourceFaceSpillCapacity", "<= 0xffffffffu"),
                "premature_pool_exhaustion": ("<= kSourceFaceSpillCapacity",
                                              "< kSourceFaceSpillCapacity"),
                "demote_inline_tiles": ("count <= kSourceFaceTileCapacity ||", "false ||"),
            }.items():
                with self.subTest(backend=suffix, mutation=name):
                    run = self.compile_and_run(host_source(
                        suffix, directory, False, mutation, SPILL_POOL_CASES))
                    self.assertNotEqual(run.returncode, 0)
                    expected_failure = ("inline query bypasses spill counter"
                                        if name == "demote_inline_tiles" else
                                        "global spill exhaustion guard")
                    self.assertIn(expected_failure, run.stderr)

    def test_reference_and_default_with_independent_geometry(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            for enabled in (False, True):
                with self.subTest(backend=suffix, enabled=enabled):
                    run = self.compile_and_run(host_source(suffix, directory, enabled))
                    self.assertEqual(run.returncode, 0, run.stderr)
                    if enabled:
                        print(f"{suffix}: {run.stdout}", end="")

    def test_incomplete_lists_do_not_choose_coverage_by_reservation_order(self):
        partial_query = (
            "const uint candidateCount = queryAllFaces ? sourceFaceCount : count;\n"
            "            if (!queryHierarchy && sourceQueryComplete && "
            "dot(normal, sunDir) > 0.0) {",
            "const uint candidateCount = queryAllFaces ? sourceFaceCount\n"
            "                : min(count, kSourceFaceTileCapacity);\n"
            "            if ((sourceQueryComplete || sourceFaceCount <= kSourceFaceCapacity)\n"
            "                && dot(normal, sunDir) > 0.0) {",
        )
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            for enabled in (False, True):
                with self.subTest(backend=suffix, enabled=enabled):
                    run = self.compile_and_run(host_source(
                        suffix, directory, enabled, cases=ORDER_CASES))
                    self.assertEqual(run.returncode, 0, run.stderr)
                    print(f"{suffix} reference={enabled}: {run.stdout}", end="")
            with self.subTest(backend=suffix, mutation="partial_exact_query"):
                run = self.compile_and_run(host_source(
                    suffix, directory, False, partial_query, ORDER_CASES))
                self.assertNotEqual(run.returncode, 0)
                self.assertIn("retained subset changes shadow coverage", run.stderr)

    def test_mutation_controls(self):
        mutations = {
            "drop_reference": ("sourceQueryComplete || queryAllFaces", "sourceQueryComplete"),
            "truncate_pool": ("candidate < candidateCount", "candidate + 1u < candidateCount"),
            "scan_beyond_budget": ("sourceFaceCount <= kSourceFaceOverflowReferenceBudget", "true"),
            "wrong_record": ("faceIndex = candidate;", "faceIndex = 0u;"),
            "accept_sample_after_miss": ("if (layer == 1 && sourceQueryComplete) continue;", ""),
            "drop_other_layer": ("layer == 1 && sourceQueryComplete", "sourceQueryComplete"),
            "ignore_list": ("faceIndex = sunDepthBuf[tileBase + 1u + candidate];",
                            "faceIndex = candidate;"),
            "truncate_spill": ("candidate < candidateCount",
                               "candidate < min(candidateCount, kSourceFaceTileCapacity)"),
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

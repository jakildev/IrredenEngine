"""Execute production face indexers with serial atomics and independent tile lists.

Checks projected records and bounded writes, not GPU ordering, caster transforms,
fallback rasterization, or the complete receiver shader.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")

PREAMBLE = r"""
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <vector>
using uint = std::uint32_t;
using atomic_uint = uint;
std::uint64_t atomicAdds=0, atomicMaxes=0, atomicExchanges=0;
std::uint64_t atomicOrs=0;
using std::abs;
constexpr int memory_order_relaxed = 0;
struct vec2 {
    float x,y;
    vec2()=default;
    explicit vec2(float a):x(a),y(a){}
    vec2(float a,float b):x(a),y(b){}
};
struct ivec2 {
    int x,y;
    explicit ivec2(int a):x(a),y(a){}
    explicit ivec2(vec2 a):x(int(a.x)),y(int(a.y)){}
};
struct vec3 {
    float x,y,z;
    vec2 xy() const {return {x,y};}
    float operator[](uint i) const {return i==0?x:(i==1?y:z);}
};
vec2 operator+(vec2 a,vec2 b){return {a.x+b.x,a.y+b.y};}
vec2 operator-(vec2 a,vec2 b){return {a.x-b.x,a.y-b.y};}
vec2 operator/(vec2 a,vec2 b){return {a.x/b.x,a.y/b.y};}
vec2 operator*(vec2 a,float b){return {a.x*b,a.y*b};}
vec2 abs(vec2 a){return {std::abs(a.x),std::abs(a.y)};}
float min(float a,float b){return std::min(a,b);}
float max(float a,float b){return std::max(a,b);}
vec2 min(vec2 a,vec2 b){return {std::min(a.x,b.x),std::min(a.y,b.y)};}
vec2 max(vec2 a,vec2 b){return {std::max(a.x,b.x),std::max(a.y,b.y)};}
vec2 clamp(vec2 a,vec2 low,vec2 high){return min(max(a,low),high);}
ivec2 min(ivec2 a,ivec2 b){return ivec2(vec2(std::min(a.x,b.x),std::min(a.y,b.y)));}
ivec2 max(ivec2 a,ivec2 b){return ivec2(vec2(std::max(a.x,b.x),std::max(a.y,b.y)));}
vec2 floor(vec2 a){return {std::floor(a.x),std::floor(a.y)};}
uint floatBitsToUint(float a){uint b;std::memcpy(&b,&a,4);return b;}
float uintBitsToFloat(uint a){float b;std::memcpy(&b,&a,4);return b;}
template<class T,class U> T as_type(U a){T b;std::memcpy(&b,&a,4);return b;}
uint atomicAdd(uint& p,uint n){++atomicAdds;uint old=p;p+=n;return old;}
uint atomicMax(uint& p,uint n){++atomicMaxes;uint old=p;p=std::max(p,n);return old;}
uint atomicExchange(uint& p,uint n){++atomicExchanges;uint old=p;p=n;return old;}
uint atomicOr(uint& p,uint n){++atomicOrs;uint old=p;p|=n;return old;}
uint atomic_fetch_or_explicit(uint* p,uint n,int){return atomicOr(*p,n);}
uint atomic_exchange_explicit(uint* p,uint n,int){return atomicExchange(*p,n);}
uint atomic_fetch_add_explicit(uint* p,uint n,int){return atomicAdd(*p,n);}
uint atomic_fetch_max_explicit(uint* p,uint n,int){return atomicMax(*p,n);}
void atomic_store_explicit(uint* p,uint n,int){*p=n;}
struct FrameDataSun {
    vec2 cascadeOriginUV_0{0,0},cascadeOriginUV_1{-8,-8};
    vec2 cascadeTexelSize_0{1,1},cascadeTexelSize_1{2,3};
};
FrameDataSun sunFrame;
const vec2 cascadeOriginUV_0{0,0},cascadeOriginUV_1{-8,-8};
const vec2 cascadeTexelSize_0{1,1},cascadeTexelSize_1{2,3};
// GLSL indexes the bound SSBO and reads its length(); Metal takes the raw pointer.
struct BoundBuffer {
    uint* data;
    uint words;
    uint& operator[](uint i) const {return data[i];}
    int length() const {return int(words);}
};
BoundBuffer sunDepthBuf;
"""

CASES = r"""
constexpr uint guardWords=32, canary=0xa5c31e79u, unwritten=0xccccccccu;
std::vector<uint> actual(kSourceFaceBufferWords+2*guardWords,canary),expected;
uint* oracle;
void reset() {
    atomicAdds=atomicMaxes=atomicExchanges=atomicOrs=0;
    std::fill(actual.begin(),actual.end(),canary);
    sunDepthBuf={actual.data()+guardWords,kSourceFaceBufferWords};
    std::fill(sunDepthBuf.data,sunDepthBuf.data+kSourceFaceBufferWords,unwritten);
    sunDepthBuf[kSourceFaceHeaderOffset]=0;
    sunDepthBuf[kSourceFaceSpillCounterOffset]=0;
    sunDepthBuf[kSourceFaceHierarchyNeededOffset]=0;
    sunDepthBuf[kSourceFaceHierarchyReadyOffset]=0;
    for(uint tile=0;tile<32768;++tile) {
        sunDepthBuf[kSourceFaceTileOffset+tile*65]=0;
        sunDepthBuf[kSourceFaceSpillHeadOffset+tile]=0xffffffffu;
    }
    expected=actual;
    oracle=expected.data()+guardWords;
}
void report(const char* label) {
    uint occupied=0,incomplete=0,peak=0;
    for(uint tile=0;tile<32768;++tile) {
        const uint count=sunDepthBuf[kSourceFaceTileOffset+tile*65];
        occupied+=count>0;
        incomplete+=!sourceFaceQueryComplete(count);
        peak=std::max(peak,count);
    }
    std::cout<<label<<": allocation_attempts="<<sunDepthBuf[kSourceFaceHeaderOffset]
             <<" occupied="<<occupied<<" incomplete="<<incomplete
             <<" peak_raw_count="<<peak<<" atomic_add="<<atomicAdds
             <<" atomic_max="<<atomicMaxes<<"\n";
}
bool check(const char* label) {
    for(std::size_t i=0;i<actual.size();++i) if(actual[i]!=expected[i]) {
        std::cerr<<label<<" word "<<i<<" got "<<actual[i]<<" expected "<<expected[i]<<"\n";
        return false;
    }
    return true;
}
// Explicit fixture tile IDs are independent of the shader's bounds arithmetic.
void emit(vec3 corner,vec3 u,vec3 v,const std::vector<uint>& tiles) {
    const auto addsBefore=atomicAdds, maxesBefore=atomicMaxes, exchangesBefore=atomicExchanges;
    const auto orsBefore=atomicOrs;
    uint expectedAdds=0,expectedMaxes=0,expectedExchanges=0,expectedOrs=0;
    CALL_INDEX
    if(!tiles.empty()) {
        const uint id=oracle[kSourceFaceHeaderOffset]++;
        ++expectedAdds;
        // The first lost record requests the retained-pool hierarchy.
        if(id==65536) {
            oracle[kSourceFaceHierarchyNeededOffset]=1;
            expectedExchanges+=HIERARCHY_FLAG_USES_EXCHANGE;
        }
        if(id<65536) {
            oracle[kSourceFaceRecordOffset+id*kSourceFaceRecordWords+9]=0;
            const vec3 vectors[]={corner,u,v};
            for(uint vector=0;vector<3;++vector) for(uint axis=0;axis<3;++axis)
                oracle[kSourceFaceRecordOffset+id*kSourceFaceRecordWords+vector*3+axis]=floatBitsToUint(vectors[vector][axis]);
        }
        for(uint tile:tiles) {
            uint* list=oracle+kSourceFaceTileOffset+tile*65;
            if(id>=65536) {
                list[0]|=0x80000000u;
                ++expectedOrs;
                continue;
            }
            const uint count=list[0]++;
            ++expectedAdds;
            if(count==256) {
                oracle[kSourceFaceHierarchyNeededOffset]=1;
                expectedExchanges+=HIERARCHY_FLAG_USES_EXCHANGE;
            }
            if(count<64) list[count+1]=id;
            else if(count<256) {
                const uint slot=oracle[kSourceFaceSpillCounterOffset]++;
                ++expectedAdds;
                if(slot==262144) {
                    oracle[kSourceFaceHierarchyNeededOffset]=1;
                    expectedExchanges+=HIERARCHY_FLAG_USES_EXCHANGE;
                }
                if(slot>=262144) {
                    list[0]=std::max(list[0],257u);
                    ++expectedMaxes;
                    continue;
                }
                const uint node=kSourceFaceSpillNodeOffset+slot*2;
                oracle[node]=id;
                oracle[node+1]=oracle[kSourceFaceSpillHeadOffset+tile];
                oracle[kSourceFaceSpillHeadOffset+tile]=slot;
                ++expectedExchanges;
            }
        }
    }
    if(CHECK_ATOMIC_WORKLOAD &&
       (atomicAdds-addsBefore!=expectedAdds || atomicMaxes-maxesBefore!=expectedMaxes ||
        atomicExchanges-exchangesBefore!=expectedExchanges || atomicOrs-orsBefore!=expectedOrs)) {
        std::cerr<<"unexpected index atomic workload\n";
        std::exit(18);
    }
}
vec3 storedVector(uint record,uint offset) {
    float value[3];
    for(uint i=0;i<3;++i) std::memcpy(&value[i],sunDepthBuf.data+record+offset+i,4);
    return {value[0],value[1],value[2]};
}
std::vector<uint> candidates(uint tile) {
    const uint base=kSourceFaceTileOffset+tile*65;
    const uint count=sunDepthBuf[base];
    if(!sourceFaceQueryComplete(count)) return {};
    std::vector<uint> ids;
    for(uint i=0;i<std::min(count,64u);++i) ids.push_back(sunDepthBuf[base+1+i]);
    if(count>64) {
        uint node=sunDepthBuf[kSourceFaceSpillHeadOffset+tile];
        for(uint i=64;i<count;++i) {
            if(node>=262144) {std::cerr<<"missing or poisoned spill node\n";std::exit(30);}
            const uint address=kSourceFaceSpillNodeOffset+2*node;
            ids.push_back(sunDepthBuf[address]);
            node=sunDepthBuf[address+1];
        }
        if(node!=0xffffffffu) {std::cerr<<"stale or cyclic spill tail\n";std::exit(31);}
    }
    for(uint id:ids) if(id>=std::min(65536u,sunDepthBuf[kSourceFaceHeaderOffset])) {
        std::cerr<<"unpublished face ID\n";std::exit(32);
    }
    std::sort(ids.begin(),ids.end());
    if(std::adjacent_find(ids.begin(),ids.end())!=ids.end()) {
        std::cerr<<"duplicate candidate ID\n";std::exit(33);
    }
    return ids;
}
bool allCandidates(uint tile,uint first,uint count) {
    const auto ids=candidates(tile);
    if(ids.size()!=count) return false;
    for(uint i=0;i<count;++i) if(ids[i]!=first+i) return false;
    return true;
}
int main() {
    static_assert(kSourceFaceCapacity==65536 && kSourceFaceTileCapacity==64);
    static_assert(kSourceFaceTileCount==32768 && kSourceFaceRecordWords==10);
    static_assert(kSourceFaceSpillCapacity==262144 && kSourceFaceTileQueryCapacity==256);
    const vec3 corner{7,7,2},u{3,0,1.5f},v{0,3,-.75f};
    reset();
    emit(corner,u,v,{0,1,128,129,16384,16385});
    if(!check("both cascade coverage")) return 1;
    const uint record=kSourceFaceRecordOffset;
    if(sourceFaceRaySeparation({8.5f,8.5f},4,storedVector(record,0),
                              storedVector(record,3),storedVector(record,6))!=1.625f) return 2;
    if(sourceFaceRaySeparation({10.25f,8.5f},4,storedVector(record,0),
                              storedVector(record,3),storedVector(record,6))!=-1) return 3;
    emit({10,7,3.5f},{-3,0,-1.5f},v,{0,1,128,129,16384,16385});
    emit(corner,v,u,{0,1,128,129,16384,16385});
    if(!check("reflected and reversed edges")) return 4;
    reset();
    emit({7,7,0},{1,0,0},{0,1,0},{0,1,128,129,16384,16385});
    emit({-1,-1,0},{2,0,0},{0,2,0},{0,16384});
    // At the near map's last column the far map still contains the entire face.
    emit({1023,7,0},{2,0,0},{0,1,0},{127,255,16448});
    emit({1200,32,2},u,v,{16587});
    emit({-5,1,2},u,v,{16384});
    if(!check("tile and map boundaries")) return 5;
    reset();
    for(uint n=0;n<64;++n) emit(corner,u,v,{0,1,128,129,16384,16385});
    if(!check("64 candidates") ||
       !sourceFaceQueryComplete(sunDepthBuf[kSourceFaceTileOffset])) return 6;
    report("small-64");
    emit(corner,u,v,{0,1,128,129,16384,16385});
    report("small-65");
    if(!check("65 candidates") ||
       !sourceFaceQueryComplete(sunDepthBuf[kSourceFaceTileOffset]) ||
       !allCandidates(0,0,65) || !allCandidates(16385,0,65)) return 7;
    emit(corner,u,v,{0,1,128,129,16384,16385});
    if(!check("66 candidates remain bounded")) return 19;
    report("small-66");
    for(uint n=66;n<256;++n) emit(corner,u,v,{0,1,128,129,16384,16385});
    if(!check("256 candidates") || !allCandidates(0,0,256) ||
       !allCandidates(16385,0,256)) return 20;
    const uint spillAtLimit=sunDepthBuf[kSourceFaceSpillCounterOffset];
    emit(corner,u,v,{0,1,128,129,16384,16385});
    if(!check("257 candidates") || !candidates(0).empty() ||
       sunDepthBuf[kSourceFaceSpillCounterOffset]!=spillAtLimit) return 21;
    report("small-257");
    reset();
    emit({-100,-100,2},u,v,{});
    if(!check("off-map faces consume no quota")) return 8;
    for(uint n=0;n<65535;++n) emit(corner,u,v,{0,1,128,129,16384,16385});
    if(!check("global quota approach")) return 9;
    emit({32,32,2},u,v,{387,388,515,516,16514});
    if(!check("65536th record") || sunDepthBuf[kSourceFaceHeaderOffset]!=65536) return 9;
    emit({32,32,2},u,v,{387,388,515,516,16514});
    if(!check("65537th record fallback") ||
       sourceFaceQueryComplete(sunDepthBuf[kSourceFaceTileOffset+516*65]) ||
       sourceFaceTileRetainsRecords(sunDepthBuf[kSourceFaceTileOffset+516*65]) ||
       !sourceFaceTileRetainsRecords(sunDepthBuf[kSourceFaceTileOffset]) ||
       sunDepthBuf[kSourceFaceHierarchyNeededOffset]!=1) return 10;
    emit({48,48,2},u,v,{645,646,773,774,16643});
    if(!check("overflow on previously empty tiles")) return 11;
    report("global-exhaustion");
    if(!candidates(774).empty() || sunDepthBuf[kSourceFaceTileOffset+774*65+1]!=unwritten)
        return 22;
    emit({-100,-100,2},u,v,{});
    if(!check("off-map overflow")) return 12;
    reset();
    INDEX_DEGENERATE
    if(!check("degenerate projection")) return 13;
    reset();
    std::vector<uint> allTiles;
    for(uint tile=0;tile<32768;++tile) allTiles.push_back(tile);
    for(uint n=0;n<64;++n) emit({-8,-8,2},{4096,0,0},{0,4096,0},allTiles);
    if(!check("large faces cover both complete cascades")) return 14;
    for(uint tile:allTiles)
        if(!sourceFaceQueryComplete(sunDepthBuf[kSourceFaceTileOffset+tile*65])) return 15;
    report("full-map-64");
    emit({-8,-8,2},{4096,0,0},{0,4096,0},allTiles);
    report("full-map-65");
    if(!check("large face overflow keeps every tile bounded")) return 16;
    for(uint tile:allTiles)
        if(!allCandidates(tile,0,65)) return 17;
    std::cout<<"large-face control: 32768 tile entries per face; "
             <<"65 overlapping faces retain complete candidates in both cascades\n";

    reset();
    for(uint n=0;n<64;++n) emit(corner,u,v,{0,1,128,129,16384,16385});
    sunDepthBuf[kSourceFaceSpillCounterOffset]=oracle[kSourceFaceSpillCounterOffset]=262143;
    emit(corner,u,v,{0,1,128,129,16384,16385});
    if(!check("last spill node then pool exhaustion") || !allCandidates(0,0,65)) return 23;
    for(uint tile:{1u,128u,129u,16384u,16385u}) if(!candidates(tile).empty()) return 24;
    const uint failedCount=sunDepthBuf[kSourceFaceTileOffset+65];
    emit(corner,u,v,{0,1,128,129,16384,16385});
    if(!check("pool failure stays incomplete") || !candidates(0).empty() ||
       sunDepthBuf[kSourceFaceTileOffset+65]<=failedCount) return 25;
    emit({32,32,2},u,v,{387,388,515,516,16514});
    if(!check("full pool leaves primary lists usable") || !allCandidates(516,66,1)) return 26;

    sunDepthBuf[kSourceFaceHierarchyReadyOffset]=oracle[kSourceFaceHierarchyReadyOffset]=8192;
    const auto stale=actual;
    for(uint index=0;index<kSourceFaceFallbackOffset+17;++index) clearIndex(index);
    for(uint i=0;i<kSourceFaceFallbackOffset*2;++i) oracle[i]=0xffffffffu;
    oracle[kSourceFaceHeaderOffset]=oracle[kSourceFaceSpillCounterOffset]=0;
    oracle[kSourceFaceHierarchyNeededOffset]=oracle[kSourceFaceHierarchyReadyOffset]=0;
    for(uint tile=0;tile<32768;++tile) {
        oracle[kSourceFaceTileOffset+tile*65]=0;
        oracle[kSourceFaceSpillHeadOffset+tile]=0xffffffffu;
    }
    if(!check("production clear resets counters and heads without erasing records")) return 27;
    if(!std::equal(actual.begin()+guardWords+kSourceFaceSpillNodeOffset,
                   actual.begin()+guardWords+kSourceFaceHierarchyNeededOffset,
                   stale.begin()+guardWords+kSourceFaceSpillNodeOffset)) return 28;
    for(uint n=0;n<65;++n) emit(corner,u,v,{0,1,128,129,16384,16385});
    if(!check("reused nodes have no stale tail") || !allCandidates(0,0,65)) return 29;

    // Record loss alone, with no tile overflow, must still request the retained-pool hierarchy.
    reset();
    sunDepthBuf[kSourceFaceHeaderOffset]=oracle[kSourceFaceHeaderOffset]=65536;
    emit({32,32,2},u,v,{387,388,515,516,16514});
    if(!check("isolated record loss") || sourceFaceTileRetainsRecords(
           sunDepthBuf[kSourceFaceTileOffset+387*65])) return 38;

    for(uint seed=1;seed<=12;++seed) {
        reset();
        std::vector<uint> identities;
        for(uint i=0;i<256;++i) identities.push_back(i);
        uint random=seed;
        for(uint i=256;i>1;--i) {
            random=random*1664525u+1013904223u;
            std::swap(identities[i-1],identities[random%i]);
        }
        for(uint identity:identities)
            emit({7,7,float(identity)},u,v,{0,1,128,129,16384,16385});
        if(!check("permuted complete candidates")) return 34;
        for(uint tile:{0u,16385u}) {
            std::vector<uint> recovered;
            for(uint id:candidates(tile)) recovered.push_back(uint(storedVector(
                kSourceFaceRecordOffset+id*kSourceFaceRecordWords,0).z));
            std::sort(recovered.begin(),recovered.end());
            if(recovered.size()!=256) return 35;
            for(uint i=0;i<256;++i) if(recovered[i]!=i) return 36;
        }
        emit(corner,u,v,{0,1,128,129,16384,16385});
        if(!candidates(0).empty() || !check("permuted query exhaustion")) return 37;
    }
    std::cout<<"spill boundary, pool exhaustion, stale reset and 12 insertion permutations pass\n";
}
"""


def load_shader(shaders, name, suffix):
    source = (shaders / f"{name}.{suffix}").read_text()
    return re.sub(r'#include "(\w+)\.' + suffix + r'"',
                  lambda match: load_shader(shaders, match[1], suffix), source)


def shader_to_cpp(source):
    source = (source.replace("constant uint", "const uint")
              .replace("constant FrameDataSun&", "const FrameDataSun&")
              .replace("device atomic_uint*", "atomic_uint*")
              .replace("thread ", "")
              .replace("float2", "vec2").replace("float3", "vec3")
              .replace("int2", "ivec2"))
    source = re.sub(r"\bout (vec2|ivec2|float) (\w+)", r"\1 &\2", source)
    return re.sub(r"\.xy\b(?!\()", ".xy()", source)


@unittest.skipUnless(COMPILER, "source-face index controls require a C++ compiler")
class SourceFaceIndexTest(unittest.TestCase):
    def test_production_indexers_and_mutation_controls(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            shaders = ROOT / "engine/render/src/shaders" / directory
            source = load_shader(shaders, "ir_sun_face_index", suffix)
            indexer = source
            layout = load_shader(shaders, "ir_sun_face_query_layout", suffix)
            clear = (shaders / f"c_clear_sun_shadow_map.{suffix}").read_text()
            clear = clear[clear.index("    if (linearIdx >= kTotalTexels)"):]
            variants = {
                "production": indexer,
                "missing_cascade": indexer.replace(
                    "cascade < kSourceFaceCascadeCount", "cascade < 1u"),
                "unmarked_global_overflow": indexer.replace(
                    "kSourceFaceTileQueryCapacity + 1u", "0u"),
                "unmarked_record_loss": indexer.replace("kSourceFaceTileRecordLost", "0u"),
                "record_loss_as_tile_overflow": indexer.replace(
                    "kSourceFaceTileRecordLost", "kSourceFaceTileQueryCapacity + 1u"),
                "missing_record_hierarchy_request": indexer.replace(
                    "faceIndex == kSourceFaceCapacity", "false"),
                "tile_list_overrun": indexer.replace(
                    "slot < kSourceFaceTileCapacity", "slot <= kSourceFaceTileCapacity"),
                "record_overrun": indexer.replace(
                    "faceIndex < kSourceFaceCapacity", "faceIndex <= kSourceFaceCapacity"),
                "corrupt_record": indexer.replace("corner[axis]", "edgeU[axis]"),
                "unculled_off_map": indexer.replace(
                    "if (first.x > last.x || first.y > last.y) continue;", ""),
                "spill_pool_overrun": indexer.replace(
                    "spill < kSourceFaceSpillCapacity", "spill <= kSourceFaceSpillCapacity"),
                "spill_query_overrun": indexer.replace(
                    "slot < kSourceFaceTileQueryCapacity", "slot <= kSourceFaceTileQueryCapacity"),
                "missing_spill_candidates": indexer.replace(
                    "slot < kSourceFaceTileQueryCapacity", "false"),
                "wrong_spill_record": indexer.replace(
                    "sunDepthBuf[node] = faceIndex", "sunDepthBuf[node] = 0u") if suffix == "glsl"
                else indexer.replace("&sunDepthBuf[node], faceIndex", "&sunDepthBuf[node], 0u"),
                "stale_spill_heads": indexer,
                "stale_spill_counter": indexer,
                "stale_hierarchy_needed": indexer,
                "stale_hierarchy_ready": indexer,
                "missing_tile_hierarchy_request": indexer.replace(
                    "slot == kSourceFaceTileQueryCapacity", "false"),
                "missing_spill_hierarchy_request": indexer.replace(
                    "spill == kSourceFaceSpillCapacity", "false"),
            }
            layouts = dict.fromkeys(variants, layout)
            clears = dict.fromkeys(variants, clear)
            clears["stale_spill_heads"] = re.sub(
                r"[^\n]*sunDepthBuf\[kSourceFaceSpillHeadOffset[^\n]*\n", "", clear)
            clears["stale_spill_counter"] = re.sub(
                r"[^\n]*(?:sourceFaceSpillCounterIndex|kSourceFaceSpillCounterOffset)[^\n]*\n",
                "", clear)
            for name in ("Needed", "Ready"):
                clears[f"stale_hierarchy_{name.lower()}"] = re.sub(
                    rf"[^\n]*(?:sourceFaceHierarchy{name}Index|"
                    rf"kSourceFaceHierarchy{name}Offset)[^\n]*\n", "", clear)
            if suffix == "glsl":
                variants["header_index_off_by_one"] = indexer
                layouts["header_index_off_by_one"] = layout.replace(
                    "kSourceFaceBufferWords - kSourceFaceHeaderOffset)",
                    "kSourceFaceBufferWords - kSourceFaceHeaderOffset - 1u)")
                clears["header_index_off_by_one"] = clear
                variants["spill_counter_index_off_by_one"] = indexer
                layouts["spill_counter_index_off_by_one"] = layout.replace(
                    "kSourceFaceBufferWords - kSourceFaceSpillCounterOffset)",
                    "kSourceFaceBufferWords - kSourceFaceSpillCounterOffset - 1u)")
                clears["spill_counter_index_off_by_one"] = clear
            call = ("indexSourceSunFace(corner,u,v);" if suffix == "glsl" else
                    "indexSourceSunFace(sunDepthBuf.data,corner,u,v,sunFrame);")
            degenerate = ("indexSourceSunFace(corner,u,u);" if suffix == "glsl" else
                          "indexSourceSunFace(sunDepthBuf.data,corner,u,u,sunFrame);")
            cases = (CASES.replace("CALL_INDEX", call).replace("INDEX_DEGENERATE", degenerate)
                     .replace("HIERARCHY_FLAG_USES_EXCHANGE", str(int(suffix == "glsl"))))
            for variant, body in variants.items():
                with (self.subTest(backend=suffix, variant=variant),
                      tempfile.TemporaryDirectory() as temporary):
                    if variant != "production":
                        self.assertNotEqual((layouts[variant], body, clears[variant]),
                                            (layout, indexer, clear))
                    shader = layouts[variant] + "\n" + body
                    clear_function = (
                        "void clearIndex(uint linearIdx) {\n" if suffix == "glsl" else
                        "void clearIndexImpl(uint* sunDepthBuf,uint linearIdx) {\n")
                    shader += "\n" + clear_function + (
                        "const uint kTotalTexels=kSourceFaceFallbackOffset;\n" + clears[variant])
                    if suffix == "metal":
                        shader += ("\nvoid clearIndex(uint index) {"
                                   "clearIndexImpl(sunDepthBuf.data,index);}\n")
                    shader = shader_to_cpp(shader)
                    path = Path(temporary)
                    (path / "index.cpp").write_text(
                        PREAMBLE + shader + cases.replace(
                            "CHECK_ATOMIC_WORKLOAD",
                            "true" if variant == "production" else "false"))
                    build = subprocess.run(
                        [COMPILER, "-std=c++17", "-O2", str(path / "index.cpp"),
                         "-o", str(path / "index")], capture_output=True, text=True,
                    )
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(path / "index")], capture_output=True, text=True)
                    if variant == "production":
                        self.assertEqual(run.returncode, 0, run.stderr)
                        print(f"{suffix}:\n{run.stdout}", end="")
                    else:
                        self.assertNotEqual(run.returncode, 0, "mutation escaped the oracle")


if __name__ == "__main__":
    unittest.main()

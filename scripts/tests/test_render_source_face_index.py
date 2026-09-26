"""Execute production face indexers with serial atomics and independent tile lists.

Checks projected records and bounded writes, not GPU ordering, caster transforms,
fallback rasterization, or the complete receiver shader.
"""

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
std::uint64_t atomicAdds=0, atomicMaxes=0;
using std::abs;
constexpr int memory_order_relaxed = 0;
struct vec2 {
    float x,y;
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
vec2 min(vec2 a,vec2 b){return {std::min(a.x,b.x),std::min(a.y,b.y)};}
vec2 max(vec2 a,vec2 b){return {std::max(a.x,b.x),std::max(a.y,b.y)};}
ivec2 min(ivec2 a,ivec2 b){return ivec2(vec2(std::min(a.x,b.x),std::min(a.y,b.y)));}
ivec2 max(ivec2 a,ivec2 b){return ivec2(vec2(std::max(a.x,b.x),std::max(a.y,b.y)));}
vec2 floor(vec2 a){return {std::floor(a.x),std::floor(a.y)};}
uint floatBitsToUint(float a){uint b;std::memcpy(&b,&a,4);return b;}
template<class T> T as_type(float a){T b;std::memcpy(&b,&a,4);return b;}
uint atomicAdd(uint& p,uint n){++atomicAdds;uint old=p;p+=n;return old;}
uint atomicMax(uint& p,uint n){++atomicMaxes;uint old=p;p=std::max(p,n);return old;}
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
    atomicAdds=atomicMaxes=0;
    std::fill(actual.begin(),actual.end(),canary);
    sunDepthBuf={actual.data()+guardWords,kSourceFaceBufferWords};
    std::fill(sunDepthBuf.data,sunDepthBuf.data+kSourceFaceBufferWords,unwritten);
    sunDepthBuf[kSourceFaceHeaderOffset]=0;
    for(uint tile=0;tile<32768;++tile)
        sunDepthBuf[kSourceFaceTileOffset+tile*65]=0;
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
    const auto addsBefore=atomicAdds, maxesBefore=atomicMaxes;
    const uint expectedId=oracle[kSourceFaceHeaderOffset];
    CALL_INDEX
    const bool allocated=tiles.size()>0;
    const bool exact=expectedId<65536;
    const auto expectedAdds=allocated ? 1+(exact ? tiles.size() : 0) : 0;
    const auto expectedMaxes=allocated && !exact ? tiles.size() : 0;
    if(CHECK_ATOMIC_WORKLOAD &&
       (atomicAdds-addsBefore!=expectedAdds || atomicMaxes-maxesBefore!=expectedMaxes)) {
        std::cerr<<"unexpected index atomic workload\n";
        std::exit(18);
    }
    if(tiles.size()==0) return;
    const uint id=oracle[kSourceFaceHeaderOffset]++;
    if(id<65536) {
        const vec3 vectors[]={corner,u,v};
        for(uint vector=0;vector<3;++vector) for(uint axis=0;axis<3;++axis)
            oracle[kSourceFaceRecordOffset+id*9+vector*3+axis]=floatBitsToUint(vectors[vector][axis]);
    }
    for(uint tile:tiles) {
        uint* list=oracle+kSourceFaceTileOffset+tile*65;
        if(id>=65536) list[0]=std::max(list[0],65u);
        else {
            const uint count=list[0]++;
            if(count<64) list[count+1]=id;
        }
    }
}
vec3 storedVector(uint record,uint offset) {
    float value[3];
    for(uint i=0;i<3;++i) std::memcpy(&value[i],sunDepthBuf.data+record+offset+i,4);
    return {value[0],value[1],value[2]};
}
int main() {
    static_assert(kSourceFaceCapacity==65536 && kSourceFaceTileCapacity==64);
    static_assert(kSourceFaceTileCount==32768 && kSourceFaceRecordWords==9);
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
       sourceFaceQueryComplete(sunDepthBuf[kSourceFaceTileOffset])) return 7;
    emit(corner,u,v,{0,1,128,129,16384,16385});
    if(!check("66 candidates remain bounded")) return 19;
    report("small-66");
    reset();
    emit({-100,-100,2},u,v,{});
    if(!check("off-map faces consume no quota")) return 8;
    for(uint n=0;n<65535;++n) emit(corner,u,v,{0,1,128,129,16384,16385});
    if(!check("global quota approach")) return 9;
    emit({32,32,2},u,v,{516,16514});
    if(!check("65536th record") || sunDepthBuf[kSourceFaceHeaderOffset]!=65536) return 9;
    emit({32,32,2},u,v,{516,16514});
    if(!check("65537th record fallback") ||
       sourceFaceQueryComplete(sunDepthBuf[kSourceFaceTileOffset+516*65])) return 10;
    emit({48,48,2},u,v,{774,16643});
    if(!check("overflow on previously empty tiles")) return 11;
    report("global-exhaustion");
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
        if(sourceFaceQueryComplete(sunDepthBuf[kSourceFaceTileOffset+tile*65])) return 17;
    std::cout<<"large-face control: 32768 tile entries per face; "
             <<"65 overlapping faces make both cascades incomplete\n";
}
"""


@unittest.skipUnless(COMPILER, "source-face index controls require a C++ compiler")
class SourceFaceIndexTest(unittest.TestCase):
    def test_production_indexers_and_mutation_controls(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            shaders = ROOT / "engine/render/src/shaders" / directory
            source = (shaders / f"ir_sun_face_index.{suffix}").read_text()
            indexer = source
            layout = (shaders / f"ir_sun_face_query_layout.{suffix}").read_text()
            geometry = (shaders / f"ir_projected_face.{suffix}").read_text()
            layout = layout.replace(f'#include "ir_projected_face.{suffix}"', geometry)
            variants = {
                "production": indexer,
                "missing_cascade": indexer.replace(
                    "cascade < kSourceFaceCascadeCount", "cascade < 1u"),
                "unmarked_global_overflow": indexer.replace(
                    "kSourceFaceTileCapacity + 1u", "0u"),
                "tile_list_overrun": indexer.replace(
                    "slot < kSourceFaceTileCapacity", "slot <= kSourceFaceTileCapacity"),
                "record_overrun": indexer.replace(
                    "faceIndex < kSourceFaceCapacity", "faceIndex <= kSourceFaceCapacity"),
                "corrupt_record": indexer.replace("corner[axis]", "edgeU[axis]"),
                "unculled_off_map": indexer.replace(
                    "if (first.x > last.x || first.y > last.y) continue;", ""),
            }
            layouts = dict.fromkeys(variants, layout)
            if suffix == "glsl":
                variants["header_index_off_by_one"] = indexer
                layouts["header_index_off_by_one"] = layout.replace(
                    "kSourceFaceBufferWords - kSourceFaceHeaderOffset)",
                    "kSourceFaceBufferWords - kSourceFaceHeaderOffset - 1u)")
            call = ("indexSourceSunFace(corner,u,v);" if suffix == "glsl" else
                    "indexSourceSunFace(sunDepthBuf.data,corner,u,v,sunFrame);")
            degenerate = ("indexSourceSunFace(corner,u,u);" if suffix == "glsl" else
                          "indexSourceSunFace(sunDepthBuf.data,corner,u,u,sunFrame);")
            cases = CASES.replace("CALL_INDEX", call).replace("INDEX_DEGENERATE", degenerate)
            for variant, body in variants.items():
                with (self.subTest(backend=suffix, variant=variant),
                      tempfile.TemporaryDirectory() as temporary):
                    if variant != "production":
                        self.assertNotEqual((layouts[variant], body), (layout, indexer))
                    shader = layouts[variant] + "\n" + body
                    shader = (shader.replace("constant uint", "const uint")
                              .replace("constant FrameDataSun&", "const FrameDataSun&")
                              .replace("device atomic_uint*", "atomic_uint*")
                              .replace("float2", "vec2").replace("float3", "vec3")
                              .replace("int2", "ivec2").replace(".xy", ".xy()"))
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

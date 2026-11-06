"""Depth-slab extrema for the raster-only sun layer: proofs and executed controls.

Part one proves, against brute force, that per-slab minimum and maximum
answer interval existence exactly when the slab is no wider than the query
window, and finds the counterexample when it is wider. Part two runs the
production PCF predicate on a slab whose two extrema are both rejected while
an interior write blocks the receiver, so extrema alone cannot replace the
full write set. Part three states the storage arithmetic.

Part four executes the primary-layer depth-slab writer, clear and receiver in
both dialects at three slots (the shipped layout compiles the layer out). An
independent oracle applies the production per-word predicate to every write a
texel received. Where the texel kept every slab and the nearest word lies
beyond the throw, the slab-backed sampler must agree with that oracle for
depth-decided words, never remove the nearest word's shadow, never add a
shadow no write supports, and answer independently of write order. An
overflowed texel keeps the nearest-only answer. Mutations of the writer, the
clear and the receiver each fail a named control.

Part five emulates the workgroup-cooperative splat lane by lane, the kernel's
barriers as phase boundaries (init, anchor reduction, routed splat, flush),
and requires the planes it leaves to equal the per-pixel writer's on every
write set: boxes inside the window, boxes spilling past it, boxes leaving the
cascade, four slabs on one texel, slabs the global planes already hold, both
lane orders. A dropped flush, a spill routed nowhere, a shifted flush origin
and an inclusive window bound each fail the comparison.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import test_render_source_face_overflow as overflow
from shader_contract_helpers import extract_block

COMPILER = shutil.which("c++")
SHADERS = overflow.SHADERS
MAIN = "int main(){"

SLAB_PROOF = r"""
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
// Depths are quantized integers; the window (lo, hi) is open at both ends.
struct Slabs {
    int width; std::vector<int> lo, hi;
    explicit Slabs(int w, int count) : width(w), lo(count, 1 << 30), hi(count, -(1 << 30)) {}
    void insert(int z) {
        const int s = z / width; lo[s] = std::min(lo[s], z); hi[s] = std::max(hi[s], z);
    }
    bool occupied(int s) const { return lo[s] <= hi[s]; }
    bool blocks(int low, int high) const {
        const int first = low / width, last = high / width;
        if (first == last) return occupied(first) && hi[first] > low && lo[first] < high;
        if (occupied(first) && hi[first] > low) return true;
        if (occupied(last) && lo[last] < high) return true;
        for (int s = first + 1; s < last; ++s) if (occupied(s)) return true;
        return false;
    }
};
bool brute(const std::vector<int>& set, int low, int high) {
    for (int z : set) if (z > low && z < high) return true;
    return false;
}
int main() {
    const int domain = 40;
    std::vector<std::vector<int>> sets;
    for (int a = 0; a < domain; ++a) {
        sets.push_back({a});
        for (int b = a + 1; b < domain; ++b) {
            sets.push_back({a, b});
            for (int c = b + 1; c < domain; ++c) sets.push_back({a, b, c});
        }
    }
    long exact = 0, mismatches = 0;
    for (int window : {8, 10, 13}) for (int width : {4, 7, 8, 10, 13}) {
        if (width > window) continue;
        for (const auto& set : sets) {
            Slabs slabs(width, domain / width + 2);
            for (int z : set) slabs.insert(z);
            for (int high = window; high <= domain; ++high) {
                const int low = high - window;
                if (slabs.blocks(low, high) != brute(set, low, high)) {
                    std::cerr << "slab extrema disagree with brute force\n"; return 1;
                }
                ++exact;
            }
        }
    }
    for (int window : {8}) for (int width : {9, 16}) {
        for (const auto& set : sets) {
            Slabs slabs(width, domain / width + 2);
            for (int z : set) slabs.insert(z);
            for (int high = window; high <= domain; ++high) {
                const int low = high - window;
                if (slabs.blocks(low, high) != brute(set, low, high)) ++mismatches;
            }
        }
    }
    if (mismatches == 0) { std::cerr << "wide slabs never mislead the extrema query\n"; return 1; }
    std::cout << exact << " exact slab queries; " << mismatches
              << " wide-slab mismatches when both window ends share a slab\n";
}
"""

# Receiver tilted toward +X so its plane gradient is one depth unit per sun
# texel: a splat displaced +7 texels is coplanar at depth 3, one displaced -7
# at depth 17, while a direct write at depth 6 is a genuine blocker.
PROVENANCE = overflow.CASES[:overflow.CASES.index("int main(){")] + r"""
uint word(float z,uint low){return (depth(z)&0xffffff00u)|low;}
void tap(uint cascade,uint primary,uint second){
    const uint address=cascade*kCascadeTexelCount+3*kSunShadowMapDim+2;
    sunDepthBuf.words[address]=primary;
    sunDepthBuf.words[kSourceFaceFallbackOffset+address]=second;
}
float query(uint cascade){
    return sample({2.5f,3.5f},10,cascade,false,{0.70710678f,0,0.70710678f},64);
}
int main(){
    const uint empty=0xffffffffu;
    const uint nearSplat=word(3,0x70),interior=word(6,0),farSplat=word(17,0x90);
    for(uint cascade:{0u,1u}){
        reset(0,0,cascade);
        tap(cascade,interior,empty);
        require(query(cascade)==1,"interior direct write blocks the tilted receiver");
        tap(cascade,nearSplat,empty);
        require(query(cascade)==0,"slab minimum is a coplanar splat");
        tap(cascade,farSplat,empty);
        require(query(cascade)==0,"slab maximum is a coplanar splat");
        secondDepthLayer=true;
        tap(cascade,nearSplat,farSplat);
        require(query(cascade)==0,"both slab extrema together miss the interior blocker");
        tap(cascade,nearSplat,interior);
        require(query(cascade)==1,"retaining the interior word restores the shadow");
        secondDepthLayer=false;
    }
    const unsigned long long slabs=1024/64,words=slabs*2;
    const unsigned long long bytes=2ull*1024*1024*words*4;
    require(bytes==268435456ull,"sixteen min/max slabs over the encoded range cost 256 MiB");
    std::cout<<"slab extrema counterexample executed; "<<bytes<<" bytes for "<<slabs
             <<" slabs; "<<(2ull*1024*1024*8*4)<<" bytes for a 256-unit frame range\n";
}
"""


SHIMS = {
    "glsl": r"""
uint atomicMin(uint& x,uint v){uint o=x;x=std::min(o,v);return o;}
uint atomicMax(uint& x,uint v){uint o=x;x=std::max(o,v);return o;}
uint atomicOr(uint& x,uint v){uint o=x;x=o|v;return o;}
uint atomicCompSwap(uint& x,uint c,uint v){uint o=x;if(o==c)x=v;return o;}
""",
    "metal": r"""
enum memory_order_t {memory_order_relaxed};
uint atomic_load_explicit(uint* x,memory_order_t){return *x;}
void atomic_store_explicit(uint* x,uint v,memory_order_t){*x=v;}
uint atomic_fetch_min_explicit(uint* x,uint v,memory_order_t){uint o=*x;*x=std::min(o,v);return o;}
uint atomic_fetch_max_explicit(uint* x,uint v,memory_order_t){uint o=*x;*x=std::max(o,v);return o;}
uint atomic_fetch_or_explicit(uint* x,uint v,memory_order_t){uint o=*x;*x=o|v;return o;}
bool atomic_compare_exchange_weak_explicit(uint* x,uint* expected,uint v,
                                           memory_order_t,memory_order_t){
    if(*x==*expected){*x=v;return true;}*expected=*x;return false;}
""",
}

CASES = overflow.CASES[:overflow.CASES.index(MAIN)] + r"""
const uint EMPTY=0xffffffffu;
int texelOf(uint cascade,int x,int y){return int(cascade*kCascadeTexelCount+y*kSunShadowMapDim+x);}
uint word(float z,uint marker){return (uint((z+512.0f)*1024.0f)<<8)|marker;}
float depthOf(uint w){return float(w>>8)/1024.0f-512.0f;}
void clearEverything(){
    clearDepthSlabPlanes();
    for(int y=0;y<2*kSunShadowMapDim;++y)for(int x=0;x<kSunShadowMapDim;++x)clearTexel(x,y);
}
// The production clear is whole-map; one texel's words are reset here so the
// sweep can reuse a texel without re-filling 48 MiB per write set.
void clearOne(uint cascade,int x,int y){
    const int texel=texelOf(cascade,x,y);
    clearTexel(x,y+int(cascade)*kSunShadowMapDim);
    for(uint slot=0;slot<kSunDepthSlabSlots;++slot){
        sunDepthBuf.words[sunDepthSlabMinIndex(texel,slot)]=0xffffffffu;
        sunDepthBuf.words[sunDepthSlabMaxIndex(texel,slot)]=0u;
    }
}
// The production predicate, restated: throw window, then the direct / surface
// near rejection or the coverage-splat same-plane test against the receiver plane.
bool oracleWord(uint w,float z,float bias,bool surface,ivec2 px,vec2 gradUV,vec2 uv){
    if(w==EMPTY)return false;
    const uint marker=w&0xffu;
    const bool isSurface=(marker&0xf0u)==0x80u||(marker&0x0fu)==0x08u;
    if(surface&&isSurface)return false;
    const float q=depthOf(w),diff=z-q;
    if(diff-bias>=64.0f)return false;
    if(marker==0u||isSurface)return diff>bias;
    int dx=int((w>>4)&0xfu),dy=int(w&0xfu);if(dx>=8)dx-=16;if(dy>=8)dy-=16;
    const vec2 originUV{float(px.x-dx)+.5f,float(px.y-dy)+.5f};
    const float expectedZ=z+dot(gradUV,originUV-uv);
    return expectedZ-q>bias;
}
struct Receiver {float z;bool surface;vec3 normal;float bias;vec2 gradUV;};
Receiver receiver(float z,bool surface,vec3 normal){
    const float slope=std::max(0.05f,normal.z);
    return {z,surface,normal,2.0f/slope+4.0f/1024.0f,{normal.x/slope,normal.y/slope}};
}
std::vector<uint> slabWords(uint cascade,int x,int y){
    std::vector<uint> out;
    for(uint slot=0;slot<kSunDepthSlabSlots;++slot){
        out.push_back(sunDepthBuf.words[sunDepthSlabMinIndex(texelOf(cascade,x,y),slot)]);
        out.push_back(sunDepthBuf.words[sunDepthSlabMaxIndex(texelOf(cascade,x,y),slot)]);
    }
    return out;
}
bool overflowed(uint cascade,int x,int y){
    const uint flag=sunDepthBuf.words[sunDepthSlabMaxIndex(texelOf(cascade,x,y),0u)];
    if(kSunDepthSlabDirectMapped){
        for(uint slot=0;slot<kSunDepthSlabSlots;++slot){
            uint lo=sunDepthBuf.words[sunDepthSlabMinIndex(texelOf(cascade,x,y),slot)];
            uint hi=sunDepthBuf.words[sunDepthSlabMaxIndex(texelOf(cascade,x,y),slot)];
            if(lo!=EMPTY && (lo>>8)/65536u!=(hi>>8)/65536u)return true;
        }
        return false;
    }
    return (flag&kSunDepthSlabOverflow)!=0u;
}
uint distinctSlabs(const std::vector<uint>& writes){
    std::vector<uint> slabs;for(uint w:writes)slabs.push_back((w>>8)/65536u);
    std::sort(slabs.begin(),slabs.end());slabs.erase(std::unique(slabs.begin(),slabs.end()),slabs.end());
    return uint(slabs.size());
}
uint comparisons=0,overflowCases=0,recovered=0,residual=0;
// Contract: a texel that kept every slab answers the per-write oracle exactly
// for depth-decided words; a splat whose plane test fails can still hide an
// interior write of its slab (counted as residual, never for direct words).
// An overflowed texel, or a nearest word not beyond the throw, keeps the
// nearest-only answer. The design never removes a shadow the nearest word
// gives and never adds one no write supports.
void check(uint cascade,const std::vector<uint>& writes,const Receiver& r){
    const vec2 uv{2.5f,3.5f};const ivec2 px{2,3};
    const uint nearest=*std::min_element(writes.begin(),writes.end());
    bool any=false,splatInWindow=false;
    for(uint w:writes){
        any=any||oracleWord(w,r.z,r.bias,r.surface,px,r.gradUV,uv);
        const uint marker=w&0xffu;
        const bool splat=marker!=0u&&!((marker&0xf0u)==0x80u||(marker&0x0fu)==0x08u);
        splatInWindow=splatInWindow||(splat&&r.z-depthOf(w)-r.bias<64.0f);
    }
    const bool legacy=oracleWord(nearest,r.z,r.bias,r.surface,px,r.gradUV,uv);
    const bool beyond=r.z-depthOf(nearest)-r.bias>=64.0f;
    bool collision=false;
    for(uint a:writes)for(uint b:writes){
        const uint sa=(a>>8)/65536u,sb=(b>>8)/65536u;
        if(sa!=sb&&sa%kSunDepthSlabSlots==sb%kSunDepthSlabSlots)collision=true;
    }
    const bool expectOverflow=kSunDepthSlabDirectMapped?collision:
        distinctSlabs(writes)>kSunDepthSlabSlots;
    std::vector<uint> order(writes);
    std::vector<uint> firstSlabs;
    uint permutation=0;float firstGot=0;
    do{
        clearOne(cascade,2,3);
        for(uint w:order)WRITE_PRIMARY(texelOf(cascade,2,3),w);
        require(sunDepthBuf.words[texelOf(cascade,2,3)]==nearest,
                "nearest word is the minimum write");
        require(overflowed(cascade,2,3)==expectOverflow,"overflow bit follows distinct slab count");
        const float got=sample(uv,r.z,cascade,r.surface,r.normal,64);
        auto report=[&](const char* label){
            std::cerr<<"receiver z="<<r.z<<" surface="<<r.surface
                     <<" normal.x="<<r.normal.x<<" writes:";
            for(uint w:writes)std::cerr<<' '<<depthOf(w)<<"/0x"<<std::hex<<(w&0xffu)<<std::dec;
            std::cerr<<" got "<<got<<" legacy "<<legacy<<" oracle "<<any<<"\n";
            require(false,label);
        };
        if(got!=0&&got!=1)report("slab receiver returns a fractional single-tap answer");
        if(legacy&&got!=1)report("slab receiver removed the nearest word's shadow");
        if(got==1&&!any)report("slab receiver added a shadow no write supports");
        if(!legacy&&(expectOverflow||!beyond)&&got!=0)
            report("overflow or near nearest must keep the nearest-only answer");
        if(!legacy&&!expectOverflow&&beyond&&!splatInWindow&&got!=float(any))
            report("slab receiver disagrees with the per-write oracle");
        if(permutation==0)firstGot=got;
        else if(got!=firstGot)report("slab receiver answer depends on write order");
        if(!legacy&&!expectOverflow&&beyond&&splatInWindow&&any&&got==0&&permutation==0)++residual;
        // Slot assignment follows arrival; the per-slab extrema must not.
        std::vector<uint> slabs=slabWords(cascade,2,3);
        std::vector<std::pair<uint,uint>> pairs;
        for(uint slot=0;slot<kSunDepthSlabSlots;++slot)
            pairs.push_back({slabs[slot*2],slabs[slot*2+1]&~kSunDepthSlabOverflow});
        std::sort(pairs.begin(),pairs.end());
        slabs.clear();for(auto& pr:pairs){slabs.push_back(pr.first);slabs.push_back(pr.second);}
        if(!expectOverflow){
            if(permutation==0)firstSlabs=slabs;
            else require(slabs==firstSlabs,"slab extrema depend on write order");
        }
        ++permutation;
    }while(permutation<24&&std::next_permutation(order.begin(),order.end()));
    ++comparisons;overflowCases+=expectOverflow;recovered+=(!expectOverflow&&beyond&&any&&!legacy&&firstGot==1);
}
int main(){
    static_assert(kSunDepthSlabSlots>=3u,"the fixture needs the far, lower and receiver slabs");
    sunDepthBuf.words.assign(kSourceFaceBufferWords,0xDEADBEEFu);
    sunDepthBuf.recordOffset=kSourceFaceRecordOffset;sunDepthBuf.recordEnd=kSourceFaceSpillCounterOffset;
    sunDepthBuf.spillCounterOffset=kSourceFaceSpillCounterOffset;
    // The allocator writes the capacity word before the first dispatch; the
    // production clear preserves it.
    sunDepthBuf.words[kSourceFaceCapacityOffset]=kSourceFaceCapacity;
    clearEverything();
    require(sunDepthBuf.words[kSourceFaceCapacityOffset]==kSourceFaceCapacity,
            "clear preserves the capacity word");
    require(sunDepthBuf.words[kSourceFaceHeaderOffset]==0u,"clear resets the record count");
    for(uint cascade:{0u,1u}){
        require(sunDepthBuf.words[texelOf(cascade,2,3)]==EMPTY,"clear empties the nearest word");
        const auto slabs=slabWords(cascade,2,3);
        for(uint slot=0;slot<kSunDepthSlabSlots;++slot){
            require(slabs[slot*2]==EMPTY&&slabs[slot*2+1]==0u,"clear resets poisoned slab words");
        }
        require(sample({2.5f,3.5f},10,cascade,false,{0,0,1},64)==0,"cleared texel casts nothing");
    }
    if(kSunDepthSlabDirectMapped){
        for(uint cascade:{0u,1u}){
            clearOne(cascade,2,3);
            WRITE_PRIMARY(texelOf(cascade,2,3),word(2,0));
            WRITE_PRIMARY(texelOf(cascade,2,3),word(4,0));
            require(sunDepthBuf.words[sunDepthSlabMinIndex(texelOf(cascade,2,3),2u)]==word(2,0),
                    "direct minimum write retained");
            require(sunDepthBuf.words[sunDepthSlabMaxIndex(texelOf(cascade,2,3),2u)]==word(4,0),
                    "direct maximum write retained");
            // Slot 2 answers while slot 0 is empty.
            WRITE_PRIMARY(texelOf(cascade,2,3),word(-220,0));
            require(sample({2.5f,3.5f},10,cascade,false,{0,0,1},64)==1,
                    "direct sparse slot recovery");
            // Slot 0 answers before the colliding, nonqueried slot 1 is visited.
            clearOne(cascade,2,3);
            for(uint w:{word(-250,0),word(-58,0),word(65,0)})WRITE_PRIMARY(texelOf(cascade,2,3),w);
            require(sample({2.5f,3.5f},73,cascade,false,{0,0,1},64)==0,
                    "direct collision in nonqueried slot disables whole texel");
            sunDepthBuf.words[kSourceFaceHeaderOffset]=1u;
            sunDepthBuf.words[kSourceFaceFallbackOffset+texelOf(cascade,2,3)]=word(65,0x89u);
            require(sample({2.5f,3.5f},73,cascade,false,{0,0,1},64)==1,
                    "direct collision preserves independent indexed hit");
            sunDepthBuf.words[kSourceFaceHeaderOffset]=0u;
            sunDepthBuf.words[kSourceFaceFallbackOffset+texelOf(cascade,2,3)]=EMPTY;
            for(int slab=2;slab<15;++slab){
                float z=float(slab*64-512+10);
                check(cascade,{word(z-128,0),word(z,0)},receiver(z+8,false,{0,0,1}));
                check(cascade,{word(z-192,0),word(z,0)},receiver(z+8,false,{0,0,1}));
            }
        }
    }
    const Receiver flat=receiver(10,false,{0,0,1});
    // Nearest-two loses the third distinct depth behind two distant samples.
    {
        const std::vector<uint> three{word(-120,0),word(-110,0),word(2,0)};
        bool nearestTwo=false;std::vector<uint> sorted(three);
        std::sort(sorted.begin(),sorted.end());
        for(uint i=0;i<2;++i)
            nearestTwo=nearestTwo||oracleWord(sorted[i],flat.z,flat.bias,false,{2,3},
                                              flat.gradUV,{2.5f,3.5f});
        require(!nearestTwo&&oracleWord(three[2],flat.z,flat.bias,false,{2,3},flat.gradUV,{2.5f,3.5f}),
                "three-distinct-depth counterexample for nearest-two");
        clearOne(0,2,3);for(uint w:three)WRITE_PRIMARY(texelOf(0,2,3),w);
        require(sample({2.5f,3.5f},10,0,false,{0,0,1},64)==1,"slabs retain the third depth");
        std::cout<<"nearest-two witness: writes -120,-110,2 receiver 10 -> "
                 <<"nearest-two lit, slabs blocked\n";
    }
    const float q=1.0f/1024.0f;
    for(uint cascade:{0u,1u})for(bool surface:{false,true})
    for(vec3 normal:{vec3{0,0,1},vec3{.6f,0,.8f}}){
        for(float z:{10.0f,2.00390625f,-30.0f,66.0f,200.0f,-200.0f}){
            const Receiver r=receiver(z,surface,normal);
            const float a=z-r.bias-64.0f,b=z-r.bias;
            std::vector<float> depths{a-q,a,a+q,b-q,b,b+q,z-32.0f,z-63.0f,z-65.0f,
                                      z-100.0f,z-130.0f,z-200.0f,z+5.0f,-64.0f,
                                      -64.0f+q,0.0f,-q};
            std::vector<uint> markers{0u,0x11u,0xf1u,0x77u};
            if(!surface)markers.push_back(0x85u);
            std::vector<uint> pool;
            for(float d:depths){
                if(d<-512.0f||d>=512.0f)continue;
                for(uint m:markers)pool.push_back(word(d,m));
            }
            std::sort(pool.begin(),pool.end());pool.erase(std::unique(pool.begin(),pool.end()),pool.end());
            for(uint w:pool)check(cascade,{w},r);
            for(uint i=0;i<pool.size();++i)for(uint j=i+1;j<pool.size();j+=3)
                check(cascade,{pool[i],pool[j]},r);
            for(uint i=0;i<pool.size();i+=5)for(uint j=i+1;j<pool.size();j+=7)
            for(uint k=j+1;k<pool.size();k+=11)
                check(cascade,{pool[i],pool[j],pool[k]},r);
            for(uint i=0;i<pool.size();i+=9)for(uint j=i+1;j<pool.size();j+=13)
                check(cascade,{pool[i],pool[j],pool[(i*7+j*3)%pool.size()],pool[(i*11+5)%pool.size()]},r);
            // Duplicates and the fixture stack: receiver, lower blocker, far casters.
            check(cascade,{word(z-120,0),word(z-120,0),word(z-120,0x11u),word(z-8,0)},r);
            check(cascade,{word(z-120,0),word(z-8,0),word(z+1,0)},r);
            check(cascade,{word(z-120,0),word(z-180,0),word(z-8,0),word(z+1,0)},r);
            check(cascade,{word(z-120,0),word(z-180,0),word(z-250,0),word(z-8,0),word(z+1,0)},r);
        }
    }
    require(recovered>0&&overflowCases>0,"the sweep exercises recovery and overflow");
    // Mixed layers: a slab answer and a retained indexed-layer word count once.
    for(uint cascade:{0u,1u}){
        sunDepthBuf.words[kSourceFaceHeaderOffset]=1u;
        clearOne(cascade,2,3);
        WRITE_PRIMARY(texelOf(cascade,2,3),word(-120,0));WRITE_PRIMARY(texelOf(cascade,2,3),word(2,0));
        sunDepthBuf.words[kSourceFaceFallbackOffset+texelOf(cascade,2,3)]=word(1,0x89u);
        require(sample({2.5f,3.5f},10,cascade,false,{0,0,1},64)==1,
                "primary slab and indexed layer union once");
        sunDepthBuf.words[kSourceFaceFallbackOffset+texelOf(cascade,2,3)]=EMPTY;
        sunDepthBuf.words[kSourceFaceHeaderOffset]=0u;
    }
    // Reuse: a cleared texel forgets the previous frame's slabs.
    for(uint cascade:{0u,1u}){
        clearOne(cascade,2,3);
        WRITE_PRIMARY(texelOf(cascade,2,3),word(-120,0));WRITE_PRIMARY(texelOf(cascade,2,3),word(2,0));
        require(sample({2.5f,3.5f},10,cascade,false,{0,0,1},64)==1,
                "stacked texel blocks before clear");
        clearOne(cascade,2,3);
        WRITE_PRIMARY(texelOf(cascade,2,3),word(-120,0));
        require(sample({2.5f,3.5f},10,cascade,false,{0,0,1},64)==0,
                "no stale slab survives the clear");
    }
    // A throw that differs from the slab width keeps the nearest-only answer,
    // even where a retained slab holds a blocker inside that foreign window.
    clearOne(0,2,3);WRITE_PRIMARY(texelOf(0,2,3),word(-120,0));WRITE_PRIMARY(texelOf(0,2,3),word(-50,0));
    require(sample({2.5f,3.5f},10,0,false,{0,0,1},100)==0,"foreign throw skips slabs");
    std::cout<<comparisons<<" write sets ("<<overflowCases<<" overflowed, "<<recovered
             <<" recovered behind a far nearest word, "<<residual
             <<" splat-extrema residuals), every permutation up to 24\n";
}
"""

# Lane-by-lane emulation of one bake workgroup. The kernel separates its phases
# with barriers; here every lane completes a phase before any lane starts the
# next, in ascending or descending lane order. The reference applies the
# per-pixel writer to the same write set, and the planes must agree word for word.
COOPERATIVE_CASES = r"""
const uint kLanes = uint(TILE_EDGE * TILE_EDGE);
struct Lane { bool caster; ivec2 base; float z; };
int texelIndexOf(int cascadeOffset, ivec2 px){return cascadeOffset+px.y*kSunShadowMapDim+px.x;}
bool insideMap(ivec2 px){return px.x>=0&&px.x<kSunShadowMapDim&&px.y>=0&&px.y<kSunShadowMapDim;}
void referenceTile(const std::vector<Lane>& lanes,int cascadeOffset,int radius){
    for(const Lane& lane:lanes){
        if(!lane.caster)continue;
        for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){
            const ivec2 px=lane.base+ivec2(dx,dy);
            if(!insideMap(px))continue;
            PRIMARY(texelIndexOf(cascadeOffset,px),packSunDepth(lane.z,ivec2(dx,dy)));
        }
    }
}
void cooperativeTile(const std::vector<Lane>& lanes,int cascadeOffset,int radius,bool descending){
    std::vector<uint> order;
    for(uint lane=0;lane<kLanes;++lane)order.push_back(descending?kLanes-1-lane:lane);
    for(uint lane:order)WINDOW_RESET(lane);
    for(uint lane:order)if(lanes[lane].caster)WINDOW_REDUCE(lanes[lane].base);
    const ivec2 anchor=WINDOW_ANCHOR(radius);
    const ivec2 extent=WINDOW_EXTENT(radius);
    for(uint lane:order)WINDOW_INIT(lane,kLanes,extent);
    for(uint lane:order){
        if(!lanes[lane].caster)continue;
        for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){
            WINDOW_WRITE(cascadeOffset,lanes[lane].base+ivec2(dx,dy),anchor,
                         packSunDepth(lanes[lane].z,ivec2(dx,dy)));
        }
    }
    for(uint lane:order)WINDOW_FLUSH(lane,kLanes,cascadeOffset,anchor,extent);
}
const char* planeOf(uint index){
    if(index<kSunDepthSlabMinPlanesOffset)
        return index<kSourceFaceFallbackOffset?"nearest":"fallback";
    if(index<kSunDepthSlabMaxPlanesOffset)return "slab min";
    if(index<kSourceFaceHeaderOffset)return "slab max";
    return "tail";
}
uint cooperativeSets=0,spilledWrites=0,overflowTexels=0,droppedWrites=0;
uint rng=12345u;
uint next(uint bound){rng=rng*1664525u+1013904223u;return (rng>>8)%bound;}
// `priming` is written per pixel in both arms first, so the cooperative tile
// merges into planes other workgroups already claimed.
ivec2 extentOf(int radius){return WINDOW_EXTENT(radius);}
void compare(const std::vector<Lane>& priming,const std::vector<Lane>& lanes,int cascadeOffset,
             int radius,bool descending,const char* label){
    clearEverything();
    referenceTile(priming,cascadeOffset,radius);
    referenceTile(lanes,cascadeOffset,radius);
    std::vector<uint> expected=sunDepthBuf.words;
    clearEverything();
    referenceTile(priming,cascadeOffset,radius);
    cooperativeTile(lanes,cascadeOffset,radius,descending);
    // Nearest words agree word for word. A texel's slots are a set: slot
    // positions follow arrival order and the receiver scans every slot, so the
    // sorted (min, max) pairs are compared; an overflowed texel's slots hold
    // whichever three slabs arrived first, which the receiver never reads, so
    // only its overflow bit is compared.
    auto mismatch=[&](uint i,const char* what){
        const bool slabWord=i>=kSunDepthSlabOffset&&i<kSourceFaceHeaderOffset;
        const uint texel=(i-(slabWord?kSunDepthSlabOffset:0u))%kSourceFaceFallbackOffset;
        std::cerr<<label<<": "<<what<<" at word "<<i<<" (cascade "<<texel/kCascadeTexelCount
                 <<" texel "<<texel%kCascadeTexelCount%kSunShadowMapDim<<','
                 <<texel%kCascadeTexelCount/kSunShadowMapDim<<") expected "<<expected[i]
                 <<" got "<<sunDepthBuf.words[i]<<"\n";
        require(false,"cooperative window disagrees with the per-pixel writer");
    };
    for(uint i=0;i<kSunDepthSlabOffset;++i)
        if(expected[i]!=sunDepthBuf.words[i])mismatch(i,planeOf(i));
    for(uint i=kSourceFaceHeaderOffset;i<expected.size();++i)
        if(expected[i]!=sunDepthBuf.words[i])mismatch(i,planeOf(i));
    if(!std::equal(expected.begin()+kSunDepthSlabOffset,expected.begin()+kSourceFaceHeaderOffset,
                   sunDepthBuf.words.begin()+kSunDepthSlabOffset)){
        for(uint texel=0;texel<kSourceFaceFallbackOffset;++texel){
            const uint flag=sunDepthSlabMaxIndex(int(texel),0u);
            const uint flagE=expected[flag]&kSunDepthSlabOverflow;
            const uint flagG=sunDepthBuf.words[flag]&kSunDepthSlabOverflow;
            if(flagE!=flagG)mismatch(flag,"overflow bit");
            if(flagE!=0u)continue;
            std::vector<std::pair<uint,uint>> e,g;
            for(uint slot=0;slot<kSunDepthSlabSlots;++slot){
                e.push_back({expected[sunDepthSlabMinIndex(int(texel),slot)],
                             expected[sunDepthSlabMaxIndex(int(texel),slot)]&~kSunDepthSlabOverflow});
                g.push_back({sunDepthBuf.words[sunDepthSlabMinIndex(int(texel),slot)],
                             sunDepthBuf.words[sunDepthSlabMaxIndex(int(texel),slot)]&~kSunDepthSlabOverflow});
            }
            std::sort(e.begin(),e.end());std::sort(g.begin(),g.end());
            if(e!=g)mismatch(sunDepthSlabMinIndex(int(texel),0u),"slab set");
        }
    }
    // A tile without a caster has an empty footprint and touches no window word.
    if(std::none_of(lanes.begin(),lanes.end(),[](const Lane& l){return l.caster;})){
        require(extentOf(radius).x==0,"an empty tile has no footprint");
    }
    // The spill and overflow census of the set, from the reference planes.
    ivec2 lo{kSunShadowMapDim,kSunShadowMapDim},hi{-1,-1};
    for(const Lane& lane:lanes){
        if(!lane.caster)continue;
        lo=ivec2(std::min(lo.x,lane.base.x),std::min(lo.y,lane.base.y));
        hi=ivec2(std::max(hi.x,lane.base.x),std::max(hi.y,lane.base.y));
    }
    if(hi.x>=0){
        const ivec2 anchor=lo-radius;
        for(const Lane& lane:lanes){
            if(!lane.caster)continue;
            for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){
                const ivec2 rel=lane.base+ivec2(dx,dy)-anchor;
                if(!(uint(rel.x)<kSunDepthSlabWindowTexels&&uint(rel.y)<kSunDepthSlabWindowTexels))++spilledWrites;
                if(!insideMap(lane.base+ivec2(dx,dy)))++droppedWrites;
            }
        }
        for(int y=std::max(0,lo.y-radius);y<=std::min(kSunShadowMapDim-1,hi.y+radius);++y)
        for(int x=std::max(0,lo.x-radius);x<=std::min(kSunShadowMapDim-1,hi.x+radius);++x){
            if((sunDepthBuf.words[sunDepthSlabMaxIndex(texelIndexOf(cascadeOffset,ivec2(x,y)),0u)]
                &kSunDepthSlabOverflow)!=0u)++overflowTexels;
        }
    }
    ++cooperativeSets;
}
std::vector<Lane> emptyTile(){return std::vector<Lane>(kLanes,Lane{false,ivec2(0,0),0.0f});}
int main(){
    static_assert(kSunDepthSlabSlots>=3u,"the fixture needs the far, lower and receiver slabs");
    sunDepthBuf.words.assign(kSourceFaceBufferWords,0xDEADBEEFu);
    sunDepthBuf.recordOffset=kSourceFaceRecordOffset;sunDepthBuf.recordEnd=kSourceFaceSpillCounterOffset;
    sunDepthBuf.spillCounterOffset=kSourceFaceSpillCounterOffset;
    sunDepthBuf.words[kSourceFaceCapacityOffset]=kSourceFaceCapacity;
    const std::vector<Lane> none=emptyTile();
    const int offsets[2]={0,kCascadeTexelCount};
    // No caster at all: the footprint is empty and nothing is written.
    compare(none,none,offsets[0],7,false,"empty tile");
    // One caster: the whole box inside the window, at both radii.
    for(int radius:{7,0}){
        std::vector<Lane> lanes=emptyTile();
        lanes[27]={true,ivec2(100,100),-15.0f};
        compare(none,lanes,offsets[0],radius,false,"one caster");
    }
    // Every lane a caster on a tight cluster: no spill.
    {
        std::vector<Lane> lanes=emptyTile();
        for(uint lane=0;lane<kLanes;++lane)
            lanes[lane]={true,ivec2(200+int(lane%TILE_EDGE),300+int(lane/TILE_EDGE)),-15.0f+float(lane%3)};
        compare(none,lanes,offsets[1],7,false,"clustered tile");
        compare(none,lanes,offsets[1],7,true,"clustered tile, descending lanes");
    }
    // Bases 20 texels apart: the second box reaches window column 32 exactly.
    {
        std::vector<Lane> lanes=emptyTile();
        lanes[0]={true,ivec2(400,400),-15.0f};
        lanes[9]={true,ivec2(420,400),-15.0f};
        lanes[18]={true,ivec2(400,425),-85.0f};
        compare(none,lanes,offsets[0],7,false,"spilling boxes");
    }
    // Boxes leaving the cascade at both corners are dropped, not wrapped.
    {
        std::vector<Lane> lanes=emptyTile();
        lanes[1]={true,ivec2(2,3),-15.0f};
        lanes[2]={true,ivec2(1021,1022),-85.0f};
        lanes[3]={true,ivec2(0,0),2.0f};
        compare(none,lanes,offsets[1],7,false,"map edges");
    }
    // Four distinct slabs on one texel overflow in the window; two in the
    // window on top of two the planes already hold overflow at the merge.
    {
        std::vector<Lane> lanes=emptyTile();
        lanes[4]={true,ivec2(500,500),-200.0f};
        lanes[5]={true,ivec2(500,500),-120.0f};
        lanes[6]={true,ivec2(500,500),-40.0f};
        lanes[7]={true,ivec2(500,500),40.0f};
        compare(none,lanes,offsets[0],7,false,"window overflow");
        std::vector<Lane> priming=emptyTile();
        priming[0]={true,ivec2(503,498),-200.0f};
        priming[1]={true,ivec2(497,502),-120.0f};
        std::vector<Lane> second=emptyTile();
        second[8]={true,ivec2(500,500),-40.0f};
        second[9]={true,ivec2(500,500),40.0f};
        compare(priming,second,offsets[0],7,false,"merge overflow");
        std::vector<Lane> same=emptyTile();
        same[8]={true,ivec2(500,500),-121.0f};
        same[9]={true,ivec2(506,494),-199.0f};
        compare(priming,same,offsets[1],7,true,"merge into claimed slabs");
    }
    // Duplicates and the fixture stack through the window.
    {
        std::vector<Lane> lanes=emptyTile();
        for(uint lane=0;lane<kLanes;++lane)lanes[lane]={true,ivec2(600,600),-85.0f};
        lanes[10]={true,ivec2(600,600),-15.0f};
        lanes[11]={true,ivec2(601,600),2.0f};
        compare(none,lanes,offsets[0],7,false,"duplicate stack");
    }
    // Random tiles: casters scattered over a region wider than the window
    // minus the halo, depths from a handful of slabs, both radii, both
    // cascades, both lane orders, some over a primed neighbourhood.
    for(uint trial=0;trial<96;++trial){
        const int radius=trial%4==3?0:7;
        const ivec2 centre{int(20+next(984)),int(20+next(984))};
        const int spread=int(8+next(36));
        std::vector<Lane> lanes=emptyTile(),priming=emptyTile();
        for(uint lane=0;lane<kLanes;++lane){
            if(next(4)==0)continue;
            const float depths[6]={-200.0f,-120.0f,-85.0f,-15.0f,2.0f,40.0f};
            lanes[lane]={true,centre+ivec2(int(next(uint(spread)))-spread/2,int(next(uint(spread)))-spread/2),
                         depths[next(6)]+float(next(8))*0.25f};
        }
        if(trial%3==0)for(uint lane=0;lane<kLanes;lane+=5)
            priming[lane]={true,centre+ivec2(int(next(40))-20,int(next(40))-20),-85.0f-float(next(3))*64.0f};
        compare(priming,lanes,offsets[trial%2],radius,trial%2==1,"random tile");
    }
    require(spilledWrites>0&&overflowTexels>0&&droppedWrites>0,
            "the sweep exercises spills, overflow and dropped writes");
    std::cout<<cooperativeSets<<" cooperative write sets agree with the per-pixel writer ("
             <<spilledWrites<<" spilled writes, "<<droppedWrites<<" dropped at the cascade edge, "
             <<overflowTexels<<" overflowed texels)\n";
}
"""

# The kernel's wrapper, system constant and Metal registry entry carry the
# cooperative group size; the harness reads each so a drift fails here.
COOPERATIVE_WRAPPER = {
    "glsl": SHADERS / "c_bake_sun_shadow_map_cooperative.glsl",
    "metal": SHADERS / "metal" / "c_bake_sun_shadow_map_cooperative.metal",
}
METAL_PIPELINE = SHADERS.parent / "metal" / "metal_pipeline.cpp"
LAYOUT_TWINS = {
    "hpp": SHADERS.parents[2] / "prefabs" / "irreden" / "render" / "sun_face_query_layout.hpp",
    "glsl": SHADERS / "ir_sun_face_query_layout.glsl",
    "metal": SHADERS / "metal" / "ir_sun_face_query_layout.metal",
}


def cooperative_group_size(suffix):
    return int(re.search(r"#define IR_SUN_BAKE_GROUP_SIZE (\d+)",
                         COOPERATIVE_WRAPPER[suffix].read_text())[1])


def window_source(suffix, directory, mutation=None):
    """The cooperative window include as host code: GLSL's file-scope shared
    arrays become globals, Metal's threadgroup atomics become plain words. The
    arrays gain one spare row so an inclusive window bound (a control) aliases
    into padding the flush never visits instead of past the allocation."""
    source = (SHADERS / directory / f"ir_sun_depth_slab_window.{suffix}").read_text()
    if mutation is not None:
        old, new = mutation
        if source.count(old) != 1:
            raise ValueError(f"window mutation subject not unique: {old!r}")
        source = source.replace(old, new)
    source = "\n".join(line for line in source.splitlines()
                       if not line.startswith(("#include", "using namespace")))
    source = (source.replace("threadgroup atomic_uint *", "uint *")
              .replace("threadgroup atomic_int *", "int *")
              .replace("int2(0)", "ivec2(0)")
              .replace("device atomic_uint *sunDepthBuf", "BoundBuffer& sunDepthBuf")
              .replace("shared uint ", "uint ").replace("shared int ", "int "))
    source = source.replace("[kSunDepthSlabWindowArea];",
                            "[kSunDepthSlabWindowArea + kSunDepthSlabWindowTexels];")
    source = source.replace("[kSunDepthSlabWindowSlotWords];",
                            "[kSunDepthSlabWindowSlotWords + kSunDepthSlabWindowTexels];")
    return source + "\n"


COOPERATIVE_SHIMS = {
    "glsl": r"""
int atomicMin(int& x,int v){int o=x;x=std::min(o,v);return o;}
int atomicMax(int& x,int v){int o=x;x=std::max(o,v);return o;}
ivec2 operator-(ivec2 a,int b){return {a.x-b,a.y-b};}
ivec2 operator+(ivec2 a,int b){return {a.x+b,a.y+b};}
ivec2 min(ivec2 a,ivec2 b){return {std::min(a.x,b.x),std::min(a.y,b.y)};}
""",
    "metal": r"""
int atomic_load_explicit(int* x,memory_order_t){return *x;}
void atomic_store_explicit(int* x,int v,memory_order_t){*x=v;}
int atomic_fetch_min_explicit(int* x,int v,memory_order_t){int o=*x;*x=std::min(o,v);return o;}
int atomic_fetch_max_explicit(int* x,int v,memory_order_t){int o=*x;*x=std::max(o,v);return o;}
ivec2 operator-(ivec2 a,int b){return {a.x-b,a.y-b};}
ivec2 operator+(ivec2 a,int b){return {a.x+b,a.y+b};}
ivec2 min(ivec2 a,ivec2 b){return {std::min(a.x,b.x),std::min(a.y,b.y)};}
""",
}
COOPERATIVE_GLUE = {
    "glsl": r"""
#define PRIMARY(t,w) writeSunPrimaryDepth(t,w)
#define WINDOW_RESET(lane) sunWindowReset(lane)
#define WINDOW_REDUCE(base) sunWindowReduceBase(base)
#define WINDOW_ANCHOR(radius) sunWindowAnchor(radius)
#define WINDOW_EXTENT(radius) sunWindowExtent(radius)
#define WINDOW_INIT(lane,lanes,extent) sunWindowInit(lane,lanes,extent)
#define WINDOW_WRITE(off,px,anchor,w) writeSunTexelCooperative(off,px,anchor,w)
#define WINDOW_FLUSH(lane,lanes,off,anchor,extent) sunWindowFlush(lane,lanes,off,anchor,extent)
""",
    "metal": r"""
uint windowNearestStore[kSunDepthSlabWindowArea + kSunDepthSlabWindowTexels];
uint windowSlabMinStore[kSunDepthSlabWindowSlotWords + kSunDepthSlabWindowTexels];
uint windowSlabMaxStore[kSunDepthSlabWindowSlotWords + kSunDepthSlabWindowTexels];
int windowBaseMinStore[2],windowBaseMaxStore[2];
SunDepthSlabWindow window{windowNearestStore,windowSlabMinStore,windowSlabMaxStore,
                          windowBaseMinStore,windowBaseMaxStore};
#define PRIMARY(t,w) writeSunPrimaryDepth(sunDepthBuf,t,w)
#define WINDOW_RESET(lane) sunWindowReset(window,lane)
#define WINDOW_REDUCE(base) sunWindowReduceBase(window,base)
#define WINDOW_ANCHOR(radius) sunWindowAnchor(window,radius)
#define WINDOW_EXTENT(radius) sunWindowExtent(window,radius)
#define WINDOW_INIT(lane,lanes,extent) sunWindowInit(window,lane,lanes,extent)
#define WINDOW_WRITE(off,px,anchor,w) writeSunTexelCooperative(sunDepthBuf,window,off,px,anchor,w)
#define WINDOW_FLUSH(lane,lanes,off,anchor,extent) \
    sunWindowFlush(sunDepthBuf,window,lane,lanes,off,anchor,extent)
""",
}


def writer_source(suffix, directory):
    source = (SHADERS / directory / f"ir_sun_depth_slab_write.{suffix}").read_text()
    source = "\n".join(line for line in source.splitlines()
                       if not line.startswith(("#include", "using namespace")))
    return source.replace("device atomic_uint *sunDepthBuf", "BoundBuffer& sunDepthBuf") + "\n"


BAKE_SYSTEM = (SHADERS.parents[2] / "prefabs" / "irreden" / "render" / "systems"
               / "system_bake_sun_shadow_map.hpp")


def clear_source(suffix, directory):
    source = (SHADERS / directory / f"c_clear_sun_shadow_map.{suffix}").read_text()
    constant = re.search(r"(?:const|constant) int kTotalTexels = [^;]+;", source)[0]
    kernel = extract_block(source, "void main(" if suffix == "glsl" else "kernel void c_clear")
    body = kernel[kernel.index("{") + 1:-1]
    body = re.sub(r"ivec2 gid = ivec2\(gl_GlobalInvocationID.xy\);|int2 gid = int2\(globalId.xy\);",
                  "ivec2 gid = ivec2(gx, gy);", body)
    return (constant.replace("constant", "const")
            + "\nvoid clearTexel(int gx,int gy){" + body + "}\n" + fills_source())


def fills_source():
    """The bake system's slab-plane fills, executed against the harness buffer:
    the production member with its device call rebound to a byte fill."""
    fills = extract_block(BAKE_SYSTEM.read_text(), "void clearDepthSlabPlanes(")
    fills = re.sub(r"\s*using IRPrefab::SunShadow::\w+;", "", fills)
    fills = fills.replace("IRRender::device()->fillBuffer(\n            sunShadowDepthMap_,",
                          "fillWords(")
    fills = fills.replace("std::uint32_t", "uint")
    if fills.count("fillWords(") != 2:
        raise ValueError("expected the two slab-plane fills")
    return ("void fillWords(std::size_t offsetBytes,std::size_t sizeBytes,unsigned char value){\n"
            "    std::memset(reinterpret_cast<unsigned char*>(sunDepthBuf.words.data())"
            "+offsetBytes,value,sizeBytes);}\n" + fills + "\n")


def host_source(suffix, directory, sampler_mutation=None, writer_mutation=None,
                clear_mutation=None, cooperative=False, window_mutation=None, direct=False):
    writer = writer_source(suffix, directory)
    clear = clear_source(suffix, directory)
    if cooperative:
        writer += (COOPERATIVE_SHIMS[suffix] + window_source(suffix, directory, window_mutation)
                   + COOPERATIVE_GLUE[suffix])
    if writer_mutation is not None:
        old, new = writer_mutation
        if writer.count(old) != 1:
            raise ValueError(f"writer mutation subject not unique: {old!r}")
        writer = writer.replace(old, new)
    if clear_mutation is not None:
        old, new = clear_mutation
        if clear.count(old) != 1:
            raise ValueError(f"clear mutation subject not unique: {old!r}")
        clear = clear.replace(old, new)
    call = ("writeSunPrimaryDepth(sunDepthBuf,T,W)" if suffix == "metal"
            else "writeSunPrimaryDepth(T,W)")
    cases = re.sub(r"WRITE_PRIMARY\(([^,]+),([^)]+)\)",
                   lambda m: call.replace("T", m[1]).replace("W", m[2]), CASES)
    if cooperative:
        cases = (cases[:cases.index(MAIN)]
                 + COOPERATIVE_CASES.replace("TILE_EDGE", str(cooperative_group_size(suffix))))
    cases = "#include <algorithm>\n#include <cstring>\n" + SHIMS[suffix] + writer + clear + cases
    code = overflow.host_source(suffix, directory, False, mutation=sampler_mutation,
                                cases=cases, include_pcf=True)
    # The controls execute the layer at the smallest useful configuration
    # whatever slot count ships (zero compiles it out).
    code, replaced = re.subn(r"const uint kSunDepthSlabSlots = \d+u;",
                             "const uint kSunDepthSlabSlots = 3u;", code)
    if replaced == 0:
        raise ValueError("expected the layout's slot-count constant")
    code, replaced = re.subn(r"const bool kSunDepthSlabDirectMapped = (?:true|false);",
                             f"const bool kSunDepthSlabDirectMapped = {str(direct).lower()};", code)
    if replaced == 0:
        raise ValueError("expected the layout's direct-map selector")
    return code


@unittest.skipUnless(COMPILER, "slab proofs and controls require a C++ compiler")
class SunDepthSlabTest(unittest.TestCase):
    def test_slab_extrema_exact_when_no_wider_than_the_window(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "slab.cpp").write_text(SLAB_PROOF)
            build = subprocess.run([COMPILER, "-std=c++17", "-O2", str(path / "slab.cpp"),
                                    "-o", str(path / "slab")], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(path / "slab")], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            print(run.stdout, end="")
            mutant = SLAB_PROOF.replace("hi[first] > low", "hi[first] >= low")
            (path / "slab.cpp").write_text(mutant)
            subprocess.run([COMPILER, "-std=c++17", "-O2", str(path / "slab.cpp"),
                            "-o", str(path / "slab")], check=True, capture_output=True)
            run = subprocess.run([str(path / "slab")], capture_output=True, text=True)
            self.assertNotEqual(run.returncode, 0)
            self.assertIn("disagree", run.stderr)

    def test_extrema_lose_an_interior_blocker_under_provenance_rejection(self):
        harness = overflow.SourceFaceOverflowTest()
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            with self.subTest(backend=suffix):
                run = harness.compile_and_run(overflow.host_source(
                    suffix, directory, False, cases=PROVENANCE, include_pcf=True))
                self.assertEqual(run.returncode, 0, run.stderr)
                print(f"{suffix}: {run.stdout}", end="")
            with self.subTest(backend=suffix, mutation="skip_same_plane_test"):
                run = harness.compile_and_run(overflow.host_source(
                    suffix, directory, False, cases=PROVENANCE, include_pcf=True,
                    mutation=("if (h <= bias) return false;", "")))
                self.assertNotEqual(run.returncode, 0)
                self.assertIn("coplanar splat", run.stderr)


    def test_writer_clear_and_receiver_agree_with_oracle(self):
        harness = overflow.SourceFaceOverflowTest()
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            for direct in (False, True):
                with self.subTest(backend=suffix, direct=direct):
                    run = harness.compile_and_run(host_source(suffix, directory, direct=direct))
                    self.assertEqual(run.returncode, 0, run.stderr)
                    print(f"{suffix} direct={direct}: {run.stdout}", end="")

    def test_mutation_controls(self):
        harness = overflow.SourceFaceOverflowTest()
        oracle = "slab receiver disagrees with the per-write oracle"
        sampler = {
            "drop_slab_lookup": (
                ("!sunStoredTapBeyondThrow(stored, sunZ, bias, maxShadowThrow) ||", "true ||"),
                "slabs retain the third depth"),
            "drop_slab_max_candidate": (
                ("        if (sunStoredTapBlocks(high, sunZ, bias, maxShadowThrow, "
                 "surfaceReceiver,\n"
                 "                               px, origin, texelSz, gradUV, sunUV)) "
                 "return true;\n", ""),
                oracle),
            "ignore_overflow": (
                ("if ((sunDepthBuf[sunDepthSlabMaxIndex(texelIndex, 0u)] & "
                 "kSunDepthSlabOverflow) != 0u) return false;", ""),
                "overflow or near nearest must keep the nearest-only answer"),
            "unbounded_candidates": (
                ("if (depthDiff - bias >= maxShadowThrow) return false;", ""),
                "slab receiver added a shadow no write supports"),
            "foreign_throw_consults_slabs": (
                ("if (kSunDepthSlabSlots == 0u || "
                 "maxShadowThrow != float(kSunDepthSlabWidthUnits)) return false;", ""),
                "foreign throw skips slabs"),
        }
        writer = {
            "glsl": {
                "drop_same_slab_max": (
                    ("            if (highDepth > (sunDepthBuf[high] & "
                     "~kSunDepthSlabOverflow)) {\n"
                     "                atomicMax(sunDepthBuf[high], highDepth);\n"
                     "            }\n", ""),
                    "slab extrema depend on write order"),
                "no_overflow_flag": (
                    ("        atomicOr(sunDepthBuf[flagWord], kSunDepthSlabOverflow);\n", ""),
                    "overflow bit follows distinct slab count"),
                "stale_covering_skip": (
                    ("if (lowDepth < resident) atomicMin",
                     "if (lowDepth > resident) atomicMin"),
                    "slab extrema depend on write order"),
                "wrong_slab_match": (
                    ("if (sunDepthSlab(resident) == slab) {", "if (true) {"),
                    "slabs retain the third depth"),
            },
            "metal": {
                "drop_same_slab_max": (
                    ("            if (highDepth > (high & ~kSunDepthSlabOverflow)) {\n"
                     "                atomic_fetch_max_explicit(&sunDepthBuf[highIndex], "
                     "highDepth, memory_order_relaxed);\n"
                     "            }\n", ""),
                    "slab extrema depend on write order"),
                "no_overflow_flag": (
                    ("        atomic_fetch_or_explicit(&sunDepthBuf[flagWord], "
                     "kSunDepthSlabOverflow, memory_order_relaxed);\n", ""),
                    "overflow bit follows distinct slab count"),
                "stale_covering_skip": (
                    ("if (lowDepth < resident) {", "if (lowDepth > resident) {"),
                    "slab extrema depend on write order"),
                "wrong_slab_match": (
                    ("if (sunDepthSlab(resident) == slab) {", "if (true) {"),
                    "slabs retain the third depth"),
            },
        }
        # Dropping the max-plane fill leaves poisoned max words (and their
        # overflow bits) in place; the same subject in both dialects, since the
        # fills belong to the bake system.
        max_fill = re.search(
            r"\n\s*fillWords\(\n\s*std::size_t\(kSunDepthSlabMaxPlanesOffset\)[^;]*;",
            fills_source())[0]
        clear = {"glsl": (max_fill, ""), "metal": (max_fill, "")}
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            for name, (mutation, failure) in sampler.items():
                with self.subTest(backend=suffix, mutation=name):
                    run = harness.compile_and_run(
                        host_source(suffix, directory, sampler_mutation=mutation))
                    self.assertNotEqual(run.returncode, 0)
                    self.assertIn(failure, run.stderr)
            for name, (mutation, failure) in writer[suffix].items():
                with self.subTest(backend=suffix, mutation=name):
                    run = harness.compile_and_run(
                        host_source(suffix, directory, writer_mutation=mutation))
                    self.assertNotEqual(run.returncode, 0)
                    self.assertIn(failure, run.stderr)
            with self.subTest(backend=suffix, mutation="stale_slab_clear"):
                run = harness.compile_and_run(
                    host_source(suffix, directory, clear_mutation=clear[suffix]))
                self.assertNotEqual(run.returncode, 0)
                self.assertIn("clear resets poisoned slab words", run.stderr)


    def test_zero_slots_write_only_the_nearest_word(self):
        harness = overflow.SourceFaceOverflowTest()
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            code = host_source(suffix, directory, direct=True)
            code = code[:code.index(MAIN)] + """
int main(){
sunDepthBuf.words.assign(kSourceFaceBufferWords,0xDEADBEEFu);
sunDepthBuf.words[kSourceFaceCapacityOffset]=kSourceFaceCapacity;
clearEverything();
auto previous=sunDepthBuf.words;
WRITE
require(sunDepthBuf.words[texelOf(0,2,3)]==word(-120,0),"zero slots retain nearest");
sunDepthBuf.words[texelOf(0,2,3)]=previous[texelOf(0,2,3)];
require(sunDepthBuf.words==previous,"zero slots never touch slab storage");
return 0;}
""".replace("WRITE", ("writeSunPrimaryDepth(sunDepthBuf,texelOf(0,2,3),word(-120,0));"
                       if suffix == "metal" else
                       "writeSunPrimaryDepth(texelOf(0,2,3),word(-120,0));"))
            code = code.replace("const uint kSunDepthSlabSlots = 3u;",
                                "const uint kSunDepthSlabSlots = 0u;")
            run = harness.compile_and_run(code)
            self.assertEqual(run.returncode, 0, run.stderr)

    def test_direct_mutation_controls(self):
        harness = overflow.SourceFaceOverflowTest()
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            stores = (
                ("atomicMin(sunDepthBuf[lowIndex], lowDepth);",
                 "atomicMax(sunDepthBuf[highIndex], highDepth);") if suffix == "glsl" else
                ("atomic_fetch_min_explicit(&sunDepthBuf[lowIndex], lowDepth, "
                 "memory_order_relaxed);",
                 "atomic_fetch_max_explicit(&sunDepthBuf[highIndex], highDepth, "
                 "memory_order_relaxed);\n        return;"))
            for kind, store in zip(("minimum", "maximum"), stores):
                with self.subTest(backend=suffix, mutation=f"direct_drop_{kind}"):
                    run = harness.compile_and_run(host_source(
                        suffix, directory, direct=True, writer_mutation=(store, "")))
                    self.assertNotEqual(run.returncode, 0)
                    self.assertIn(f"direct {kind} write retained", run.stderr)
                    print(f"{suffix}: direct_drop_{kind} failed by name")
            controls = {
                "bypass_collision": (
                    ("if (sunDepthSlab(low) != sunDepthSlab(high)) return false;", ""),
                    "direct collision in nonqueried slot disables whole texel"),
                "accept_before_collision_scan": (
                    ("if (sunDepthSlab(low) != sunDepthSlab(high)) return false;",
                     "if (sunStoredTapBlocks(low, sunZ, bias, maxShadowThrow, surfaceReceiver, "
                     "px, origin, texelSz, gradUV, sunUV)) return true;\n"
                     "            if (sunDepthSlab(low) != sunDepthSlab(high)) return false;"),
                    "direct collision in nonqueried slot disables whole texel"),
                "stop_at_empty_slot": (
                    ("if (low == 0xFFFFFFFFu) continue;", "if (low == 0xFFFFFFFFu) break;"),
                    "direct sparse slot recovery"),
            }
            for name, (mutation, failure) in controls.items():
                with self.subTest(backend=suffix, mutation=name):
                    run = harness.compile_and_run(host_source(
                        suffix, directory, direct=True, sampler_mutation=mutation))
                    self.assertNotEqual(run.returncode, 0)
                    self.assertIn(failure, run.stderr)
                    print(f"{suffix}: {name} failed by name")
            max_fill = re.search(
                r"\n\s*fillWords\(\n\s*std::size_t\(kSunDepthSlabMaxPlanesOffset\)[^;]*;",
                fills_source())[0]
            run = harness.compile_and_run(host_source(
                suffix, directory, direct=True, clear_mutation=(max_fill, "")))
            self.assertNotEqual(run.returncode, 0)
            self.assertIn("clear resets poisoned slab words", run.stderr)
            print(f"{suffix}: direct_stale_clear failed by name")

    def test_direct_dispatch_and_selector_twins(self):
        selectors = {name: re.search(r"kSunDepthSlabDirectMapped = (true|false);",
                                     path.read_text())[1]
                     for name, path in LAYOUT_TWINS.items()}
        self.assertEqual(len(set(selectors.values())), 1, selectors)
        source = BAKE_SYSTEM.read_text()
        dispatch = extract_block(source, "RasterBakeDispatch rasterBakeDispatch(")
        self.assertIn("!IRPrefab::SunShadow::kSunDepthSlabDirectMapped", dispatch)
        self.assertRegex(source, r"if constexpr \(\s*"
                         r"IRPrefab::SunShadow::kSunDepthSlabSlots > 0u &&"
                         r"\s*!IRPrefab::SunShadow::kSunDepthSlabDirectMapped\s*\)")
        # Execute the production dispatch against fake program handles in both modes.
        for direct in (False, True):
            code = """
#include <cstdlib>
struct ShaderProgram {};
namespace IRPrefab::SunShadow {constexpr bool kSunDepthSlabDirectMapped = DIRECT;}
constexpr int kBakeSunShadowCooperativeGroupSize=8,kBakeSunShadowGroupSize=16;
struct System {
ShaderProgram a,b,c;
ShaderProgram *bakeCooperativeProgram_=&a,*bakeSecondDepthProgram_=&b,*bakeProgram_=&c;
bool retainSecondPrimaryDepth_=false;
struct RasterBakeDispatch {ShaderProgram *program_; int groupSize_;};
DISPATCH
};
int main(){System s;
for(bool second:{false,true})for(bool splat:{false,true}){
s.retainSecondPrimaryDepth_=second;
auto got=s.rasterBakeDispatch(splat);
auto expected=second?s.bakeSecondDepthProgram_:
 (!DIRECT&&splat?s.bakeCooperativeProgram_:s.bakeProgram_);
if(got.program_!=expected)return 1;
}return 0;}
""".replace("DISPATCH", dispatch).replace("DIRECT", str(direct).lower())
            code = "#include <initializer_list>\n" + code
            harness = overflow.SourceFaceOverflowTest()
            run = harness.compile_and_run(code)
            self.assertEqual(run.returncode, 0, run.stderr)
            mutant = code.replace("!IRPrefab::SunShadow::kSunDepthSlabDirectMapped &&", "")
            if direct:
                self.assertNotEqual(harness.compile_and_run(mutant).returncode, 0)

    def test_cooperative_window_agrees_with_per_pixel_writer(self):
        harness = overflow.SourceFaceOverflowTest()
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            with self.subTest(backend=suffix):
                run = harness.compile_and_run(host_source(suffix, directory, cooperative=True))
                self.assertEqual(run.returncode, 0, run.stderr)
                self.assertIn("agree with the per-pixel writer", run.stdout)
                print(f"{suffix}: {run.stdout}", end="")

    def test_cooperative_mutation_controls(self):
        harness = overflow.SourceFaceOverflowTest()
        disagree = "cooperative window disagrees with the per-pixel writer"
        controls = {
            "glsl": {
                "dropped_flush": (
                    ("        atomicMin(sunDepthBuf[index], nearest);\n", ""), disagree),
                "dropped_spill": (
                    ("    writeSunPrimaryDepth(cascadeOffset + px.y * kSunShadowMapDim + px.x, "
                     "packedDepth);\n", ""), disagree),
                "wrong_window_origin": (
                    ("        const ivec2 px = anchor + cell;",
                     "        const ivec2 px = anchor + cell + ivec2(1, 0);"), disagree),
                "inclusive_window_bound": (
                    ("if (uint(rel.x) < kSunDepthSlabWindowTexels && uint(rel.y) < "
                     "kSunDepthSlabWindowTexels) {",
                     "if (uint(rel.x) <= kSunDepthSlabWindowTexels && uint(rel.y) < "
                     "kSunDepthSlabWindowTexels) {"), disagree),
            },
            "metal": {
                "dropped_flush": (
                    ("        atomic_fetch_min_explicit(&sunDepthBuf[index], nearest, "
                     "memory_order_relaxed);\n", ""), disagree),
                "dropped_spill": (
                    ("    writeSunPrimaryDepth(sunDepthBuf, cascadeOffset + px.y * "
                     "kSunShadowMapDim + px.x, packedDepth);\n", ""), disagree),
                "wrong_window_origin": (
                    ("        const int2 px = anchor + cell;",
                     "        const int2 px = anchor + cell + int2(1, 0);"), disagree),
                "inclusive_window_bound": (
                    ("if (uint(rel.x) < kSunDepthSlabWindowTexels && uint(rel.y) < "
                     "kSunDepthSlabWindowTexels) {",
                     "if (uint(rel.x) <= kSunDepthSlabWindowTexels && uint(rel.y) < "
                     "kSunDepthSlabWindowTexels) {"), disagree),
            },
        }
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            for name, (mutation, failure) in controls[suffix].items():
                with self.subTest(backend=suffix, mutation=name):
                    run = harness.compile_and_run(
                        host_source(suffix, directory, cooperative=True, window_mutation=mutation))
                    self.assertNotEqual(run.returncode, 0)
                    self.assertIn(failure, run.stderr)

    def test_cooperative_layout_twins_and_group_size_agree(self):
        windows = {name: int(re.search(r"kSunDepthSlabWindowTexels = (\d+)u", path.read_text())[1])
                   for name, path in LAYOUT_TWINS.items()}
        slots = {name: int(re.search(r"kSunDepthSlabSlots = (\d+)u", path.read_text())[1])
                 for name, path in LAYOUT_TWINS.items()}
        self.assertEqual(len(set(windows.values())), 1, windows)
        self.assertEqual(len(set(slots.values())), 1, slots)
        window = windows["hpp"]
        self.assertLessEqual(window * window * (1 + 2 * slots["hpp"]) * 4, 32768)
        groups = {suffix: cooperative_group_size(suffix) for suffix in COOPERATIVE_WRAPPER}
        groups["system"] = int(re.search(r"kBakeSunShadowCooperativeGroupSize = (\d+);",
                                         BAKE_SYSTEM.read_text())[1])
        registry = re.search(
            r'functionName == "c_bake_sun_shadow_map_cooperative"\) \{\s*'
            r"return MTL::Size\((\d+), (\d+), 1\);", METAL_PIPELINE.read_text())
        self.assertIsNotNone(registry, "cooperative kernel missing from the threadgroup registry")
        groups["registry"] = int(registry[1])
        self.assertEqual(registry[1], registry[2])
        self.assertEqual(len(set(groups.values())), 1, groups)
        # The flush hands every lane an equal share of the window.
        self.assertEqual((window * window) % (groups["system"] ** 2), 0)


if __name__ == "__main__":
    unittest.main()

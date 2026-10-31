"""Execute the primary-layer depth-slab writer, clear and receiver in both dialects.

An independent oracle applies the production per-word predicate to every
write a texel received. Where the texel kept every slab and the nearest word
lies beyond the throw, the slab-backed sampler must agree with that oracle for
depth-decided words, never remove the nearest word's shadow, never add a
shadow no write supports, and answer independently of write order. An
overflowed texel keeps the nearest-only answer. Mutations of the writer, the
clear and the receiver each fail a named control.
"""

import re
import unittest

import test_render_source_face_overflow as overflow
from shader_contract_helpers import extract_block

SHADERS = overflow.SHADERS
MAIN = "int main(){"

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
bool atomic_compare_exchange_weak_explicit(uint* x,uint* expected,uint v,memory_order_t,memory_order_t){
    if(*x==*expected){*x=v;return true;}*expected=*x;return false;}
""",
}

CASES = overflow.CASES[:overflow.CASES.index(MAIN)] + r"""
const uint EMPTY=0xffffffffu;
int texelOf(uint cascade,int x,int y){return int(cascade*kCascadeTexelCount+y*kSunShadowMapDim+x);}
uint word(float z,uint marker){return (uint((z+512.0f)*1024.0f)<<8)|marker;}
float depthOf(uint w){return float(w>>8)/1024.0f-512.0f;}
void clearEverything(){
    for(int y=0;y<2*kSunShadowMapDim;++y)for(int x=0;x<kSunShadowMapDim;++x)clearTexel(x,y);
}
void clearOne(uint cascade,int x,int y){clearTexel(x,y+int(cascade)*kSunShadowMapDim);}
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
    return (sunDepthBuf.words[sunDepthSlabMaxIndex(texelOf(cascade,x,y),0u)]&kSunDepthSlabOverflow)!=0u;
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
    const bool expectOverflow=distinctSlabs(writes)>kSunDepthSlabSlots;
    std::vector<uint> order(writes);
    std::vector<uint> firstSlabs;
    uint permutation=0;float firstGot=0;
    do{
        clearOne(cascade,2,3);
        for(uint w:order)WRITE_PRIMARY(texelOf(cascade,2,3),w);
        require(sunDepthBuf.words[texelOf(cascade,2,3)]==nearest,"nearest word is the minimum write");
        require(overflowed(cascade,2,3)==expectOverflow,"overflow bit follows distinct slab count");
        const float got=sample(uv,r.z,cascade,r.surface,r.normal,64);
        auto report=[&](const char* label){
            std::cerr<<"receiver z="<<r.z<<" surface="<<r.surface<<" normal.x="<<r.normal.x<<" writes:";
            for(uint w:writes)std::cerr<<' '<<depthOf(w)<<"/0x"<<std::hex<<(w&0xffu)<<std::dec;
            std::cerr<<" got "<<got<<" legacy "<<legacy<<" oracle "<<any<<"\n";
            require(false,label);
        };
        if(got!=0&&got!=1)report("slab receiver returns a fractional single-tap answer");
        if(legacy&&got!=1)report("slab receiver removed the nearest word's shadow");
        if(got==1&&!any)report("slab receiver added a shadow no write supports");
        if(!legacy&&(expectOverflow||!beyond)&&got!=0)report("overflow or near nearest must keep the nearest-only answer");
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
    clearEverything();
    require(sunDepthBuf.words[kSourceFaceHeaderOffset]==0u,"clear resets the record count");
    for(uint cascade:{0u,1u}){
        require(sunDepthBuf.words[texelOf(cascade,2,3)]==EMPTY,"clear empties the nearest word");
        const auto slabs=slabWords(cascade,2,3);
        for(uint slot=0;slot<kSunDepthSlabSlots;++slot){
            require(slabs[slot*2]==EMPTY&&slabs[slot*2+1]==0u,"clear resets poisoned slab words");
        }
        require(sample({2.5f,3.5f},10,cascade,false,{0,0,1},64)==0,"cleared texel casts nothing");
    }
    const Receiver flat=receiver(10,false,{0,0,1});
    // Nearest-two loses the third distinct depth behind two distant samples.
    {
        const std::vector<uint> three{word(-120,0),word(-110,0),word(2,0)};
        bool nearestTwo=false;std::vector<uint> sorted(three);std::sort(sorted.begin(),sorted.end());
        for(uint i=0;i<2;++i)nearestTwo=nearestTwo||oracleWord(sorted[i],flat.z,flat.bias,false,{2,3},flat.gradUV,{2.5f,3.5f});
        require(!nearestTwo&&oracleWord(three[2],flat.z,flat.bias,false,{2,3},flat.gradUV,{2.5f,3.5f}),
                "three-distinct-depth counterexample for nearest-two");
        clearOne(0,2,3);for(uint w:three)WRITE_PRIMARY(texelOf(0,2,3),w);
        require(sample({2.5f,3.5f},10,0,false,{0,0,1},64)==1,"slabs retain the third depth");
        std::cout<<"nearest-two witness: writes -120,-110,2 receiver 10 -> nearest-two lit, slabs blocked\n";
    }
    const float q=1.0f/1024.0f;
    for(uint cascade:{0u,1u})for(bool surface:{false,true})for(vec3 normal:{vec3{0,0,1},vec3{.6f,0,.8f}}){
        for(float z:{10.0f,2.00390625f,-30.0f,66.0f,200.0f,-200.0f}){
            const Receiver r=receiver(z,surface,normal);
            const float a=z-r.bias-64.0f,b=z-r.bias;
            std::vector<float> depths{a-q,a,a+q,b-q,b,b+q,z-32.0f,z-63.0f,z-65.0f,z-100.0f,z-130.0f,z-200.0f,
                                      z+5.0f,-64.0f,-64.0f+q,0.0f,-q};
            std::vector<uint> markers{0u,0x11u,0xf1u,0x77u};
            if(!surface)markers.push_back(0x85u);
            std::vector<uint> pool;
            for(float d:depths){
                if(d<-512.0f||d>=512.0f)continue;
                for(uint m:markers)pool.push_back(word(d,m));
            }
            std::sort(pool.begin(),pool.end());pool.erase(std::unique(pool.begin(),pool.end()),pool.end());
            for(uint w:pool)check(cascade,{w},r);
            for(uint i=0;i<pool.size();++i)for(uint j=i+1;j<pool.size();j+=3)check(cascade,{pool[i],pool[j]},r);
            for(uint i=0;i<pool.size();i+=5)for(uint j=i+1;j<pool.size();j+=7)for(uint k=j+1;k<pool.size();k+=11)
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
        require(sample({2.5f,3.5f},10,cascade,false,{0,0,1},64)==1,"primary slab and indexed layer union once");
        sunDepthBuf.words[kSourceFaceFallbackOffset+texelOf(cascade,2,3)]=EMPTY;
        sunDepthBuf.words[kSourceFaceHeaderOffset]=0u;
    }
    // Reuse: a cleared texel forgets the previous frame's slabs.
    for(uint cascade:{0u,1u}){
        clearOne(cascade,2,3);
        WRITE_PRIMARY(texelOf(cascade,2,3),word(-120,0));WRITE_PRIMARY(texelOf(cascade,2,3),word(2,0));
        require(sample({2.5f,3.5f},10,cascade,false,{0,0,1},64)==1,"stacked texel blocks before clear");
        clearOne(cascade,2,3);
        WRITE_PRIMARY(texelOf(cascade,2,3),word(-120,0));
        require(sample({2.5f,3.5f},10,cascade,false,{0,0,1},64)==0,"no stale slab survives the clear");
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


def writer_source(suffix, directory):
    source = (SHADERS / directory / f"ir_sun_depth_slab_write.{suffix}").read_text()
    source = "\n".join(line for line in source.splitlines()
                       if not line.startswith("#include") and not line.startswith("using namespace"))
    return source.replace("device atomic_uint *sunDepthBuf", "BoundBuffer& sunDepthBuf") + "\n"


def clear_source(suffix, directory):
    source = (SHADERS / directory / f"c_clear_sun_shadow_map.{suffix}").read_text()
    constant = re.search(r"(?:const|constant) int kTotalTexels = [^;]+;", source)[0]
    kernel = extract_block(source, "void main(" if suffix == "glsl" else "kernel void c_clear")
    body = kernel[kernel.index("{") + 1:-1]
    body = re.sub(r"ivec2 gid = ivec2\(gl_GlobalInvocationID.xy\);|int2 gid = int2\(globalId.xy\);",
                  "ivec2 gid = ivec2(gx, gy);", body)
    return constant.replace("constant", "const") + "\nvoid clearTexel(int gx,int gy){" + body + "}\n"


def host_source(suffix, directory, sampler_mutation=None, writer_mutation=None,
                clear_mutation=None):
    writer = writer_source(suffix, directory)
    clear = clear_source(suffix, directory)
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
    cases = "#include <algorithm>\n" + SHIMS[suffix] + writer + clear + cases
    code = overflow.host_source(suffix, directory, False, mutation=sampler_mutation,
                                cases=cases, include_pcf=True)
    # The shipped layout compiles the layer out; the controls execute it at
    # the smallest useful configuration.
    shipped = "const uint kSunDepthSlabSlots = 0u;"
    if shipped not in code:
        raise ValueError("expected the shipped zero-slot layout")
    return code.replace(shipped, "const uint kSunDepthSlabSlots = 3u;")


@unittest.skipUnless(overflow.COMPILER, "depth-slab controls require a C++ compiler")
class SunDepthSlabTest(unittest.TestCase):
    def test_writer_clear_and_receiver_agree_with_oracle(self):
        harness = overflow.SourceFaceOverflowTest()
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            with self.subTest(backend=suffix):
                run = harness.compile_and_run(host_source(suffix, directory))
                self.assertEqual(run.returncode, 0, run.stderr)
                print(f"{suffix}: {run.stdout}", end="")

    def test_mutation_controls(self):
        harness = overflow.SourceFaceOverflowTest()
        oracle = "slab receiver disagrees with the per-write oracle"
        sampler = {
            "drop_slab_lookup": (
                ("!sunStoredTapBeyondThrow(stored, sunZ, bias, maxShadowThrow) ||", "true ||"),
                "slabs retain the third depth"),
            "drop_slab_max_candidate": (
                ("        if (sunStoredTapBlocks(high, sunZ, bias, maxShadowThrow, surfaceReceiver,\n"
                 "                               px, origin, texelSz, gradUV, sunUV)) return true;\n", ""),
                oracle),
            "ignore_overflow": (
                ("if ((sunDepthBuf[sunDepthSlabMaxIndex(texelIndex, 0u)] & kSunDepthSlabOverflow) != 0u) return false;", ""),
                "overflow or near nearest must keep the nearest-only answer"),
            "unbounded_candidates": (
                ("if (depthDiff - bias >= maxShadowThrow) return false;", ""),
                "slab receiver added a shadow no write supports"),
            "foreign_throw_consults_slabs": (
                ("if (kSunDepthSlabSlots == 0u || maxShadowThrow != float(kSunDepthSlabWidthUnits)) return false;", ""),
                "foreign throw skips slabs"),
        }
        writer = {
            "glsl": {
                "drop_same_slab_max": (
                    ("            if (packedDepth > (sunDepthBuf[high] & ~kSunDepthSlabOverflow)) {\n"
                     "                atomicMax(sunDepthBuf[high], packedDepth);\n"
                     "            }\n", ""),
                    "slab extrema depend on write order"),
                "no_overflow_flag": (
                    ("        atomicOr(sunDepthBuf[flagWord], kSunDepthSlabOverflow);\n", ""),
                    "overflow bit follows distinct slab count"),
                "stale_covering_skip": (
                    ("if (packedDepth < resident) atomicMin", "if (packedDepth > resident) atomicMin"),
                    "slab extrema depend on write order"),
                "wrong_slab_match": (
                    ("if (sunDepthSlab(resident) == slab) {", "if (true) {"),
                    "slabs retain the third depth"),
            },
            "metal": {
                "drop_same_slab_max": (
                    ("            if (packedDepth > (high & ~kSunDepthSlabOverflow)) {\n"
                     "                atomic_fetch_max_explicit(&sunDepthBuf[highIndex], packedDepth, memory_order_relaxed);\n"
                     "            }\n", ""),
                    "slab extrema depend on write order"),
                "no_overflow_flag": (
                    ("        atomic_fetch_or_explicit(&sunDepthBuf[flagWord], kSunDepthSlabOverflow, memory_order_relaxed);\n", ""),
                    "overflow bit follows distinct slab count"),
                "stale_covering_skip": (
                    ("if (packedDepth < resident) {", "if (packedDepth > resident) {"),
                    "slab extrema depend on write order"),
                "wrong_slab_match": (
                    ("if (sunDepthSlab(resident) == slab) {", "if (true) {"),
                    "slabs retain the third depth"),
            },
        }
        clear = {
            "glsl": ("    for (uint slot = 0u; slot < kSunDepthSlabSlots; ++slot) {\n"
                     "        sunDepthBuf[sunDepthSlabMinIndex(linearIdx, slot)] = 0xFFFFFFFFu;\n"
                     "        sunDepthBuf[sunDepthSlabMaxIndex(linearIdx, slot)] = 0u;\n"
                     "    }", ""),
            "metal": ("    for (uint slot = 0u; slot < kSunDepthSlabSlots; ++slot) {\n"
                      "        atomic_store_explicit(&sunDepthBuf[sunDepthSlabMinIndex(linearIdx, slot)], 0xFFFFFFFFu, memory_order_relaxed);\n"
                      "        atomic_store_explicit(&sunDepthBuf[sunDepthSlabMaxIndex(linearIdx, slot)], 0u, memory_order_relaxed);\n"
                      "    }", ""),
        }
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


if __name__ == "__main__":
    unittest.main()

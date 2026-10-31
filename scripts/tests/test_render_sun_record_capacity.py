"""Execute the sun face producers, clear, hierarchy build and receiver at grown
record capacities through the serial CPU adapters.

The record pool is sized by a capacity the allocator writes beside the header
(Metal) or that the bound length implies (GLSL). These controls allocate the
emulated buffer at a small pool, at one grown step above the default and at a
pool whose hierarchy needs three cooperative passes, then exhaust each by one
record and check the retained prefix, the lost-record bit, the hierarchy over
the retained records and the receiver's answers. They prove the shader
arithmetic for the layout; GPU resize lifetime and readback completion need
native controls.
"""

import unittest

import test_render_source_face_overflow as receiver
import test_render_sun_hierarchy as hierarchy

CAPACITIES = (1024, 131072, 262144)

PRODUCER_STUBS = r"""
vec2 clamp(vec2 a,vec2 lo,vec2 hi){
    return {std::min(std::max(a.x,lo.x),hi.x),std::min(std::max(a.y,lo.y),hi.y)};
}
uint atomicAdd(uint& p,uint n){uint o=p;p+=n;return o;}
uint atomicMax(uint& p,uint n){uint o=p;p=std::max(p,n);return o;}
uint atomicExchange(uint& p,uint n){uint o=p;p=n;return o;}
uint atomicOr(uint& p,uint n){uint o=p;p|=n;return o;}
uint atomic_fetch_add_explicit(uint* p,uint n,int){return atomicAdd(*p,n);}
uint atomic_fetch_max_explicit(uint* p,uint n,int){return atomicMax(*p,n);}
uint atomic_exchange_explicit(uint* p,uint n,int){return atomicExchange(*p,n);}
uint atomic_fetch_or_explicit(uint* p,uint n,int){return atomicOr(*p,n);}
struct FrameDataSun {
    vec2 cascadeOriginUV_0{0,0},cascadeTexelSize_0{1,1};
    vec2 cascadeOriginUV_1{-8,-8},cascadeTexelSize_1{2,3};
};
FrameDataSun sunFrame;
const vec2 cascadeOriginUV_0{0,0},cascadeTexelSize_0{1,1};
const vec2 cascadeOriginUV_1{-8,-8},cascadeTexelSize_1{2,3};
"""

CASES = r"""
uint liveCapacity(){return READ_CAPACITY;}
uint bufferWordsFor(uint capacity){
    return kSourceFaceRecordOffset+kSourceFaceTailFixedWords+kSourceFaceWordsPerRecord*capacity;
}
// Undefined storage after a resize: every word the shaders read must come from the clear.
void allocate(uint capacity){
    sunDepthBuf.words.assign(bufferWordsFor(capacity),0xdeadbeefu);
    sunDepthBuf.words[kSourceFaceCapacityOffset]=capacity;
    sunDepthBuf.recordOffset=kSourceFaceRecordOffset;
    sunDepthBuf.recordEnd=sourceFaceSpillCounterIndex(capacity);
    sunDepthBuf.spillCounterOffset=sourceFaceSpillCounterIndex(capacity);
    sunDepthBuf.readCount=0;
    faces.clear();
}
void clearAll(uint capacity){
    for(uint id=0;id<kSourceFaceFallbackOffset;++id)clearInvocation(id);
    require(sunDepthBuf.words[kSourceFaceCapacityOffset]==capacity,
            "clear preserves the capacity word");
    require(liveCapacity()==capacity,"shaders read the allocated capacity");
    require(sunDepthBuf.words[kSourceFaceHeaderOffset]==0,"clear resets the header");
    require(sunDepthBuf.words[sourceFaceSpillCounterIndex(capacity)]==0,
            "clear resets the spill counter");
    require(sunDepthBuf.words[sourceFaceHierarchyNeededIndex(capacity)]==0,
            "clear resets hierarchy request");
    require(sunDepthBuf.words[sourceFaceHierarchyReadyIndex(capacity)]==0,
            "clear resets hierarchy readiness");
    for(uint tile=0;tile<kSourceFaceTileCount;tile+=97){
        require(sunDepthBuf.words[sourceFaceTileBase(tile)]==0,"clear resets tile counts");
        require(sunDepthBuf.words[sourceFaceSpillHeadIndex(capacity,tile)]==0xffffffffu,
                "clear resets spill heads");
    }
    sunDepthBuf.readCount=0;
}
void indexFace(Face f){INDEX_CALL faces.push_back(f);}
// Small faces spread across both cascades, clear of the receiver's tiles, so no
// tile overflows its inline list.
Face spread(uint k){
    const float x=19.5f+8.0f*float(k%120u), y=19.5f+8.0f*float((k/120u)%120u);
    return Face{{x,y,2},{1,0,0},{0,1,0}};
}
uint expectedSpillNodes(){
    uint nodes=0;
    for(uint tile=0;tile<kSourceFaceTileCount;++tile){
        const uint count=sunDepthBuf.words[sourceFaceTileBase(tile)]&~kSourceFaceTileRecordLost;
        nodes+=std::min(count,kSourceFaceTileQueryCapacity)>kSourceFaceTileCapacity
            ? std::min(count,kSourceFaceTileQueryCapacity)-kSourceFaceTileCapacity : 0u;
    }
    return nodes;
}
uint lostTiles(){
    uint lost=0;
    for(uint tile=0;tile<kSourceFaceTileCount;++tile)
        lost+=!sourceFaceTileRetainsRecords(sunDepthBuf.words[sourceFaceTileBase(tile)]);
    return lost;
}
uint leafCountFor(uint records){uint leaves=1;while(leaves*8<records)leaves*=2;return leaves;}
void buildAll(uint capacity){
    uint level=capacity/kSourceFaceHierarchyLeafSize;
    int passes=0;
    for(uint covered=kSourceFaceHierarchyWorkgroupSize;;covered*=kSourceFaceHierarchyWorkgroupSize){
        ++passes;if(covered>=level)break;
    }
    if(passes<2)passes=2;
    passes+=PASS_DELTA;
    for(int pass=0;pass<passes;++pass){
        level=(level+kSourceFaceHierarchyWorkgroupSize-1)/kSourceFaceHierarchyWorkgroupSize;
        const uint groups=level<1?1:level;
        for(uint group=0;group<groups;++group)buildInvocation(group,uint(pass));
        if(pass==0)require(sunDepthBuf.words[sourceFaceHierarchyReadyIndex(capacity)]==0,
                           "subtree dispatch cannot publish cross-workgroup readiness");
    }
    sunDepthBuf.readCount=0;
}
int main(){
    const Face hit{{2.8f,3,2},{.2f,0,0},{0,1,0}};
    const Face miss{{2,3,2},{.25f,0,0},{0,1,0}};
    // Interior of cascade-0 tile 516 and cascade-1 tile 16514, clear of tile edges.
    const Face lostFace{{35.5f,35.5f,2},{1,0,0},{0,1,0}};
    uint checks=0;
    for(uint capacity:{CAPACITY_LIST}){
        for(uint receiverCase=0;receiverCase<2;++receiverCase){
            allocate(capacity);
            clearAll(capacity);
            // 257 coincident faces make tile 0 incomplete without losing records.
            for(uint i=0;i<257;++i)indexFace(receiverCase==0?hit:miss);
            require(!sourceFaceQueryComplete(sunDepthBuf.words[sourceFaceTileBase(0)]),
                    "tile 0 overflows its query capacity");
            require(sunDepthBuf.words[sourceFaceHierarchyNeededIndex(capacity)]==1,
                    "tile overflow requests the hierarchy");
            for(uint k=257;k+1<capacity;++k)indexFace(spread(k));
            require(sunDepthBuf.words[kSourceFaceHeaderOffset]==capacity-1,
                    "capacity-minus-one requests");
            require(lostTiles()==0,"no record lost below capacity");
            indexFace(spread(capacity-1));
            require(sunDepthBuf.words[kSourceFaceHeaderOffset]==capacity,"exact capacity requests");
            require(lostTiles()==0,"the last record fits the pool");
            const uint spillNodes=sunDepthBuf.words[sourceFaceSpillCounterIndex(capacity)];
            require(spillNodes==expectedSpillNodes() && spillNodes>=2u*(256u-64u),
                    "spill nodes match the overflowed tile lists");
            const uint stale=sunDepthBuf.words[sourceFaceSpillHeadIndex(capacity,7777)];
            require(stale==0xffffffffu,"untouched spill head keeps its cleared value");
            buildAll(capacity);
            require(sunDepthBuf.words[sourceFaceHierarchyReadyIndex(capacity)]==leafCountFor(capacity),
                    "hierarchy publishes the retained leaf count");
            require(sample({2.9f,3.5f},10,0,true,{0,0,1},64)==(receiverCase==0?1.0f:0.0f),
                    "retained incomplete tile queries the hierarchy exactly");
            if(receiverCase==1){
                fallback(0,0xffffffffu,depth(2));
                require(sample({2.9f,3.5f},10,0,true,{0,0,1},64)==0,
                        "exact hierarchy miss suppresses the indexed sampled layer");
            }
            // One reservation past the pool: nothing is written past the records,
            // the overlapped tiles carry the lost bit and answer from the sampled layers.
            indexFace(lostFace);
            require(sunDepthBuf.words[kSourceFaceHeaderOffset]==capacity+1,
                    "capacity-plus-one requests");
            require(sunDepthBuf.words[sourceFaceSpillCounterIndex(capacity)]==spillNodes,
                    "a lost reservation writes no record over the tail");
            require(lostTiles()==2 &&
                    !sourceFaceTileRetainsRecords(sunDepthBuf.words[sourceFaceTileBase(516)]) &&
                    !sourceFaceTileRetainsRecords(sunDepthBuf.words[sourceFaceTileBase(16514)]) &&
                    sourceFaceTileRetainsRecords(sunDepthBuf.words[sourceFaceTileBase(0)]),
                    "the lost record marks its tile in both cascades");
            require(sample({2.9f,3.5f},10,0,true,{0,0,1},64)==(receiverCase==0?1.0f:0.0f),
                    "tiles that kept their records still query the hierarchy");
            indexFace(hit);
            require(lostTiles()==4,"a lost record over tile 0 marks it");
            // The retained hierarchy no longer answers tile 0; only the sampled
            // layers do, and the indexed layer blocks when the miss case seeded it.
            require(sample({2.9f,3.5f},10,0,true,{0,0,1},64)==(receiverCase==1?1.0f:0.0f),
                    "a tile that lost records answers from the sampled layers");
            // Clear and reuse at the same capacity: stale records are unreachable.
            clearAll(capacity);
            faces.clear();
            require(sample({2.9f,3.5f},10,0,true,{0,0,1},64)==0,"cleared pool casts nothing");
            ++checks;
        }
    }
    std::cout<<checks<<" grown-capacity producer/clear/build/receiver controls passed\n";
}
"""


def source(suffix, directory, mutation=None, pass_delta=0):
    shaders = receiver.SHADERS / directory
    producer = receiver.load_shader(shaders, "ir_sun_face_index", suffix)
    producer = (producer.replace("device atomic_uint* sunDepthBuf", "BoundBuffer& sunDepthBuf")
                .replace("constant FrameDataSun& sunFrame", "const FrameDataSun& sunFrame"))
    # Double literals would select the host's generic vec2 constructor.
    for literal in ("0.0", "-1.0", "-3.4028234663852886e38", "3.4028234663852886e38"):
        producer = producer.replace(f"vec2({literal})", f"vec2({literal}f)")
        producer = producer.replace(f"float2({literal})", f"float2({literal}f)")
    read_capacity = ("sourceFaceCapacity(uint(sunDepthBuf.length()))" if suffix == "glsl"
                     else "sunDepthBuf.words[kSourceFaceCapacityOffset]")
    index_call = ("indexSourceSunFace(f.corner,f.u,f.v);" if suffix == "glsl"
                  else "indexSourceSunFace(sunDepthBuf,f.corner,f.u,f.v,sunFrame);")
    prefix = receiver.CASES[:receiver.CASES.index("int main(){")]
    cases = (prefix + PRODUCER_STUBS + producer + CASES
             .replace("READ_CAPACITY", read_capacity)
             .replace("INDEX_CALL", index_call)
             .replace("PASS_DELTA", str(pass_delta))
             .replace("CAPACITY_LIST", ",".join(f"{c}u" for c in CAPACITIES)))
    saved = hierarchy.CASES
    hierarchy.CASES = cases
    try:
        code = hierarchy.source(suffix, directory)
    finally:
        hierarchy.CASES = saved
    # The cooperative phase guard reads readiness at the live capacity's offset.
    guard = "sunDepthBuf.words[kSourceFaceHierarchyReadyOffset]!=0"
    if guard not in code:
        raise ValueError("missing hierarchy phase guard")
    code = code.replace(
        guard, f"sunDepthBuf.words[sourceFaceHierarchyReadyIndex({read_capacity})]!=0")
    if mutation:
        before, after = mutation
        if before not in code:
            raise ValueError(f"missing capacity mutation {before!r}")
        code = code.replace(before, after)
    return code


@unittest.skipUnless(receiver.COMPILER, "record capacity controls require a C++ compiler")
class SunRecordCapacityTest(unittest.TestCase):
    def test_grown_pools_index_build_and_sample(self):
        harness = receiver.SourceFaceOverflowTest()
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            with self.subTest(backend=suffix):
                run = harness.compile_and_run(source(suffix, directory))
                self.assertEqual(run.returncode, 0, run.stderr)
                print(f"{suffix}: {run.stdout}", end="")

    def test_mutations(self):
        harness = receiver.SourceFaceOverflowTest()
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            mutations = {
                "fixed_capacity_producer": (
                    ("const uint capacity = sourceFaceCapacity(boundWords);",
                     "const uint capacity = kSourceFaceCapacity;") if suffix == "glsl" else
                    ("const uint capacity = atomic_load_explicit("
                     "&sunDepthBuf[kSourceFaceCapacityOffset], memory_order_relaxed);",
                     "const uint capacity = kSourceFaceCapacity;")),
                "fixed_capacity_receiver": (
                    ("    const uint capacity = sourceFaceCapacity(boundWords);\n"
                     "    const uint sourceFaceCount",
                     "    const uint capacity = kSourceFaceCapacity;\n"
                     "    const uint sourceFaceCount") if suffix == "glsl" else
                    ("    const uint capacity = sunDepthBuf[kSourceFaceCapacityOffset];\n"
                     "    const uint sourceFaceCount",
                     "    const uint capacity = kSourceFaceCapacity;\n"
                     "    const uint sourceFaceCount")),
                "publish_before_root": ("pass != 0u && subtrees == 1u && lane == 0u",
                                        "pass != 0u && lane == 0u"),
                "count_unpublished_records": ("return requested < capacity ? requested : capacity;",
                                              "return requested;"),
            }
            for name, mutation in mutations.items():
                with self.subTest(backend=suffix, mutation=name):
                    run = harness.compile_and_run(source(suffix, directory, mutation))
                    self.assertNotEqual(run.returncode, 0, "mutation escaped capacity controls")
            with self.subTest(backend=suffix, mutation="drop_last_pass"):
                run = harness.compile_and_run(source(suffix, directory, pass_delta=-1))
                self.assertNotEqual(run.returncode, 0, "a missing pass escaped capacity controls")


if __name__ == "__main__":
    unittest.main()

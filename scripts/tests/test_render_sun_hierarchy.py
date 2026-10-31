"""Execute production hierarchy phases and receivers through serial CPU adapters.

The independent triangle oracle covers finite geometry. Analytic records use a
sentinel predicate to isolate conservative node admission and type dispatch;
Each workgroup phase runs all lanes before the next phase. This checks the
algorithm and publication schedule, not GPU barrier visibility or concurrency;
GPU synchronization, upload bindings and analytic intersections need native controls.
"""

import re
import unittest

import test_render_source_face_overflow as receiver
from shader_contract_helpers import compact, extract_block, extract_function

CASES = receiver.CASES[:receiver.CASES.index("int main(){")] + r"""
bool reverseGroups=false;
void buildPass(uint pass){
    const uint groups=pass==0?64:1;
    for(uint i=0;i<groups;++i)buildInvocation(reverseGroups?groups-1-i:i,pass);
}
void build(){
    sunDepthBuf.words[kSourceFaceHierarchyNeededOffset]=1;
    buildPass(0);
    require(sunDepthBuf.words[kSourceFaceHierarchyReadyOffset]==0,
            "subtree dispatch cannot publish cross-workgroup readiness");
    buildPass(1);
    sunDepthBuf.readCount=0;
}
void clear(){
    for(uint id=0;id<kSourceFaceFallbackOffset;++id)clearInvocation(id);
    sunDepthBuf.readCount=0;
}
uint expectedLeaves(uint count){uint leaves=1;while(leaves*8<count)leaves*=2;return leaves;}
struct ReferenceBounds {bool occupied=false;D3 low{},high{};};
void checkAllNodes(){
    const uint leaves=expectedLeaves(uint(faces.size()));
    std::vector<ReferenceBounds> expected(2*leaves);
    for(uint i=0;i<faces.size();++i){
        auto& bounds=expected[leaves+i/8];
        const auto& face=faces[i];
        for(uint u=0;u<2;++u)for(uint v=0;v<2;++v){
            const D3 point{double(face.corner.x)+u*double(face.u.x)+v*double(face.v.x),
                           double(face.corner.y)+u*double(face.u.y)+v*double(face.v.y),
                           double(face.corner.z)+u*double(face.u.z)+v*double(face.v.z)};
            if(!bounds.occupied){bounds.occupied=true;bounds.low=bounds.high=point;}
            else{
                bounds.low={std::min(bounds.low.x,point.x),std::min(bounds.low.y,point.y),
                            std::min(bounds.low.z,point.z)};
                bounds.high={std::max(bounds.high.x,point.x),std::max(bounds.high.y,point.y),
                             std::max(bounds.high.z,point.z)};
            }
        }
    }
    for(uint node=leaves-1;node>0;--node){
        const auto& left=expected[2*node];const auto& right=expected[2*node+1];
        auto& bounds=expected[node];
        if(!left.occupied)bounds=right;
        else if(!right.occupied)bounds=left;
        else{
            bounds.occupied=true;
            bounds.low={std::min(left.low.x,right.low.x),std::min(left.low.y,right.low.y),
                        std::min(left.low.z,right.low.z)};
            bounds.high={std::max(left.high.x,right.high.x),std::max(left.high.y,right.high.y),
                         std::max(left.high.z,right.high.z)};
        }
    }
    for(uint node=1;node<2*leaves;++node){
        const uint offset=kSourceFaceHierarchyOffset+node*kSourceFaceHierarchyNodeWords;
        const auto& want=expected[node];
        const float kind=uintBitsToFloat(sunDepthBuf.words[offset+3]);
        require(kind==(want.occupied?0.0f:-1.0f),"every node replaces poisoned or stale contents");
        if(!want.occupied)continue;
        const double low[]={want.low.x,want.low.y,want.low.z};
        const double high[]={want.high.x,want.high.y,want.high.z};
        for(uint axis=0;axis<3;++axis){
            const double actualLow=uintBitsToFloat(sunDepthBuf.words[offset+axis]);
            const double actualHigh=uintBitsToFloat(sunDepthBuf.words[offset+4+axis]);
            require(actualLow<=low[axis] && actualHigh>=high[axis],
                    "every published node encloses its independent descendant corners");
            require(low[axis]-actualLow<.125 && actualHigh-high[axis]<.125,
                    "node bounds exclude unrelated subtree or previous-frame geometry");
        }
    }
}
int main(){
    const uint empty=0xffffffffu;
    const Face distant{{2,3,-120},{1,0,0},{0,1,0}};
    const Face miss{{2,3,2},{.25f,0,0},{0,1,0}};
    const Face hit{{2.8f,3,2},{.2f,0,0},{0,1,0}};
    uint comparisons=0;
    for(uint cascade:{0u,1u})for(uint count:{257u,513u,1023u,1024u,1025u,65536u}){
        for(uint position:{0u,count/2,count-1}){
            reset(count,count,cascade);
            for(uint i=0;i<count;++i)writeFace(i==position?hit:distant);
            fallback(cascade,empty,depth(-120));
            reverseGroups=position!=0;
            build();
            require(oracle({2.9f,3.5f},10,64),"independent lower blocker oracle");
            require(sample({2.9f,3.5f},10,cascade,true,{0,0,1},64)==1,
                    "hierarchy preserves lower blocker behind distant indexed depth");
            require(sunDepthBuf.readCount<=count*kSourceFaceRecordWords,
                    "hierarchy visits each record at most once");
            sunDepthBuf.words[tileBase(cascade)]=65;
            sunDepthBuf.words[kSourceFaceSpillCounterOffset]=kSourceFaceSpillCapacity+1;
            for(uint i=0;i<64;++i)sunDepthBuf.words[tileBase(cascade)+i+1]=empty;
            require(sample({2.9f,3.5f},10,cascade,true,{0,0,1},64)==1,
                    "spill allocation winners and unpublished losers use global geometry");
            ++comparisons;
        }
        reset(count,count,cascade);fill(count,distant);build();
        require(sample({2.9f,3.5f},10,cascade,true,{0,0,1},64)==0,
                "distant-only hierarchy respects finite throw");
        require(sunDepthBuf.readCount==0,"depth bounds prune distant-only root");
        reset(count,count,cascade);fill(count,miss);build();
        fallback(cascade,empty,depth(1));
        require(sample({2.9f,3.5f},10,cascade)==0,
                "complete hierarchy miss suppresses indexed raster approximation");
        require(sunDepthBuf.readCount==0,"footprint bounds prune disjoint root");
        fallback(cascade,depth(2),depth(1));
        require(sample({2.9f,3.5f},10,cascade)==1,
                "hierarchy miss preserves independent raster layer");
    }
    for(uint count:{1u,8u,9u,16u,17u,256u,257u,1016u,1017u,1023u,1024u,1025u,
                    2047u,2048u,2049u,65535u,65536u}){
        reset(count,257);fill(count,hit);
        sunDepthBuf.words[kSourceFaceHierarchyNeededOffset]=1;
        buildPass(0);
        const uint expected=expectedLeaves(count);
        require(sunDepthBuf.words[kSourceFaceHierarchyReadyOffset]==0,
                "subtree dispatch cannot publish cross-workgroup readiness");
        buildPass(1);
        require(sunDepthBuf.words[kSourceFaceHierarchyReadyOffset]==expected,
                "root publishes dynamic power-of-two leaf count");
        sunDepthBuf.readCount=0;
        require(sample()==1,"small and padded trees retain exact geometry");
    }
    for(uint count:{1023u,1024u,1025u,2049u,65536u}){
        reset(count,257);
        for(uint i=0;i<count;++i)
            writeFace({{2+float(i)/4,3,2},{.125f,0,0},{0,.125f,0}});
        build();checkAllNodes();
    }
    reset(65536,257);fill(65536,hit);build();
    for(uint count:{65535u,8193u,1025u,1024u,1023u,513u,257u,17u,9u,8u,1u}){
        clear();
        sunDepthBuf.words[kSourceFaceHeaderOffset]=count;
        sunDepthBuf.words[tileBase(0)]=257;
        faces.clear();fill(count,miss);
        build();checkAllNodes();
        require(sample()==0,"shrinking frames cannot traverse stale records or padded leaves");
    }
    reset(257,257);fill(257,hit);build();
    for(uint ready:{0u,3u,32u,128u,8193u,0xffffffffu}){
        sunDepthBuf.words[kSourceFaceHierarchyReadyOffset]=ready;
        sunDepthBuf.readCount=0;
        require(sample()==0 && sunDepthBuf.readCount==0,
                "invalid or incomplete readiness cannot expose tree records");
    }
    reset(257,257);fill(257,hit);build();
    sunDepthBuf.words[kSourceFaceHeaderOffset]=65537;
    sunDepthBuf.words[kSourceFaceHierarchyReadyOffset]=16384;
    sunDepthBuf.readCount=0;
    fallback(0,empty,depth(-120));
    require(sample()==0 && sunDepthBuf.readCount==0,
            "readiness for unpublished records cannot expose the tree");
    fallback(0,empty,depth(2));
    require(sample()==1,"unpublished hierarchy preserves sampled fallback");
    reset(257,257);fill(257,hit);build();clear();
    require(sunDepthBuf.words[kSourceFaceHierarchyReadyOffset]==0 &&
            sunDepthBuf.words[kSourceFaceHierarchyNeededOffset]==0,
            "clear retires stale hierarchy metadata");
    sunDepthBuf.words[kSourceFaceHeaderOffset]=257;
    sunDepthBuf.words[tileBase(0)]=257;
    require(sample()==0 && sunDepthBuf.readCount==0,"stale tree cannot shadow next frame");
    buildPass(0);buildPass(1);
    require(sunDepthBuf.words[kSourceFaceHierarchyReadyOffset]==0,
            "unneeded build cannot publish stale tree");
    reset(0,257);sunDepthBuf.words[kSourceFaceHierarchyNeededOffset]=1;
    buildPass(0);buildPass(1);
    require(sunDepthBuf.words[kSourceFaceHierarchyReadyOffset]==0 &&
            sunDepthBuf.readCount==0,"empty pool skips build");
    // Global record exhaustion: the retained prefix is complete for tiles that kept every record.
    // A tile that lost records never traverses it and answers from the sampled layers.
    uint exhaustedComparisons=0;
    for(uint cascade:{0u,1u})for(uint requested:{65535u,65536u,65537u,70000u}){
        const uint retained=std::min(requested,65536u);
        for(uint position:{0u,retained/2,retained-1})for(uint lost:{0u,1u}){
            reset(requested,257,cascade);
            for(uint i=0;i<retained;++i)writeFace(i==position?hit:distant);
            if(lost)sunDepthBuf.words[tileBase(cascade)]|=kSourceFaceTileRecordLost;
            fallback(cascade,empty,depth(-120));
            reverseGroups=position!=0;
            build();
            require(sunDepthBuf.words[kSourceFaceHierarchyReadyOffset]==8192,
                    "exhausted pool publishes its retained prefix");
            require(sample({2.9f,3.5f},10,cascade)==float(!lost),lost
                    ?"lost tile answers from the sampled layers, not the retained pool"
                    :"retained blocker survives global record exhaustion");
            require(lost?sunDepthBuf.readCount==0
                        :sunDepthBuf.readCount<=retained*kSourceFaceRecordWords,lost
                    ?"lost tile never traverses the retained pool"
                    :"retained-pool traversal visits each record at most once");
            ++exhaustedComparisons;
        }
        if(requested<=65536)continue;
        for(uint lost:{0u,1u}){
            reset(requested,257|(lost?kSourceFaceTileRecordLost:0u),cascade);
            fill(65536,distant);build();
            fallback(cascade,empty,depth(2));
            require(sample({2.9f,3.5f},10,cascade)==float(lost),lost
                    ?"lost tile keeps the sampled indexed layer after a retained-pool miss"
                    :"retained tile certifies a complete retained-pool miss");
        }
        // Documented limit: a lost lower record hidden behind a distant sampled depth.
        reset(requested,kSourceFaceTileRecordLost,cascade);fill(65536,distant);build();
        fallback(cascade,empty,depth(-120));
        require(sample({2.9f,3.5f},10,cascade)==0,"lost record under distant depth stays a limit");
        fallback(cascade,depth(2),depth(-120));
        require(sample({2.9f,3.5f},10,cascade)==1,"lost tile preserves independent raster layer");
        clear();
        require(sunDepthBuf.words[tileBase(cascade)]==0 &&
                sunDepthBuf.words[kSourceFaceHierarchyReadyOffset]==0,
                "clear retires lost-record tiles and exhausted readiness");
    }
    reverseGroups=false;
    reset(257,257);fill(256,miss);writeFace({{100,100,2},{0,0,1},{1,2,0}});
    sunDepthBuf.words[kSourceFaceRecordOffset+256*kSourceFaceRecordWords+9]=7;
    build();
    require(sample()==1 && analyticCalls==1,
            "analytic records remain unbounded and reach exact type predicate");
    reset(257,257);fill(257,hit);build();
    sunDepthBuf.words[tileBase(0)]=1;sunDepthBuf.words[tileBase(0)+1]=0;
    require(sample()==1 && sunDepthBuf.readCount==kSourceFaceRecordWords,
            "complete inline tile retains ordinary query cost");
    reset(257,257);fill(255,distant);
    writeFace({{2,3,20},{1,0,0},{0,1,0}});writeFace(distant);build();
    require(sample()==0,"overlapping node bounds do not relax per-record throw");
    reset(257,257);fill(257,hit);build();
    for(float z:{2.0f,2.00390625f,2.004f,102.0f,102.01f})
        require(sample({2.9f,3.5f},z)==oracle({2.9f,3.5f},z,100),
                "hierarchy retains strict bias and throw boundaries");
    reset(513,513);
    for(uint i=0;i<513;++i){
        const float angle=.25f+.006f*i,c=std::cos(angle),s=std::sin(angle);
        vec3 u{2.5f*c,2.5f*s,.1f},v{-1.4f*s,1.4f*c,-.2f};
        vec3 corner{3.9f-(u.x+v.x)*.5f+.001f*i,3.7f-(u.y+v.y)*.5f,2+.003f*i};
        if(i%3==0){corner={corner.x+u.x,corner.y+u.y,corner.z+u.z};
            u={-u.x,-u.y,-u.z};}
        if(i%5==0)std::swap(u,v);
        writeFace({corner,u,v});
    }
    build();
    uint hits=0,misses=0;
    for(uint y=0;y<37;++y)for(uint x=0;x<31;++x){
        const vec2 uv{.031f+7.92f*x/30,.017f+7.95f*y/36};
        const bool expected=oracle(uv,10,100);
        sunDepthBuf.readCount=0;
        require(sample(uv)==expected,"hierarchy agrees with independent triangle rays");
        require(sunDepthBuf.readCount<=513*kSourceFaceRecordWords,
                "stackless traversal never repeats geometry");
        hits+=expected;misses+=!expected;
    }
    require(hits>100 && misses>100,"hierarchy oracle fixture contains hits and misses");
    std::cout<<comparisons<<" hierarchy overlap/order controls; "<<exhaustedComparisons
             <<" exhausted-pool retained/lost controls; readiness, clear, pool, "
               "analytic admission, mixed-layer controls and "<<hits+misses
             <<" independent triangle rays passed\n";
}
"""


def source(suffix, directory, mutation=None):
    code = receiver.host_source(suffix, directory, False, cases=CASES)
    shader = (receiver.SHADERS / directory / f"c_build_sun_hierarchy.{suffix}").read_text()
    helpers = extract_function(shader, "writeSourceSunBounds")
    helpers += extract_block(shader, "SourceSunBounds sourceSunLeafBounds(")
    helpers += "\n".join(extract_function(shader, name) for name in (
        "initializeSourceSunHierarchy", "reduceSourceSunHierarchy"))
    marker = "void main()" if suffix == "glsl" else "kernel void c_build_sun_hierarchy("
    kernel = extract_block(shader, marker)
    kernel = kernel[kernel.index("{") + 1:-1]
    kernel = re.sub(r"    const uint (?:lane|group|pass) = [^;]+;\n", "", kernel)
    kernel = re.sub(r"    threadgroup SourceSunBounds hierarchyBounds\[[^;]+;\n", "", kernel)
    for helper in ("initializeSourceSunHierarchy", "reduceSourceSunHierarchy"):
        kernel, replacements = re.subn(
            rf"({helper}\([^;]+\);)", r"for(uint lane=0;lane<128;++lane)\1", kernel)
        if replacements != 1:
            raise ValueError(f"expected one production phase call for {helper}")
    barrier = ("barrier();" if suffix == "glsl" else
               "threadgroup_barrier(mem_flags::mem_threadgroup);")
    if kernel.count(barrier) != 2:
        raise ValueError("expected initialization and reduction phase barriers")
    kernel = kernel.replace(barrier, "hierarchyPhaseBoundary();")
    clear = (receiver.SHADERS / directory / f"c_clear_sun_shadow_map.{suffix}").read_text()
    clear = clear[clear.index("    if (linearIdx >= kTotalTexels)"):]
    builder = ("constexpr int memory_order_relaxed=0;\n"
               "void atomic_store_explicit(uint* p,uint value,int){*p=value;}\n"
               "uint atomic_load_explicit(uint* p,int){return *p;}\n")
    builder += r"""
SourceSunBounds hierarchyBounds[2u*kSourceFaceHierarchyWorkgroupSize];
void hierarchyPhaseBoundary(){
    if(sunDepthBuf.words[kSourceFaceHierarchyReadyOffset]!=0){
        std::cerr<<"readiness published before cooperative phases complete\n";std::exit(1);
    }
}
"""
    builder += helpers + "\nvoid buildInvocation(uint group,uint pass){\nconst uint lane=0;\n"
    builder += ("for(auto& bounds:hierarchyBounds)"
                "bounds=SourceSunBounds(vec4(12345.0f),vec4(-12345.0f));\n")
    builder += kernel + "}\n"
    builder += ("void clearInvocation(uint linearIdx){\n"
                "const uint kTotalTexels=kSourceFaceFallbackOffset;\n" + clear)
    builder = (builder.replace("device uint* sunDepthBuf", "BoundBuffer& sunDepthBuf")
               .replace("device const uint* sunDepthBuf", "BoundBuffer& sunDepthBuf")
               .replace("threadgroup ", "")
               .replace("thread ", "").replace("float4", "vec4"))
    code = code.replace("struct Face {", builder + "\nstruct Face {")
    stub = extract_function(code, "sourceShapeRaySeparation")
    code = code.replace(stub, "uint analyticCalls=0;\nfloat sourceShapeRaySeparation("
                        "vec2,float,uint type,vec3,vec3,vec3){"
                        "if(type!=7)std::abort();++analyticCalls;return 8.0f;}")
    if mutation:
        before, after = mutation
        if before not in code:
            raise ValueError(f"missing hierarchy mutation {before!r}")
        code = code.replace(before, after)
    return code


def dispatch_source(mutation=None):
    root = receiver.SHADERS.parents[3]
    layout = (root / "engine/prefabs/irreden/render/sun_face_query_layout.hpp").read_text()
    # The whole layout header, with its cascade constants inlined: the dispatch
    # derives pass and group counts from SourceFaceLayout.
    constants = layout.replace(
        "#include <irreden/render/sun_shadow_cascade.hpp>",
        "namespace IRPrefab::SunShadow { constexpr std::uint32_t kSunShadowMapDim = 1024u;"
        " constexpr std::uint32_t kSunShadowCascadeCount = 2u; }")
    system_path = root / "engine/prefabs/irreden/render/systems/system_bake_sun_shadow_map.hpp"
    system = system_path.read_text()
    dispatch = extract_function(system, "buildOverflowHierarchy")
    pipeline = (root / "engine/render/src/metal/metal_pipeline.cpp").read_text()
    mapping = extract_block(pipeline, "MTL::Size threadgroupSizeForFunctionName(")
    code = r"""
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
using uint=std::uint32_t;
void require(bool ok,const char* message){if(!ok){std::cerr<<message<<"\n";std::exit(1);}}
CONSTANTS
namespace IRMath {uint divCeil(uint n,uint d){return (n+d-1)/d;}}
enum class BufferTarget {SHADER_STORAGE,UNIFORM};
enum class BarrierType {SHADER_STORAGE};
constexpr int kBufferIndex_SunShadowDepthMap=28,kBufferIndex_RevoxelizeDetachedParams=16;
std::vector<int> events;std::vector<uint> groups;
struct Buffer{
    int id;
    void bindBase(BufferTarget target,int slot){
        require(slot==(id==-2?28:16),"dispatch binds the expected buffer slot");
        require(target==(id==-2?BufferTarget::SHADER_STORAGE:BufferTarget::UNIFORM),
                "dispatch uses the expected buffer target");
        events.push_back(10+id);
    }
};
struct Program{void use(){events.push_back(1);}};
struct Device{
    void dispatchCompute(uint x,uint y,uint z){
        require(y==1 && z==1,"hierarchy dispatch remains one-dimensional");
        groups.push_back(x);events.push_back(20);
    }
    void memoryBarrier(BarrierType){events.push_back(30);}
};
namespace IRRender {Device* device(){static Device value;return &value;}}
struct Fixture{
    Program program;Buffer depth{-2},restore{-1},pass0{0},pass1{1},pass2{2};
    Program* hierarchyProgram_=&program;Buffer* sunShadowDepthMap_=&depth;
    Buffer* hierarchyFrames_[3]={&pass0,&pass1,&pass2};Buffer* revoxelizeParamsBuf_=&restore;
    IRPrefab::SunShadow::SourceFaceLayout recordLayout_{};
    DISPATCH
};
namespace MTL {struct Size{
    uint width,height,depth;Size(uint x,uint y,uint z):width(x),height(y),depth(z){}
};}
MAPPING
int main(){
    using namespace IRPrefab::SunShadow;
    static_assert(kSourceFaceHierarchyBuildPasses==2 && kSourceFaceHierarchyWorkgroupSize==128);
    Fixture fixture;fixture.buildOverflowHierarchy();
    require(groups==std::vector<uint>({64,1}),"CPU dispatch emits exactly two bounded passes");
    require(events==std::vector<int>({1,8,10,20,30,11,20,30,9}),
            "each pass binds its immutable parameters then publishes before the next consumer");
    const auto size=threadgroupSizeForFunctionName("c_build_sun_hierarchy");
    require(size.width==kSourceFaceHierarchyWorkgroupSize && size.height==1 && size.depth==1,
            "Metal hierarchy workgroup width agrees with the layout");
    // A grown pool dispatches the passes and groups its leaf allocation needs.
    for(uint capacity:{131072u,262144u,kSourceFaceCapacityCeiling}){
        events.clear();groups.clear();
        fixture.recordLayout_=SourceFaceLayout{capacity};
        fixture.buildOverflowHierarchy();
        const uint leaves=capacity/kSourceFaceHierarchyLeafSize;
        std::vector<uint> expected;
        for(uint level=leaves;;){
            level=(level+kSourceFaceHierarchyWorkgroupSize-1)/kSourceFaceHierarchyWorkgroupSize;
            expected.push_back(level<1?1:level);
            if(level<=1)break;
        }
        if(expected.size()<2)expected.push_back(1);
        require(groups==expected,"grown pool dispatches one group per subtree per pass");
        require(fixture.recordLayout_.buildPasses()==int(expected.size()),
                "pass count matches the dispatched groups");
    }
}
""".replace("CONSTANTS", constants).replace("DISPATCH", dispatch).replace("MAPPING", mapping)
    if mutation:
        before, after = mutation
        if code.count(before) != 1:
            raise ValueError(f"expected one dispatch mutation site for {before!r}")
        code = code.replace(before, after)
    return code


@unittest.skipUnless(receiver.COMPILER, "hierarchy controls require a C++ compiler")
class SunHierarchyTest(unittest.TestCase):
    def test_workgroup_phase_schedule(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            with self.subTest(backend=suffix):
                shader = (receiver.SHADERS / directory /
                          f"c_build_sun_hierarchy.{suffix}").read_text()
                layout = (receiver.SHADERS / directory /
                          f"ir_sun_face_query_layout.{suffix}").read_text()
                width = int(re.search(r"kSourceFaceHierarchyWorkgroupSize = (\d+)u;", layout)[1])
                self.assertEqual(width, 128)
                marker = "void main()" if suffix == "glsl" else "kernel void c_build_sun_hierarchy("
                kernel = compact(extract_block(shader, marker))
                barrier = ("barrier();" if suffix == "glsl" else
                           "threadgroup_barrier(mem_flags::mem_threadgroup);")
                self.assertEqual(kernel.count(barrier), 2)
                self.assertRegex(kernel, r"initializeSourceSunHierarchy\([^;]+; " +
                                 re.escape(barrier))
                self.assertRegex(kernel, r"reduceSourceSunHierarchy\([^;]+; " + re.escape(barrier))
                self.assertIn("if (pass != 0u && subtrees == 1u && lane == 0u)", kernel)
                self.assertNotRegex(kernel, r"if \([^)]*lane[^)]*\) return")
                if suffix == "glsl":
                    self.assertIn(f"local_size_x = {width}", shader)
                else:
                    self.assertIn("lane [[thread_index_in_threadgroup]]", shader)
                    self.assertIn("groupId [[threadgroup_position_in_grid]]", shader)

    def test_cpu_dispatch_and_metal_width_with_mutations(self):
        harness = receiver.SourceFaceOverflowTest()
        run = harness.compile_and_run(dispatch_source())
        self.assertEqual(run.returncode, 0, run.stderr)
        mutations = {
            "omit_publication": (
                "IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);", ""),
            "reuse_pass_zero_parameters": (
                "hierarchyFrames_[pass]->bindBase", "hierarchyFrames_[0]->bindBase"),
            "omit_second_pass": ("pass < passes", "pass < 1"),
            "wrong_group_count": ("recordLayout_.buildGroups(pass);",
                                  "recordLayout_.buildGroups(pass) / 2u;"),
            "wrong_metal_width": ("if (functionName == \"c_build_sun_hierarchy\") {\n"
                                  "        return MTL::Size(128, 1, 1);",
                                  "if (functionName == \"c_build_sun_hierarchy\") {\n"
                                  "        return MTL::Size(64, 1, 1);"),
        }
        for name, mutation in mutations.items():
            with self.subTest(mutation=name):
                run = harness.compile_and_run(dispatch_source(mutation))
                self.assertNotEqual(run.returncode, 0, "mutation escaped dispatch controls")

    def test_production_build_and_query(self):
        harness = receiver.SourceFaceOverflowTest()
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            with self.subTest(backend=suffix):
                run = harness.compile_and_run(source(suffix, directory))
                self.assertEqual(run.returncode, 0, run.stderr)
                print(f"{suffix}: {run.stdout}", end="")

    def test_mutations(self):
        mutations = {
            "disable_hierarchy": ("const bool queryHierarchy = !sourceQueryComplete",
                                  "const bool queryHierarchy = false"),
            "truncate_tree": ("++node;", "break;"),
            "drop_last_leaf_record": (
                "i < min(records, first + kSourceFaceHierarchyLeafSize)",
                "i + 1u < min(records, first + kSourceFaceHierarchyLeafSize)"),
            "accept_incomplete_readiness": (
                "hierarchyLeaves == sourceSunHierarchyLeafCount(retainedFaces)", "true"),
            "demote_analytic_bounds": ("if (bounds.low.w > 0.0) return true;", ""),
            "ignore_depth_window": ("separation < maxShadowThrow", "true"),
            "ignore_lost_bit": ("return (count & kSourceFaceTileRecordLost) == 0u;",
                                "return true;"),
            "query_lost_tile_hierarchy": (
                "!sourceQueryComplete && sourceFaceTileRetainsRecords(count) &&",
                "!sourceQueryComplete &&"),
            "count_unpublished_records": (
                "return requested < capacity ? requested : capacity;",
                "return requested;"),
            "reject_exhausted_build": ("if (records == 0u ||",
                                       "if (records == 0u || records >= capacity ||"),
            "publish_subtree_as_complete": ("pass != 0u && subtrees == 1u && lane == 0u",
                                            "lane == 0u"),
            "omit_top_root": ("stage > 0u; stage /= 2u", "stage > 1u; stage /= 2u"),
            "alias_subtrees": ("const uint rootNode = subtrees + group;",
                               "const uint rootNode = subtrees;"),
            "drop_reduction_lanes": ("rootNode * level + lane, bounds",
                                     "rootNode * level, bounds"),
            "drop_upper_subtree_roots": ("const uint subtrees = level / width;",
                                         "const uint subtrees = max(1u, level / width / 2u);"),
            "skip_pass_reduction": ("built < pass; ++built", "built < pass + 1u; ++built"),
        }
        harness = receiver.SourceFaceOverflowTest()
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            for name, mutation in mutations.items():
                with self.subTest(backend=suffix, mutation=name):
                    run = harness.compile_and_run(source(suffix, directory, mutation))
                    self.assertNotEqual(run.returncode, 0, "mutation escaped hierarchy controls")


if __name__ == "__main__":
    unittest.main()

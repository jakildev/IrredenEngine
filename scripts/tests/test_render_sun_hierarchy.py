"""Execute production hierarchy builds and receivers through serial CPU adapters.

The independent triangle oracle covers finite geometry. Analytic records use a
sentinel predicate to isolate conservative node admission and type dispatch;
GPU barriers, upload bindings and analytic intersections need native controls.
"""

import re
import unittest

import test_render_source_face_overflow as receiver
from shader_contract_helpers import extract_block, extract_function

CASES = receiver.CASES[:receiver.CASES.index("int main(){")] + r"""
void buildLevel(uint level){
    for(uint id=0;id<kSourceFaceHierarchyMaxLeaves;++id)buildInvocation(id,level);
}
void build(){
    sunDepthBuf.words[kSourceFaceHierarchyNeededOffset]=1;
    buildLevel(0);
    for(uint level=kSourceFaceHierarchyMaxLeaves/2;level>0;level/=2)buildLevel(level);
    sunDepthBuf.readCount=0;
}
void clear(){
    for(uint id=0;id<kSourceFaceFallbackOffset;++id)clearInvocation(id);
    sunDepthBuf.readCount=0;
}
int main(){
    const uint empty=0xffffffffu;
    const Face distant{{2,3,-120},{1,0,0},{0,1,0}};
    const Face miss{{2,3,2},{.25f,0,0},{0,1,0}};
    const Face hit{{2.8f,3,2},{.2f,0,0},{0,1,0}};
    uint comparisons=0;
    for(uint cascade:{0u,1u})for(uint count:{257u,513u,65536u}){
        for(uint position:{0u,count/2,count-1}){
            reset(count,count,cascade);
            for(uint i=0;i<count;++i)writeFace(i==position?hit:distant);
            fallback(cascade,empty,depth(-120));
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
    for(uint count:{1u,8u,9u,16u,17u,256u,257u,65536u}){
        reset(count,257);fill(count,hit);
        sunDepthBuf.words[kSourceFaceHierarchyNeededOffset]=1;
        buildLevel(0);
        uint expected=1;while(expected*8<count)expected*=2;
        require(sunDepthBuf.words[kSourceFaceHierarchyReadyOffset]==(expected==1?1u:0u),
                "leaf dispatch cannot publish incomplete parents");
        for(uint level=kSourceFaceHierarchyMaxLeaves/2;level>1;level/=2)buildLevel(level);
        require(sunDepthBuf.words[kSourceFaceHierarchyReadyOffset]==(expected==1?1u:0u),
                "nonroot dispatch cannot publish readiness");
        buildLevel(1);
        require(sunDepthBuf.words[kSourceFaceHierarchyReadyOffset]==expected,
                "root publishes dynamic power-of-two leaf count");
        sunDepthBuf.readCount=0;
        require(sample()==1,"small and padded trees retain exact geometry");
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
            "overflowed record pool cannot certify exact hierarchy");
    fallback(0,empty,depth(2));
    require(sample()==1,"overflowed pool preserves sampled fallback");
    reset(257,257);fill(257,hit);build();clear();
    require(sunDepthBuf.words[kSourceFaceHierarchyReadyOffset]==0 &&
            sunDepthBuf.words[kSourceFaceHierarchyNeededOffset]==0,
            "clear retires stale hierarchy metadata");
    sunDepthBuf.words[kSourceFaceHeaderOffset]=257;
    sunDepthBuf.words[tileBase(0)]=257;
    require(sample()==0 && sunDepthBuf.readCount==0,"stale tree cannot shadow next frame");
    buildLevel(0);buildLevel(1);
    require(sunDepthBuf.words[kSourceFaceHierarchyReadyOffset]==0,
            "unneeded build cannot publish stale tree");
    for(uint count:{0u,65537u}){
        reset(count,257);sunDepthBuf.words[kSourceFaceHierarchyNeededOffset]=1;
        buildLevel(0);buildLevel(1);
        require(sunDepthBuf.words[kSourceFaceHierarchyReadyOffset]==0 &&
                sunDepthBuf.readCount==0,"empty or overflowed pool skips build");
    }
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
    std::cout<<comparisons<<" hierarchy overlap/order controls; readiness, clear, pool, "
               "analytic admission, mixed-layer controls and "<<hits+misses
             <<" independent triangle rays passed\n";
}
"""


def source(suffix, directory, mutation=None):
    code = receiver.host_source(suffix, directory, False, cases=CASES)
    shader = (receiver.SHADERS / directory / f"c_build_sun_hierarchy.{suffix}").read_text()
    write = extract_function(shader, "writeSourceSunBounds")
    marker = "void main()" if suffix == "glsl" else "kernel void c_build_sun_hierarchy("
    kernel = extract_block(shader, marker)
    kernel = kernel[kernel.index("{") + 1:-1]
    kernel = re.sub(r"    const uint (?:id|level) = [^;]+;\n", "", kernel)
    clear = (receiver.SHADERS / directory / f"c_clear_sun_shadow_map.{suffix}").read_text()
    clear = clear[clear.index("    if (linearIdx >= kTotalTexels)"):]
    builder = ("constexpr int memory_order_relaxed=0;\n"
               "void atomic_store_explicit(uint* p,uint value,int){*p=value;}\n")
    builder += write + "\nvoid buildInvocation(uint id,uint level){\n" + kernel + "}\n"
    builder += ("void clearInvocation(uint linearIdx){\n"
                "const uint kTotalTexels=kSourceFaceFallbackOffset;\n" + clear)
    builder = (builder.replace("device uint* sunDepthBuf", "BoundBuffer& sunDepthBuf")
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


@unittest.skipUnless(receiver.COMPILER, "hierarchy controls require a C++ compiler")
class SunHierarchyTest(unittest.TestCase):
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
                "hierarchyLeaves == sourceSunHierarchyLeafCount(sourceFaceCount)", "true"),
            "demote_analytic_bounds": ("if (bounds.low.w > 0.0) return true;", ""),
            "ignore_depth_window": ("separation < maxShadowThrow", "true"),
            "accept_overflowed_pool": ("sourceFaceCount <= kSourceFaceCapacity", "true"),
            "publish_before_root": ("(leaves == 1u && level == 0u) || level == 1u", "true"),
        }
        harness = receiver.SourceFaceOverflowTest()
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            for name, mutation in mutations.items():
                with self.subTest(backend=suffix, mutation=name):
                    run = harness.compile_and_run(source(suffix, directory, mutation))
                    self.assertNotEqual(run.returncode, 0, "mutation escaped hierarchy controls")


if __name__ == "__main__":
    unittest.main()

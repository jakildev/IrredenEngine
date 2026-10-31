"""Execute the production PCF sampler with independent caster-depth layers.

The oracle unions per-tap blockers before filtering. It does not validate map
rasterization or recover depths already lost inside an individual map layer.
"""

import unittest

import test_render_source_face_overflow as overflow

CONTROLS = overflow.CASES[:overflow.CASES.index("int main(){")] + r"""
uint packed(float z,uint marker){return (depth(z)&0xffffff00u)|marker;}
void tap(uint cascade,int x,int y,uint primary,uint indexed){
    const uint address=cascade*kCascadeTexelCount+y*kSunShadowMapDim+x;
    sunDepthBuf.words[address]=primary;
    sunDepthBuf.words[kSourceFaceFallbackOffset+address]=indexed;
}
int main(){
    const uint empty=0xffffffffu;
    uint comparisons=0;
    for(uint cascade:{0u,1u})for(uint marker:{0u,0x85u,0x89u}){
        for(bool reverse:{false,true}){
            for(float high:{1.0f,-89.0f,-93.0f,-120.0f}){
                reset(1,0,cascade);
                const uint low=packed(2,marker),upper=packed(high,marker);
                tap(cascade,2,3,reverse?empty:low,reverse?low:empty);
                const float before=sample({2.5f,3.5f},10,cascade,false);
                tap(cascade,2,3,reverse?upper:low,reverse?low:upper);
                require(before==1,"lower caster blocks the receiver");
                require(sample({2.5f,3.5f},10,cascade,false)==before,
                        "adding an out-of-range layer erased a valid shadow");
                ++comparisons;
            }
        }
    }
    for(uint cascade:{0u,1u}){
        reset(1,0,cascade);
        tap(cascade,2,3,packed(2,0),packed(1,0x89));
        tap(cascade,3,3,empty,packed(2,0x89));
        tap(cascade,2,4,packed(2,0),empty);
        tap(cascade,3,4,empty,empty);
        require(abs(sample({3,4},10,cascade,false)-.75f)<1e-6f,
                "each tap contributes once before filtering");
        reset(1,0,cascade);
        tap(cascade,2,3,packed(-120,0),packed(-130,0x89));
        require(sample({2.5f,3.5f},10,cascade,false)==0,
                "out-of-range-only casters stay rejected");
        reset(0,0,cascade);
        tap(cascade,2,3,empty,packed(2,0x89));
        require(sample({2.5f,3.5f},10,cascade,false)==0,
                "empty index does not read stale indexed depth");
        reset(1,0,cascade);
        tap(cascade,2,3,packed(3,0x70),packed(5,0x89));
        require(sample({2.5f,3.5f},10,cascade,false,{1,0,1})==1,
                "coplanar splat cannot hide the other layer");
        reset(1,0,cascade);
        tap(cascade,2,3,empty,packed(2,0x89));
        require(sample({2.5f,3.5f},10,cascade,true)==0,
                "complete finite miss stays authoritative");
    }
    std::cout<<comparisons<<" overlap comparisons plus weighted union and range controls\n";
}
"""

COMPLETE_CONTROLS = overflow.CASES[:overflow.CASES.index("int main(){")] + r"""
int main(){
    const uint empty=0xffffffffu;
    const Face low{{2,3,2},{1,0,0},{0,1,0}};
    uint comparisons=0;
    for(uint cascade:{0u,1u})for(uint count:{64u,65u,256u}){
        for(float high:{1.0f,-53.99f,-54.0f,-54.01f,-120.0f}){
            reset(count,count,cascade);
            fill(count-1,{{2,3,high},{1,0,0},{0,1,0}});
            writeFace(low);
            fallback(cascade,empty,depth(high));
            require(oracle({2.9f,3.5f},10,64),"lower caster lies in finite interval");
            require(sample({2.9f,3.5f},10,cascade,true,{0,0,1},64)==1,
                    "complete query lost lower caster behind upper depths");
            ++comparisons;
        }
        reset(count,count,cascade);
        fill(count,{{2,3,-120},{1,0,0},{0,1,0}});
        fallback(cascade,empty,depth(-120));
        require(!oracle({2.9f,3.5f},10,64),"distant-only oracle misses");
        require(sample({2.9f,3.5f},10,cascade,true,{0,0,1},64)==0,
                "distant-only complete query respects finite throw");
    }
    std::cout<<comparisons<<" complete overlap queries plus distant-only controls\n";
}
"""


@unittest.skipUnless(overflow.COMPILER, "shadow layer controls require a C++ compiler")
class ShadowLayerUnionTest(unittest.TestCase):
    def test_complete_queries_preserve_same_layer_blockers(self):
        harness = overflow.SourceFaceOverflowTest()
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            with self.subTest(backend=suffix):
                run = harness.compile_and_run(overflow.host_source(
                    suffix, directory, False, cases=COMPLETE_CONTROLS, include_pcf=True))
                self.assertEqual(run.returncode, 0, run.stderr)
                print(f"{suffix}: {run.stdout}", end="")
                mutant = harness.compile_and_run(overflow.host_source(
                    suffix, directory, False, cases=COMPLETE_CONTROLS, include_pcf=True,
                    mutation=("sourceQueryComplete && dot(normal, sunDir) > 0.0", "false")))
                self.assertNotEqual(mutant.returncode, 0)
                self.assertIn("complete query lost lower caster behind upper depths", mutant.stderr)

    def test_independent_caster_layers(self):
        harness = overflow.SourceFaceOverflowTest()
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            with self.subTest(backend=suffix):
                run = harness.compile_and_run(overflow.host_source(
                    suffix, directory, False, cases=CONTROLS, include_pcf=True))
                self.assertEqual(run.returncode, 0, run.stderr)
                print(f"{suffix}: {run.stdout}", end="")

    def test_union_mutations(self):
        harness = overflow.SourceFaceOverflowTest()
        mutations = {
            "drop_indexed_layer": (("layer < layerCount", "layer < 1"),
                                   "lower caster blocks the receiver"),
            "count_overlap_twice": (("shadowAccum += weight;\n                break;",
                                     "shadowAccum += weight;"),
                                    "adding an out-of-range layer erased a valid shadow"),
            "unbounded_shadows": (("if (depthDiff - bias >= maxShadowThrow) return false;", ""),
                                  "out-of-range-only casters stay rejected"),
        }
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            for name, (mutation, failure) in mutations.items():
                with self.subTest(backend=suffix, mutation=name):
                    run = harness.compile_and_run(overflow.host_source(
                        suffix, directory, False, mutation=mutation,
                        cases=CONTROLS, include_pcf=True))
                    self.assertNotEqual(run.returncode, 0)
                    self.assertIn(failure, run.stderr)


if __name__ == "__main__":
    unittest.main()

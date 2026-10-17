"""Execute production receiver counters with deterministic finite candidate lists.

The host harness covers the exact-query branch and its finite fallback. Analytic
intersection is stubbed to isolate candidate classification from shape geometry.
"""

import re
import subprocess
import unittest

import test_render_source_face_overflow as receiver

CASES = receiver.CASES[:receiver.CASES.index("int main(){")] + r"""
void clearStats(){
#if IR_SUN_QUERY_STATS
    sunQueryStats = {};
#endif
    sunDepthBuf.readCount=0;
}
void expect(std::initializer_list<uint> expected,const char* label){
#if IR_SUN_QUERY_STATS
    require(expected.size()==kSunQueryCounterCount,"all counters are checked");
    uint index=0;
    for(uint value:expected){
        if(sunQueryStats.counts[index]!=value){
            std::cerr<<label<<" counter "<<index<<": "
                     <<sunQueryStats.counts[index]<<" != "<<value<<"\n";
            std::exit(1);
        }
        ++index;
    }
#endif
}
void ordered(uint count,uint cascade=0){
    for(uint i=0;i<min(count,64u);++i)sunDepthBuf.words[tileBase(cascade)+1+i]=i;
    clearStats();
}
int main(){
    const Face miss{{2,3,2},{.25f,0,0},{0,1,0}};
    const Face degenerate{{2,3,2},{1,0,0},{2,0,0}};
    const Face behind{{2,3,11},{1,0,0},{0,1,0}};
    const Face self{{2,3,10},{1,0,0},{0,1,0}};
    const Face tooFar{{2,3,-90},{1,0,0},{0,1,0}};
    const Face hit{{2,3,2},{1,0,0},{0,1,0}};
    for(uint cascade:{0u,1u}){
        reset(6,6,cascade);
        writeFace(miss);writeFace(degenerate);writeFace(behind);
        writeFace(self);writeFace(tooFar);writeFace(hit);
        ordered(6,cascade);
        require(sample({2.9f,3.5f},10,cascade)==1,"final blocker result");
        expect({1,1,6,2,0,1,6,0,0,0},"footprint misses versus depth rejection");
        require(sunDepthBuf.readCount==6*kSourceFaceRecordWords,"six candidate reads");

        sunDepthBuf.words[tileBase(cascade)+1]=5;
        clearStats();
        require(sample({2.9f,3.5f},10,cascade)==1,"first blocker result");
        expect({1,1,1,0,0,1,1,0,0,0},"first blocker early exit");
        require(sunDepthBuf.readCount==kSourceFaceRecordWords,"early exit skips later records");
        require(sample({2.9f,3.5f},10,cascade)==1,"repeated blocker result");
        expect({2,2,2,0,0,2,2,0,0,0},"additive receiver counters");

        reset(5,5,cascade);
        writeFace(miss);writeFace(degenerate);writeFace(behind);
        writeFace(self);writeFace(tooFar);ordered(5,cascade);
        require(sample({2.9f,3.5f},10,cascade)==0,"exhausted query result");
        expect({1,1,5,2,0,0,0,1,0,0},"exhausted list");

        for(uint count:{64u,65u,256u}){
            reset(count,count,cascade);fill(count,miss);ordered(count,cascade);
            require(sample({2.9f,3.5f},10,cascade)==0,"bounded spill miss result");
            expect({1,1,count,count,0,0,0,1,0,0},"inline and spill visits");
            if(count>64){
                const uint firstSpill=kSourceFaceRecordOffset+(count-1)*kSourceFaceRecordWords;
                sunDepthBuf.words[firstSpill+3]=floatBitsToUint(1);
                clearStats();
                require(sample({2.9f,3.5f},10,cascade)==1,"spill blocker result");
                expect({1,1,65,64,0,1,65,0,0,0},"first spill blocker visits");
            }
        }

        for(uint count:{65u,257u}){
            reset(257,count,cascade);ordered(count,cascade);
            sunDepthBuf.words[kSourceFaceSpillCounterOffset]=kSourceFaceSpillCapacity+1;
            fallback(cascade,0xffffffffu,depth(1));
            require(sample({2.9f,3.5f},10,cascade)==1,"incomplete fallback result");
            expect({1,0,0,0,0,0,0,0,1,0},"incomplete skips all candidates");
            require(sunDepthBuf.readCount==0,"incomplete does not read geometry");
        }
        reset(257,257,cascade);ordered(257,cascade);
        require(sample({2.9f,3.5f},10,cascade)==0,"tile capacity fallback result");
        expect({1,0,0,0,0,0,0,0,1,0},"tile capacity incomplete");

        reset(1,0,cascade);clearStats();
        require(sample({2.9f,3.5f},10,cascade)==0,"empty tile result");
        expect({1,1,0,0,0,0,0,1,0,0},"empty complete tile");
        reset(0,0,cascade);clearStats();
        require(sample({2.9f,3.5f},10,cascade)==0,"empty pool result");
        expect({1,0,0,0,0,0,0,0,0,0},"empty pool bypass");

        reset(1,1,cascade);writeFace(hit);ordered(1,cascade);
        for(vec2 uv:{vec2(-1,3.5f),vec2(1024,3.5f),vec2(2.9f,-1),vec2(2.9f,1024)}){
            clearStats();
            require(sample(uv,10,cascade)==0,"outside result");
            expect({1,0,0,0,0,0,0,0,0,1},"outside query");
        }
        clearStats();
        require(sample({2.9f,3.5f},10,cascade,true,{0,0,-1})==0,"backface result");
        expect({1,0,0,0,0,0,0,0,0,0},"backface bypass");
        clearStats();
        require(sample({2.9f,3.5f},10,cascade,false)==0,"non-surface result");
        expect({1,0,0,0,0,0,0,0,0,0},"non-surface bypass");

        reset(3,3,cascade);writeFace(miss);writeFace(hit);writeFace(hit);ordered(3,cascade);
        sunDepthBuf.words[kSourceFaceRecordOffset+9]=1;
        sunDepthBuf.words[kSourceFaceRecordOffset+kSourceFaceRecordWords+9]=2;
        require(sample({2.9f,3.5f},10,cascade)==1,"analytic blocker result");
        expect({1,1,2,0,2,1,2,0,0,0},"analytic classification excludes face misses");
        require(sunDepthBuf.readCount==2*kSourceFaceRecordWords,"analytic early exit");
    }
    std::cout<<"deterministic query counters passed\n";
}
"""


def stats_source(suffix, directory, enabled=True):
    source = receiver.host_source(suffix, directory, False, cases=CASES, query_stats=enabled)
    stub = "float sourceShapeRaySeparation(vec2,float,uint,vec3,vec3,vec3){std::abort();}"
    if stub not in source:
        raise ValueError("analytic fixture stub is absent")
    return source.replace(stub, """
float sourceShapeRaySeparation(vec2,float,uint type,vec3,vec3,vec3){
    if(type==1)return -1;
    if(type==2)return 8;
    std::abort();
}
""")


class SunQueryStatsLayoutTest(unittest.TestCase):
    def test_runtime_and_backends_share_counter_slots(self):
        names = (
            "Cascades", "Indexed", "Candidates", "FaceMisses", "ShapeCandidates",
            "Blocked", "FirstBlockerVisits", "Exhausted", "Incomplete", "Outside",
        )
        expected = [(f"kSunQuery{name}", str(index)) for index, name in enumerate(names)]
        expected.append(("kSunQueryCounterCount", str(len(names))))
        runtime = (receiver.SHADERS.parents[3] / "engine/prefabs/irreden/render/systems/"
                   "system_trixel_to_framebuffer.hpp").read_text()
        constants = dict(re.findall(
            r"static constexpr std::size_t (k\w+)\s*=\s*(\d+);", runtime))
        self.assertEqual(int(constants["kSunQueryCounterCount"]), len(names))
        self.assertEqual(int(constants["kVisibilityCounterCount"]), 3)
        probe_words = re.search(
            r"static constexpr std::size_t kVisibilityProbeWords\s*=\s*([^;]+);", runtime)
        self.assertIsNotNone(probe_words)
        self.assertEqual(
            re.sub(r"\s+", "", probe_words[1]),
            "kVisibilityCounterCount+kSunQueryCounterCount+1",
            "probe tail reserves visibility counters, query counters, and validity marker")
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            with self.subTest(backend=suffix):
                source = (receiver.SHADERS / directory / f"ir_sun_query_stats.{suffix}").read_text()
                slots = re.findall(r"(?:const|constant) uint (kSunQuery\w+)\s*=\s*(\d+)u;", source)
                self.assertEqual(slots, expected, "named counters must preserve the probe ABI")


@unittest.skipUnless(receiver.COMPILER, "query counter controls require a C++ compiler")
class SunQueryStatsTest(unittest.TestCase):
    compile_and_run = receiver.SourceFaceOverflowTest.compile_and_run

    def test_enabled_counter_semantics_and_disabled_results(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            for enabled in (False, True):
                with self.subTest(backend=suffix, enabled=enabled):
                    run = self.compile_and_run(stats_source(suffix, directory, enabled))
                    self.assertEqual(run.returncode, 0, run.stderr)

    def test_every_counter_has_a_positive_control(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            source = stats_source(suffix, directory)
            counters = re.findall(r"(?:const|constant) uint (kSunQuery\w+) = \d+u;", source)
            counters.remove("kSunQueryCounterCount")
            self.assertEqual(len(counters), 10)
            for counter in counters:
                with self.subTest(backend=suffix, counter=counter):
                    mutated, changes = re.subn(
                        rf"sunQueryStats.counts\[{counter}\] \+= [^;]+;", ";", source)
                    self.assertGreater(changes, 0)
                    run = self.compile_and_run(mutated)
                    self.assertNotEqual(run.returncode, 0, "missing counter escaped controls")
                    self.assertIn(" counter ", run.stderr)

    def test_disabled_specialization_removes_counter_state(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            with self.subTest(backend=suffix):
                source = receiver.host_source(suffix, directory, False)
                preprocessed = subprocess.run(
                    [receiver.COMPILER, "-E", "-P", "-x", "c++", "-"],
                    input=source, capture_output=True, text=True)
                self.assertEqual(preprocessed.returncode, 0, preprocessed.stderr)
                self.assertNotRegex(preprocessed.stdout, r"SunQueryStats|sunQueryStats|kSunQuery")


if __name__ == "__main__":
    unittest.main()

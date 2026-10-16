"""Execute diagnostic publication after visibility rejection, with serial atomics."""

import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_block
from test_render_scatter_visibility import COMPILER, ROOT, routing_harness

CHECKS = r"""
int main(){
    Input in;
    reset();
    if(run(fragmentPrepass,in)||lightingCalls||visibilityCodes[15]!=0xffffffffu)return 1;
    in.depth=.75f;
    if(run(fragmentReplay,in)||lightingCalls||visibilityCodes[15]!=0xffffffffu)return 2;
    in.depth=.25f;
    if(!run(fragmentReplay,in)||lightingCalls!=1||visibilityCodes[15]!=1u||
       visibilityCodes[17]!=8u||visibilityCodes[25]!=0xffffffffu)return 3;
    for(uint i=0;i<10;++i)if(i!=0&&i!=2&&visibilityCodes[15+i]!=0xffffffffu)return 4;
    // The first sentinel wrap represents a positive count, not overflow.
    for(uint previous:{0xffffffffu,0u,0xfffffffdu,0xfffffffeu}){
        reset();visibilityCodes[15]=previous;
        if(!run(fragmentReplay,in))return 5;
        const uint64_t total=uint64_t(uint(previous+1u))+2u;
        if(visibilityCodes[15]+1u!=uint(total))return 6;
        if((visibilityCodes[25]==0xffffffffu)!=(total<0x100000000ull))return 7;
    }
    reset();visibilityCodes[17]=0xfffffff6u;
    if(!run(fragmentReplay,in)||visibilityCodes[25]!=0u)return 8;
    // A later writer cannot clear an overflow recorded by any counter.
    if(!run(fragmentReplay,in)||visibilityCodes[25]!=0u)return 9;
    reset();in.color.w=0.f;
    if(run(fragmentReplay,in)||lightingCalls||visibilityCodes[15]!=0xffffffffu)return 10;
    std::puts("query publication: visibility, zero increments, sentinel and overflow passed");
    return 0;
}
"""


def harness(root, suffix):
    stats = (root / f"ir_sun_query_stats.{suffix}").read_text()
    stats = stats.replace("constant uint", "const uint")
    routed = routing_harness(root, suffix)
    source = routed[:routed.index("#define IR_PER_AXIS_SURFACE_SHADOW 0")]
    source += ("#define IR_PER_AXIS_SURFACE_SHADOW 0\n"
               "#define IR_PER_AXIS_SURFACE_LIGHTING 1\n"
               "#define IR_PER_AXIS_VISIBILITY 1\n")
    for name in ("fragmentVisible", "fragmentPrepass", "fragmentReplay"):
        source += extract_block(routed, f"float {name}(Input in)") + "\n"
    source += CHECKS
    source = source.replace("std::array<uint,15>", "std::array<uint,26>")
    source = source.replace(
        "template<class... Args>vec3 worldSurfaceLighting(vec3,float,vec3 position,Args...){",
        "template<class... Args>vec3 worldSurfaceLighting(vec3,float,vec3 position,Args&&... args){"
        "auto refs=std::tie(args...);auto& stats=std::get<sizeof...(Args)-1>(refs);"
        "stats.counts[kSunQueryCascades]=2u;stats.counts[kSunQueryCandidates]=9u;")
    source = source.replace("using Fragment=", """
uint atomicExchange(uint& slot,uint value){uint old=slot;slot=value;return old;}
void atomic_store_explicit(uint* slot,uint value,int){*slot=value;}
using Fragment=""")
    return ("#include <tuple>\n#include <cstdint>\nusing uint=unsigned;\n"
            "#define IR_SUN_QUERY_STATS 1\n" + stats + source)


@unittest.skipUnless(COMPILER, "query publication controls require a C++ compiler")
class SunQueryPublicationTest(unittest.TestCase):
    def test_visibility_and_counter_publication(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            source = harness(ROOT / "engine/render/src/shaders" / folder, suffix)
            variants = {
                "production": source,
                "sentinel-is-overflow": source.replace("previous != 0xFFFFFFFFu && ", ""),
                "miss-exact-wrap": source.replace("previous >= 0xFFFFFFFFu - value",
                                                  "previous > 0xFFFFFFFFu - value"),
                "omit-publication": source.replace("if (value == 0u) continue;", "continue;"),
            }
            for name, candidate in variants.items():
                with (self.subTest(backend=suffix, mutation=name),
                      tempfile.TemporaryDirectory() as tmp):
                    if name != "production":
                        self.assertNotEqual(candidate, source)
                    cpp, executable = Path(tmp) / "probe.cpp", Path(tmp) / "probe"
                    cpp.write_text(candidate)
                    build = subprocess.run([COMPILER, "-std=c++17", "-O2", str(cpp),
                                            "-o", str(executable)], capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(executable)], capture_output=True, text=True)
                    if name == "production":
                        self.assertEqual(run.returncode, 0, run.stderr)
                        print(suffix, run.stdout.strip())
                    else:
                        self.assertNotEqual(run.returncode, 0)


if __name__ == "__main__":
    unittest.main()

"""Execute overflow packing and ordering over the complete signed depth domain."""

import re
import unittest

from shader_contract_helpers import extract_function
from test_render_overflow_append import run_host
from test_render_per_axis_caster_position import COMPILER, ROOT, functions

HOST = r"""
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <tuple>
using uint = std::uint32_t;
struct ivec2 { int x, y; };
"""

CHECKS = r"""
int main() {
    const ivec2 cells[] = {{0,0}, {65534,0}, {0,32767}, {65534,32767}, {8,8}};
    const int tails[] = {0, 32767};
    std::uint64_t cases=0;
    for(int depth=-65536; depth<65536; ++depth) {
        for(auto cell:cells) for(int tail:tails) for(bool cut:{false,true}) {
            const uint packed=packOverflowEntryCell(cell,cut);
            const uint key=uint(encodeDepthWithFaceFrac(depth,tail&3,
                (tail>>6)&15,(tail>>2)&15,(tail>>11)&15,(tail>>10)&1));
            const uint decoded=overflowEntryCell(packed);
            if(int(decoded&65535u)!=cell.x || int(decoded>>16u)!=cell.y ||
               overflowEntryIsCutFace(packed)!=cut ||
               decodeDepthPerAxis(int(key))!=depth) return 1;
            ++cases;
        }
    }
    const uint negativeKey=0xFFFF8000u;
    if(decodeDepthPerAxis(int(negativeKey))!=-1) return 2;
    const uint uncut=packOverflowEntryCell({8,8},false);
    const uint cut=packOverflowEntryCell({8,8},true);
    if(!recordLess(uncut,7,negativeKey,2,cut,7,negativeKey,1) ||
       recordLess(cut,7,negativeKey,1,uncut,7,negativeKey,2)) return 3;
    uint random=19381;
    auto next=[&]() { random=random*1664525u+1013904223u;return random; };
    for(int i=0;i<50000;++i) {
        uint a0=next()&0x7FFFFFFFu, b0=i%2==0?a0:next()&0x7FFFFFFFu;
        uint a1=next(), b1=i%5==0?a1:next(), a2=next(), b2=i%3==0?a2:next();
        uint a3=next(), b3=next();
        for(bool flag:{false,true}) {
            uint bit=flag?kOverflowCutFaceBit:0;
            const bool expected=std::tie(a0,a2,a1,a3)<std::tie(b0,b2,b1,b3);
            if(recordLess(a0|bit,a1,a2,a3,b0|bit,b1,b2,b3)!=expected) return 4;
        }
    }
    const uint edge=packOverflowEntryCell({65534,32767},true);
    if(!recordLess(edge,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,
                   0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu)) return 5;
    std::printf("round_trips=%llu signed_depths=131072 sort_pairs=100000 flag_tie=PASS\n",
                static_cast<unsigned long long>(cases));
}
"""


def adapter(dialect):
    folder = ROOT / "engine/render/src/shaders" / ("metal" if dialect == "metal" else "")
    common = (folder / f"ir_iso_common.{dialect}").read_text()
    sort = (folder / f"c_per_axis_overflow_sort.{dialect}").read_text()
    flag = re.search(r"(?:const|constant) uint kOverflowCutFaceBit = [^;]+;", common).group()
    helpers = "\n".join(extract_function(common, name) for name in (
        "packOverflowEntryCell", "overflowEntryCell", "overflowEntryIsCutFace",
        "encodeDepthWithFaceFrac", "decodeDepthPerAxis"))
    ordering = functions(sort, "overflowRecordLess" if dialect == "metal" else "recordLess")
    source = HOST + flag + helpers + ordering.replace("overflowRecordLess", "recordLess") + CHECKS
    return source.replace("constant uint", "const uint").replace("int2", "ivec2")


@unittest.skipUnless(COMPILER, "C++ host compiler required")
class OverflowEntryTest(unittest.TestCase):
    def test_full_signed_range_and_total_order(self):
        for dialect in ("glsl", "metal"):
            with self.subTest(backend=dialect):
                source = adapter(dialect)
                result = run_host(source)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("round_trips=2621440", result.stdout)
                print(f"{dialect}: {result.stdout.strip()}")
                for original, replacement in (
                    ("decodeDepthPerAxis(int(key))", "decodeDepthPerAxis(int(key & 0x7FFFFFFFu))"),
                    ("if (a0 != b0) return !overflowEntryIsCutFace(a0);",
                     "return false;"),
                    ("return word & ~kOverflowCutFaceBit;", "return word;"),
                    ("return a3 < b3;", "return false;"),
                ):
                    self.assertIn(original, source)
                    mutant = run_host(source.replace(original, replacement))
                    self.assertNotEqual(mutant.returncode, 0, original)


if __name__ == "__main__":
    unittest.main()

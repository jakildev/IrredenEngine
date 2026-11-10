"""Execute source-face order publication and consumption across the shared sort ABI.

The independent host sort retains four-word records; GPU dispatch publication is
covered separately by native rigid-cube captures.
"""

import re
import unittest

from shader_contract_helpers import extract_function
from test_render_overflow_append import run_host
from test_render_per_axis_caster_position import COMPILER, ROOT

SHADERS = ROOT / "engine/render/src/shaders"


def adapter(dialect):
    folder = SHADERS / ("metal" if dialect == "metal" else "")
    common = (folder / f"ir_iso_common.{dialect}").read_text()
    emitter = (folder / f"c_voxel_to_trixel_stage_2_body.{dialect}").read_text()
    vertex = (folder / ("ir_source_face_scatter_body.metal" if dialect == "metal" else
                        "ir_source_face_scatter_vertex.glsl")).read_text()
    sorter = (folder / f"c_per_axis_overflow_sort.{dialect}").read_text()
    constant = re.search(r"(?:const|constant) uint kFaceSortRecordWords = [^;]+;", common).group()
    begin = emitter.index("const uint order = 128u + index *")
    end = emitter.index("atomic_fetch_add_explicit(&perAxisWinnerIds[1u]" if dialect == "metal"
                        else "atomicAdd(perAxisWinnerIds[1u]", begin)
    publish = emitter[begin:end]
    reader = re.search(r"sourceOrder\[(128u \+ [^\]]+)\]", vertex).group(1)
    reader = reader.replace("uint(gl_InstanceID)", "instanceId")
    # The shared sorter has five record-address sites; every one must use the ABI width.
    addresses = re.findall(r"const uint b = ([^;]+);", sorter)
    if len(addresses) != 5 or any("kFaceSortRecordWords" not in a for a in addresses):
        raise AssertionError("sorter record addressing must share the source-order ABI")
    less_name = "overflowRecordLess" if dialect == "metal" else "recordLess"
    less = extract_function(sorter, less_name)
    helpers = "\n".join(extract_function(common, name) for name in (
        "overflowEntryCell", "overflowEntryIsCutFace"))
    cut = re.search(r"(?:const|constant) uint kOverflowCutFaceBit = [^;]+;", common).group()
    return (r"""
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>
using uint=std::uint32_t;
constexpr int memory_order_relaxed=0;
void atomic_store_explicit(uint* p,uint value,int){*p=value;}
""" + constant + cut + helpers + less + r"""
std::vector<uint> storage(128 + 2048 * kFaceSortRecordWords + 16, 0xffffffffu);
uint* perAxisWinnerIds=storage.data();
void publish(uint index,uint voxelIndex,uint slot){
""" + publish + r"""
}
uint read(uint instanceId){
    const uint* sourceOrder=storage.data();
    return sourceOrder[""" + reader + r"""];
}
int main(){
    constexpr uint count=1287;
    for(uint index=0;index<count;++index) publish(index,(index*73u)%count,index%3u);
    using Record=std::array<uint,4>;
    std::vector<Record> records(2048);
    for(uint i=0;i<2048;++i)for(uint word=0;word<4;++word)
        records[i][word]=storage.at(128+i*4+word);
    std::sort(records.begin(),records.end(),[](const Record&a,const Record&b){return LESS(
        a[0],a[1],a[2],a[3],b[0],b[1],b[2],b[3]);});
    for(uint i=0;i<2048;++i)for(uint word=0;word<4;++word)
        storage.at(128+i*4+word)=records[i][word];
    std::vector<bool> seen(count);
    uint previous=0;
    for(uint i=0;i<count;++i){
        const uint id=read(i);
        if(id>=count||seen[id])return 1;
        seen[id]=true;
        const uint key=((id*73u)%count)*3u+id%3u;
        if(i&&key<previous)return 2;
        previous=key;
    }
    for(uint i=0;i<128;++i)if(storage[i]!=0xffffffffu)return 3;
    for(uint i=128+2048*4;i<storage.size();++i)if(storage[i]!=0xffffffffu)return 4;
    std::printf("source_faces=%u exact_permutation=PASS sorted_keys=PASS guard=PASS\n",count);
}
""").replace("LESS(", less_name + "(").replace("constant uint", "const uint")


@unittest.skipUnless(COMPILER, "C++ host compiler required")
class SourceFaceOrderTest(unittest.TestCase):
    def test_shared_allocation_emission_sort_and_reader(self):
        types = (ROOT / "engine/render/include/irreden/render/ir_render_types.hpp").read_text()
        self.assertRegex(types, r"constexpr std::uint32_t kFaceSortRecordWords = 4;")
        system_path = ROOT / "engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp"
        system = system_path.read_text()
        self.assertIn("std::size_t{kFaceSortRecordWords} * orderCapacity", system)
        for dialect in ("glsl", "metal"):
            with self.subTest(backend=dialect):
                source = adapter(dialect)
                result = run_host(source)
                self.assertEqual(result.returncode, 0, result.stderr)
                print(f"{dialect}: {result.stdout}", end="")
                for before, after in (
                    ("index * kFaceSortRecordWords", "index * 3u"),
                    ("instanceId * kFaceSortRecordWords", "instanceId * 3u"),
                ):
                    self.assertIn(before, source)
                    mutant = run_host(source.replace(before, after))
                    self.assertNotEqual(mutant.returncode, 0, "three-word mutation escaped")


if __name__ == "__main__":
    unittest.main()

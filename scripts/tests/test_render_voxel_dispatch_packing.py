"""Execute indirect dispatch writers and stage lane recovery against exact coverage."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_function

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "engine/render/src/shaders"
COMPILER = shutil.which("c++")


def lane_prefix(source):
    start = re.search(r"(?:const )?uint compactedIdx\s*=", source).start()
    end = re.search(
        r"if\s*\(zIdx >= microSliceCount\)\s*(?:\{\s*return;\s*\}|return;)",
        source[start:]).end() + start
    return source[start:end]


def harness(backend):
    folder, suffix = (SHADERS, "glsl") if backend == "glsl" else (SHADERS / "metal", "metal")
    constants = (folder / f"ir_constants.{suffix}").read_text()
    packing = int(re.search(r"kStageMicroSlicesPerGroup\s*=\s*(\d+)", constants)[1])
    compact = (folder / f"c_voxel_visibility_compact.{suffix}").read_text()
    writer = extract_function(compact, "writeDispatchDims").replace("device ", "")
    slots = "\n".join(re.findall(r"constant uint kSlot\w+\s*=\s*\d+;", compact))
    writer = slots.replace("constant ", "constexpr ") + "\n" + writer
    consumers = []
    for stage, feeder in ((1, 0), (1, 1), (2, 0)):
        source = (folder / f"c_voxel_to_trixel_stage_{stage}_body.{suffix}").read_text()
        consumers.append(
            f"#define IR_FEEDER_PASS {feeder}\nvoid lane{stage}_{feeder}(int& index,int& slice){{\n"
            + lane_prefix(source) + "\nindex=int(compactedIdx);slice=zIdx;\n}\n"
            + "#undef IR_FEEDER_PASS\n")
    call = "writeDispatchDims(0, slices);"
    if backend == "metal":
        call = "writeDispatchDims(params, 0, slices);"
    return PREAMBLE.replace("@PACKING@", str(packing)) + writer + "\n" + "\n".join(consumers) + (
        CASES.replace("@WRITE@", call))


PREAMBLE = r"""
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>
using uint=unsigned;
using std::min;using std::max;
using std::atomic_uint;using std::atomic_load_explicit;using std::atomic_store_explicit;
using std::memory_order_relaxed;
constexpr int kStageMicroSlicesPerGroup=@PACKING@;
atomic_uint params[4]{};
uint atomicAdd(atomic_uint& value,uint add){return value.fetch_add(add);}
struct I2{int x,y;};struct U3{uint x,y,z;};
struct Frame{I2 voxelRenderOptions;int perAxisRoute,feederSubCap;}frameData;
auto& voxelRenderOptions=frameData.voxelRenderOptions;
auto& perAxisRoute=frameData.perAxisRoute;auto& feederSubCap=frameData.feederSubCap;
struct Params{uint numGroupsX,visibleCount;}indirectParams;
auto& numGroupsX=indirectParams.numGroupsX;auto& visibleCount=indirectParams.visibleCount;
U3 groupId{},localId3{};
auto& gl_WorkGroupID=groupId;auto& gl_LocalInvocationID=localId3;
"""

CASES = r"""
int main(){
 for(uint count:{0u,1u,1023u,1024u,1025u})for(int density=1;density<=16;++density)
 for(int mode:{0,1,2})for(int route:{0,1})for(int lane=0;lane<3;++lane){
  const bool feeder=lane==1;
  frameData={{mode,density},route,min(density,4)};
  const uint slices=mode!=0?(feeder?feederSubCap*feederSubCap:route==0?density*density:1):1;
  params[3]=count;@WRITE@
  const uint gx=params[0],gy=params[1],gz=params[2];
  if(gx!=max(1u,min(count,1024u))||gy!=max(1u,(count+gx-1u)/gx)||
     gz!=(slices+uint(kStageMicroSlicesPerGroup)-1u)/uint(kStageMicroSlicesPerGroup))return 1;
  indirectParams={gx,count};std::vector<unsigned char> seen(count*slices);
  for(uint y=0;y<gy;++y)for(uint x=0;x<gx;++x)for(uint z=0;z<gz;++z)
  for(uint local=0;local<uint(kStageMicroSlicesPerGroup);++local){
   groupId={x,y,z};localId3={0,0,local};int index=-1,slice=-1;
   if(lane==0)lane1_0(index,slice);else if(lane==1)lane1_1(index,slice);else lane2_0(index,slice);
   if(index<0)continue;
   if(uint(index)>=count||slice<0||uint(slice)>=slices)return 2;
   if(++seen[uint(index)*slices+uint(slice)]!=1)return 3;
  }
  if(!std::all_of(seen.begin(),seen.end(),[](auto n){return n==1;}))return 4;
 }
}
"""


@unittest.skipUnless(COMPILER, "dispatch controls require a C++ compiler")
class VoxelDispatchPackingTest(unittest.TestCase):
    def test_layouts_match_recovery(self):
        glsl = (SHADERS / "ir_constants.glsl").read_text()
        metal = (SHADERS / "metal/ir_constants.metal").read_text()
        pattern = r"kStageMicroSlicesPerGroup\s*=\s*(\d+)"
        packing = int(re.search(pattern, glsl)[1])
        self.assertEqual(int(re.search(pattern, metal)[1]), packing)
        for stage in (1, 2):
            body = (SHADERS / f"c_voxel_to_trixel_stage_{stage}_body.glsl").read_text()
            self.assertEqual(int(re.search(r"local_size_z\s*=\s*(\d+)", body)[1]), packing)
        registry = (ROOT / "engine/render/src/metal/metal_pipeline.cpp").read_text()
        start = registry.index('functionName == "c_voxel_to_trixel_stage_1"')
        end = registry.index("return MTL::Size", start)
        names = set(re.findall(r'functionName == "([^"]+)"', registry[start:end]))
        self.assertEqual(names, {
            "c_voxel_to_trixel_stage_1", "c_voxel_to_trixel_stage_1_feeder",
            "c_voxel_to_trixel_stage_1_winner_resolve", "c_voxel_to_trixel_stage_2",
            "c_voxel_to_trixel_stage_2_winner",
        })
        self.assertEqual(int(re.match(r"return MTL::Size\(2, 3, (\d+)\)", registry[end:])[1]),
                         packing)

    def test_exact_once_coverage_and_positive_controls(self):
        for backend in ("glsl", "metal"):
            source = harness(backend)
            for name, candidate, expected in (
                ("production", source, 0),
                ("lost_slice_group", source.replace("* kStageMicroSlicesPerGroup +", "* 0 +"), 3),
                ("lost_grid_row", source.replace(".y * numGroupsX", ".y * 0")
                 .replace(".y * indirectParams.numGroupsX", ".y * 0"), 3),
                ("lost_tail", source.replace("zIdx >= microSliceCount", "false"), 2),
                ("lost_count", source.replace("compactedIdx >= visibleCount", "false")
                 .replace("compactedIdx >= indirectParams.visibleCount", "false"), 2),
            ):
                with (self.subTest(backend=backend, variant=name),
                      tempfile.TemporaryDirectory() as tmp):
                    if name != "production":
                        self.assertNotEqual(candidate, source)
                    cpp, binary = Path(tmp) / "control.cpp", Path(tmp) / "control"
                    cpp.write_text(candidate)
                    build = subprocess.run([COMPILER, "-std=c++17", "-O2", str(cpp),
                                            "-o", str(binary)], capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    result = subprocess.run([str(binary)], capture_output=True, text=True)
                    self.assertEqual(result.returncode, expected, result.stderr)


if __name__ == "__main__":
    unittest.main()

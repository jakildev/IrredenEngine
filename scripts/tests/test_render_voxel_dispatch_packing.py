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
    start = re.search(r"const int microSliceCount\s*=", source)
    if start is None:
        raise ValueError("voxel stage lane-recovery start moved")
    end = re.search(
        r"if\s*\(zIdx >= microSliceCount\)\s*(?:\{\s*return;\s*\}|return;)",
        source[start.start():])
    if end is None:
        raise ValueError("voxel stage lane-recovery guard moved")
    return source[start.start():start.start() + end.end()]


def harness(backend):
    folder, suffix = (SHADERS, "glsl") if backend == "glsl" else (SHADERS / "metal", "metal")
    constants = (folder / f"ir_constants.{suffix}").read_text()
    packing = int(re.search(r"kStageMicroSlicesPerGroup\s*=\s*(\d+)", constants)[1])
    compact = (folder / f"c_voxel_visibility_compact.{suffix}").read_text()
    writer = extract_function(compact, "writeDispatchDims").replace("device ", "")
    finalizer = extract_function(compact, "finalizeDispatchDims")
    finalizer = finalizer.replace("device ", "").replace("constant ", "const ")
    finalizer = finalizer.replace("FrameDataVoxelToTrixel", "Frame")
    slots = "\n".join(re.findall(
        r"(?:constant|const) uint (?:kSlot\w+|kPerAxisIndirectStrideUints)\s*=\s*\d+u?;",
        compact))
    writer = re.sub(r"\b(?:constant|const)\b", "constexpr", slots) + "\n" + writer
    consumers = []
    for stage, feeder in ((1, 0), (1, 1), (2, 0)):
        source = (folder / f"c_voxel_to_trixel_stage_{stage}_body.{suffix}").read_text()
        consumers.append(
            f"#define IR_FEEDER_PASS {feeder}\nvoid lane{stage}_{feeder}(int& index,int& slice){{\n"
            + lane_prefix(source) + "\nindex=int(compactedIdx);slice=zIdx;\n}\n"
            + "#undef IR_FEEDER_PASS\n")
    call = "finalizeDispatchDims();"
    if backend == "metal":
        call = "finalizeDispatchDims(params, frameData);"
    helper = (folder / f"ir_voxel_dispatch.{suffix}").read_text()
    return (PREAMBLE.replace("@PACKING@", str(packing))
            .replace("@PACK_VOXELS@", "true" if backend == "metal" else "false")
            + helper + "\n" + writer
            + "\n" + finalizer + "\n" + "\n".join(consumers)
            + CASES.replace("@FINALIZE@", call))


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
constexpr bool packVoxels=@PACK_VOXELS@;
uint expectedVoxelsPerGroup(uint slices){
 return packVoxels?max(uint(kStageMicroSlicesPerGroup)/slices,1u):1u;
}
atomic_uint params[192]{};
uint atomicAdd(atomic_uint& value,uint add){return value.fetch_add(add);}
struct I2{int x,y;};struct U3{uint x,y,z;};
struct U2{uint x,y;U2(uint a,uint b):x(a),y(b){}};
using uvec2=U2;using uint2=U2;
struct Frame{I2 voxelRenderOptions;int perAxisRoute,feederSubCap;}frameData;
auto& voxelRenderOptions=frameData.voxelRenderOptions;
auto& perAxisSplitStride=frameData.perAxisRoute;
auto& perAxisRoute=frameData.perAxisRoute;auto& feederSubCap=frameData.feederSubCap;
struct Params{uint numGroupsX,visibleCount;}indirectParams;
auto& numGroupsX=indirectParams.numGroupsX;auto& visibleCount=indirectParams.visibleCount;
U3 groupId{},localId3{};
auto& gl_WorkGroupID=groupId;auto& gl_LocalInvocationID=localId3;
"""

CASES = r"""
int main(){
 // Arbitrary sample counts exercise spare-lane rejection even where the
 // backend's physical width divides every supported smaller square density.
 for(uint slices:{3u,5u,7u,uint(kStageMicroSlicesPerGroup)-1u,
                  uint(kStageMicroSlicesPerGroup),uint(kStageMicroSlicesPerGroup)+1u}){
  const uint count=11,voxels=expectedVoxelsPerGroup(slices);
  std::vector<unsigned char> seen(count*slices);
  for(uint group=0;group<(count+voxels-1)/voxels;++group)
  for(uint z=0;z<(slices+kStageMicroSlicesPerGroup-1)/kStageMicroSlicesPerGroup;++z)
  for(uint local=0;local<uint(kStageMicroSlicesPerGroup);++local){
   auto lane=voxelDispatchLane(group,z,local,slices);
   if(lane.x>=count||lane.y>=slices)continue;
   if(++seen[lane.x*slices+lane.y]!=1)return 3;
  }
  if(!std::all_of(seen.begin(),seen.end(),[](auto n){return n==1;}))return 4;
 }
 for(int density=0;density<=16;++density)for(int mode:{0,1,2})
 for(int route:{0,1,2,3})for(int lane=0;lane<3;++lane){
  const bool feeder=lane==1;
  if(feeder&&route!=0)continue;
  frameData={{mode,density},route,min(density,4)};
  const uint edge=uint(max(density,1)),cap=uint(max(feederSubCap,1));
  const uint slices=mode!=0?(feeder?cap*cap:route==0?edge*edge:1):1;
  const uint voxels=expectedVoxelsPerGroup(slices);
  for(uint count:{0u,1u,voxels-1u,voxels,voxels+1u,1024u*voxels-1u,
                  1024u*voxels,1024u*voxels+1u}){
  for(auto& value:params)value=0;
  for(uint base:{0u,64u,128u})params[base+3]=count+base/64;
  @FINALIZE@
  for(uint list=0;list<uint(route==0?2:3);++list){
   const uint expectedSlices=mode==0||route!=0?1u:list==1?cap*cap:edge*edge;
   const uint expectedVoxels=expectedVoxelsPerGroup(expectedSlices);
   const uint groups=(count+list+expectedVoxels-1u)/expectedVoxels;
   const uint gx=max(1u,min(groups,1024u));
   if(params[list*64]!=gx||params[list*64+1]!=max(1u,(groups+gx-1u)/gx)||
      params[list*64+2]!=(expectedSlices+uint(kStageMicroSlicesPerGroup)-1u)/
                         uint(kStageMicroSlicesPerGroup))return 1;
  }
  const uint base=route!=0?uint(route-1)*64:feeder?64:0;
  const uint countForLane=count+base/64;
  const uint gx=params[base],gy=params[base+1],gz=params[base+2];
  indirectParams={gx,countForLane};std::vector<unsigned char> seen(countForLane*slices);
  for(uint y=0;y<gy;++y)for(uint x=0;x<gx;++x)for(uint z=0;z<gz;++z)
  for(uint local=0;local<uint(kStageMicroSlicesPerGroup);++local){
   groupId={x,y,z};localId3={0,0,local};int index=-1,slice=-1;
   if(lane==0)lane1_0(index,slice);else if(lane==1)lane1_1(index,slice);else lane2_0(index,slice);
   if(index<0)continue;
   if(uint(index)>=countForLane||slice<0||uint(slice)>=slices)return 2;
   if(++seen[uint(index)*slices+uint(slice)]!=1)return 3;
  }
  if(!std::all_of(seen.begin(),seen.end(),[](auto n){return n==1;}))return 4;
  }
 }
}
"""


@unittest.skipUnless(COMPILER, "dispatch controls require a C++ compiler")
class VoxelDispatchPackingTest(unittest.TestCase):
    def test_prefix_diagnostics(self):
        source = (SHADERS / "c_voxel_to_trixel_stage_1_body.glsl").read_text()
        with self.assertRaisesRegex(ValueError, "lane-recovery start moved"):
            lane_prefix(source.replace("const int microSliceCount", "const int missingSlices"))
        with self.assertRaisesRegex(ValueError, "lane-recovery guard moved"):
            lane_prefix(source.replace("zIdx >= microSliceCount", "false"))

    def test_layouts_match_recovery(self):
        glsl = (SHADERS / "ir_constants.glsl").read_text()
        metal = (SHADERS / "metal/ir_constants.metal").read_text()
        pattern = r"kStageMicroSlicesPerGroup\s*=\s*(\d+)"
        packing = int(re.search(pattern, glsl)[1])
        metal_packing = int(re.search(pattern, metal)[1])
        for stage in (1, 2):
            body = (SHADERS / f"c_voxel_to_trixel_stage_{stage}_body.glsl").read_text()
            layout = re.search(
                r"layout\(local_size_x\s*=\s*(\d+),\s*local_size_y\s*=\s*(\d+),"
                r"\s*local_size_z\s*=\s*(\d+)\)", body)
            self.assertIsNotNone(layout, f"stage {stage}: voxel workgroup layout moved")
            self.assertEqual(tuple(map(int, layout.groups())), (2, 3, packing))
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
                         metal_packing)

    def test_exact_once_coverage_and_positive_controls(self):
        for backend in ("glsl", "metal"):
            source = harness(backend)
            finalizer = extract_function(source, "finalizeDispatchDims")
            wrong_route = finalizer.replace("voxelRenderOptions.y, 0", "voxelRenderOptions.y, 1")
            variants = [
                ("production", source, 0),
                ("wrong_visible_domain", source.replace(finalizer, wrong_route), 1),
                ("wrong_list_count", source.replace("params[base + 3u]", "params[3u]")
                 .replace("indirectParams[base + kSlotVisibleCount]",
                          "indirectParams[kSlotVisibleCount]"), 1),
                ("lost_slice_group", source.replace(
                    "groupZ * uint(kStageMicroSlicesPerGroup)", "groupZ * 0u"), 3),
                ("lost_grid_row", source.replace(".y * numGroupsX", ".y * 0")
                 .replace(".y * indirectParams.numGroupsX", ".y * 0"), 3),
                ("lost_tail", source.replace("zIdx >= microSliceCount", "false"), 2),
                ("lost_count", source.replace("compactedIdx >= visibleCount", "false")
                 .replace("compactedIdx >= indirectParams.visibleCount", "false"), 2),
            ]
            if backend == "metal":
                variants.extend([
                    ("lost_voxel_group", source.replace(
                        "groupIndex * voxelsPerGroup", "groupIndex"), 3),
                    ("lost_lane_padding", source.replace(
                        "voxelOffset >= voxelsPerGroup", "false"), 3),
                ])
            else:
                variants.append(("lost_voxel_group", source.replace(
                    "uvec2(groupIndex,", "uvec2(0u,"), 3))
            for name, candidate, expected in variants:
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

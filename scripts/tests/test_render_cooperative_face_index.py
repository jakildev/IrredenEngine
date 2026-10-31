"""Execute cooperative shader indexers on 64 host threads with real barriers.

This checks record ownership, tile partition and bounded writes. Native GPU
execution remains necessary to validate backend memory behavior.
"""

import subprocess
import tempfile
import unittest
from pathlib import Path

from test_render_source_face_index import COMPILER, PREAMBLE, ROOT, load_shader, shader_to_cpp

CASES = r"""
#include <atomic>
#include <barrier>
#include <thread>
std::barrier<> groupBarriers[]={std::barrier(64),std::barrier(64)};
std::atomic<uint> barrierCalls{0};
thread_local uint groupIndex=0;
namespace mem_flags { constexpr int mem_threadgroup=0; }
void threadgroup_barrier(int){++barrierCalls;groupBarriers[groupIndex].arrive_and_wait();}
void barrier(){++barrierCalls;groupBarriers[groupIndex].arrive_and_wait();}
thread_local struct {uint x;} gl_LocalInvocationID;
std::atomic<uint> sharedFaceIndices[2];
SHADER
int main(){
 const uint guard=32,canary=0xa5c31e79u;
 std::vector<uint> buffer(kSourceFaceBufferWords+2*guard,canary);
 sunDepthBuf={buffer.data()+guard,kSourceFaceBufferWords};
 for(int fixture=0;fixture<18;++fixture){
  barrierCalls=0;
  std::fill(buffer.begin(),buffer.end(),canary);
  std::fill(sunDepthBuf.data,sunDepthBuf.data+kSourceFaceBufferWords,0);
  for(uint tile=0;tile<32768;++tile)
   sunDepthBuf[kSourceFaceSpillHeadOffset+tile]=0xffffffffu;
  const bool giant=fixture==1||fixture==2;
  const bool offMap=fixture==3,degenerate=fixture==4;
  const bool exhausted=fixture==5||fixture==9;
  const bool rectangle=fixture>=7&&fixture<10;
  if(exhausted)sunDepthBuf[kSourceFaceHeaderOffset]=65536;
  sunDepthBuf[kSourceFaceCapacityOffset]=kSourceFaceCapacity;
  const bool poolFailure=fixture==13||fixture==14;
  const uint poolStart=fixture==13?262143:fixture==14?262144:0;
  sunDepthBuf[kSourceFaceSpillCounterOffset]=poolStart;
  const uint groups=fixture>=15?2:1;
  const unsigned repeats=fixture==2||fixture==10||poolFailure||fixture==15?65:
   fixture==11?256:fixture==12?257:fixture==16?128:fixture==17?129:3;
  const bool farOnly=fixture==6;
  const vec3 corner=rectangle?vec3{16,0,2}:farOnly?vec3{1200,32,2}:
   giant?vec3{-8,-8,2}:(offMap?vec3{-100,-100,2}:vec3{7,7,2});
  const vec3 u=rectangle?vec3{fixture==7?23.0f:519.0f,0,1.5}:
   giant?vec3{4096,0,0}:vec3{3,0,1.5};
  const vec3 v=rectangle?vec3{0,1023,-.75}:
   degenerate?u:(giant?vec3{0,4096,0}:vec3{0,3,-.75});
  std::vector<std::thread> workers;
  for(uint group=0;group<groups;++group)
   for(uint lane=0;lane<64;++lane)workers.emplace_back([&,lane,group]{
   groupIndex=group;
   gl_LocalInvocationID.x=lane;
   for(unsigned repeat=0;repeat<repeats;++repeat){
    const vec3 indexedCorner{corner.x,corner.y,
     fixture>=15?float(group*repeats+repeat):corner.z};
    if((lane+repeat+group)%3==0)std::this_thread::yield();
    CALL
   }
  });
  for(auto& worker:workers)worker.join();
  const uint records=offMap||degenerate?0:repeats*groups;
  if(sunDepthBuf[kSourceFaceHeaderOffset]!=records+(exhausted?65536:0))return 1;
  if(barrierCalls!=records*64*2)return 6;
  if(exhausted&&records&&sunDepthBuf[kSourceFaceHierarchyNeededOffset]!=1)return 16;
  uint completeSpillTiles=0,incompleteTiles=0;
  for(uint tile=0;tile<32768;++tile){
   const uint x=tile%128,y=(tile%16384)/128;
   const bool rectangleCovered=tile<16384?
    (x>=1&&x<=(fixture==7?4:66)):(x>=1&&x<=(fixture==7?2:33)&&y<=42);
   const bool covered=records &&
    (rectangle?rectangleCovered:farOnly?tile==16587:
     (giant||tile==0||tile==1||tile==128||tile==129||tile==16384||tile==16385));
   const uint base=kSourceFaceTileOffset+tile*65;
   const uint count=sunDepthBuf[base];
   if(covered&&poolFailure){
    if(count!=65&&count!=257)return 8;
    completeSpillTiles+=count==65;
    incompleteTiles+=count==257;
   }else if(count!=(covered?(exhausted?kSourceFaceTileRecordLost:records):0))return 2;
   std::vector<uint> ids;
   for(uint slot=0;slot<std::min(covered&&!exhausted?records:0,64u);++slot)
    ids.push_back(sunDepthBuf[base+1+slot]);
   if(covered&&!exhausted&&sourceFaceQueryComplete(count)&&count>64){
    uint node=sunDepthBuf[kSourceFaceSpillHeadOffset+tile];
    for(uint slot=64;slot<count;++slot){
     if(node>=262144)return 9;
     const uint address=kSourceFaceSpillNodeOffset+node*2;
     ids.push_back(sunDepthBuf[address]);
     node=sunDepthBuf[address+1];
    }
    if(node!=0xffffffffu)return 10;
   }
   std::sort(ids.begin(),ids.end());
   if(covered&&!exhausted&&sourceFaceQueryComplete(count)){
    if(ids.size()!=records)return 11;
    for(uint i=0;i<ids.size();++i)if(ids[i]!=i)return 3;
   }else if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())return 12;
  }
  if(poolFailure&&(completeSpillTiles!=(fixture==13?1u:0u)||
     completeSpillTiles+incompleteTiles!=6))return 13;
  std::vector<uint> identities;
  for(uint record=0;record<(exhausted?0:records);++record){
   if(sunDepthBuf[kSourceFaceRecordOffset+record*kSourceFaceRecordWords+9]!=0)return 7;
   vec3 expectedCorner=corner;
   if(fixture>=15){
    float identity;
    const uint bits=sunDepthBuf[kSourceFaceRecordOffset+record*kSourceFaceRecordWords+2];
    std::memcpy(&identity,&bits,4);
    if(identity<0||identity>=float(records)||identity!=float(uint(identity)))return 14;
    identities.push_back(uint(identity));
    expectedCorner.z=identity;
   }
   const vec3 vectors[]={expectedCorner,u,v};
   for(uint a=0;a<3;++a)for(uint b=0;b<3;++b)
    if(sunDepthBuf[kSourceFaceRecordOffset+record*kSourceFaceRecordWords+a*3+b]
       !=floatBitsToUint(vectors[a][b]))return 4;
  }
  if(fixture>=15){
   std::sort(identities.begin(),identities.end());
   for(uint i=0;i<records;++i)if(identities[i]!=i)return 15;
  }
  for(uint i=0;i<guard;++i)
   if(buffer[i]!=canary||buffer[buffer.size()-1-i]!=canary)return 5;
 }
 std::cout<<"64 lanes/group: shared records, spill partitions, capacity failure, "
          <<"two-group insertion races and guards passed\n";
}
"""


@unittest.skipUnless(COMPILER, "cooperative index controls require a C++20 compiler")
class CooperativeFaceIndexTest(unittest.TestCase):
    def test_production_and_partition_mutations(self):
        preamble = PREAMBLE.replace(
            "#include <algorithm>", "#include <algorithm>\n#include <atomic>")
        preamble = preamble.replace(
            "std::uint64_t atomicAdds=0, atomicMaxes=0, atomicExchanges=0;",
            "std::atomic<std::uint64_t> atomicAdds{0}, atomicMaxes{0}, atomicExchanges{0};")
        preamble = preamble.replace("uint old=p;p+=n;return old;",
                                    "return std::atomic_ref<uint>(p).fetch_add(n);")
        preamble = preamble.replace(
            "uint old=p;p=std::max(p,n);return old;",
            "std::atomic_ref<uint> ref(p);uint old=ref.load();"
            "while(old<n&&!ref.compare_exchange_weak(old,n)){}return old;")
        preamble = preamble.replace("std::uint64_t atomicOrs=0;",
                                    "std::atomic<std::uint64_t> atomicOrs{0};")
        preamble = preamble.replace("uint old=p;p|=n;return old;",
                                    "return std::atomic_ref<uint>(p).fetch_or(n);")
        preamble = preamble.replace("uint old=p;p=n;return old;",
                                    "return std::atomic_ref<uint>(p).exchange(n);")
        preamble = preamble.replace("*p=n;", "std::atomic_ref<uint>(*p).store(n);")
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            shaders = ROOT / "engine/render/src/shaders" / folder
            layout = load_shader(shaders, "ir_sun_face_query_layout", suffix)
            source = load_shader(shaders, "ir_sun_face_index", suffix)
            barrier = ("barrier();" if suffix == "glsl" else
                       "threadgroup_barrier(mem_flags::mem_threadgroup);")
            variants = {
                "production": source,
                "missing_publication_barrier": source.replace(barrier, "", 1),
                "missing_lifetime_barrier": "".join(source.rsplit(barrier, 1)),
                "missing_tiles": source.replace("stride = 64u", "stride = 65u"),
                "duplicate_tiles": source.replace("item = lane", "item = 0u"),
                "duplicate_record": source.replace("lane == 0u", "lane < 2u"),
                "spill_query_truncated": source.replace(
                    "slot < kSourceFaceTileQueryCapacity", "slot < 65u"),
                "unmarked_capacity_failure": source.replace(
                    "kSourceFaceTileQueryCapacity + 1u", "0u"),
                "unmarked_record_loss": source.replace("kSourceFaceTileRecordLost", "0u"),
                "missing_record_hierarchy_request": source.replace(
                    "faceIndex == capacity", "false"),
            }
            call = ("indexSourceSunFace(indexedCorner,u,v);" if suffix == "glsl" else
                    "indexSourceSunFace(sunDepthBuf.data,indexedCorner,u,v,sunFrame,"
                    "lane,sharedFaceIndices[groupIndex]);")
            for name, variant in variants.items():
                with (self.subTest(backend=suffix, variant=name),
                      tempfile.TemporaryDirectory() as tmp):
                    if name != "production":
                        self.assertNotEqual(variant, source)
                    shader = layout + "\n" + variant
                    if suffix == "glsl":
                        shader = shader.replace("sharedFaceIndex", "sharedFaceIndices[groupIndex]")
                    shader = shader_to_cpp(shader.replace(
                        "threadgroup uint&", "std::atomic<uint>&"))
                    path = Path(tmp)
                    (path / "index.cpp").write_text(
                        "#define IR_SUN_FACE_INDEX_COOPERATIVE\n" + preamble
                        + CASES.replace("SHADER", shader).replace("CALL", call))
                    build = subprocess.run([COMPILER, "-std=c++20", "-pthread", "-O2",
                                            str(path / "index.cpp"), "-o", str(path / "index")],
                                           capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(path / "index")], capture_output=True,
                                         text=True, timeout=60)
                    if name == "production":
                        self.assertEqual(run.returncode, 0, run.stderr)
                        print(suffix, run.stdout.strip())
                    else:
                        self.assertNotEqual(run.returncode, 0, "partition mutation escaped")


if __name__ == "__main__":
    unittest.main()

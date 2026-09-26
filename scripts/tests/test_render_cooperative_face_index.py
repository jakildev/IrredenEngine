"""Execute cooperative shader indexers on 64 host threads with real barriers.

This checks record ownership, tile partition and bounded writes. Native GPU
execution remains necessary to validate backend memory behavior.
"""

import subprocess
import tempfile
import unittest
from pathlib import Path

from test_render_source_face_index import COMPILER, PREAMBLE, ROOT

CASES = r"""
#include <atomic>
#include <barrier>
#include <thread>
std::barrier groupBarrier(64);
std::atomic<uint> barrierCalls{0};
namespace mem_flags { constexpr int mem_threadgroup=0; }
void threadgroup_barrier(int){++barrierCalls;groupBarrier.arrive_and_wait();}
void barrier(){++barrierCalls;groupBarrier.arrive_and_wait();}
thread_local struct {uint x;} gl_LocalInvocationID;
std::atomic<uint> sharedFaceIndex{0};
SHADER
int main(){
 const uint guard=32,canary=0xa5c31e79u;
 std::vector<uint> buffer(kSourceFaceBufferWords+2*guard,canary);
 sunDepthBuf={buffer.data()+guard,kSourceFaceBufferWords};
 for(int fixture=0;fixture<7;++fixture){
  barrierCalls=0;
  std::fill(buffer.begin(),buffer.end(),canary);
  std::fill(sunDepthBuf.data,sunDepthBuf.data+kSourceFaceBufferWords,0);
  const bool giant=fixture==1||fixture==2;
  const bool offMap=fixture==3,degenerate=fixture==4;
  const bool exhausted=fixture==5;
  if(exhausted)sunDepthBuf[kSourceFaceHeaderOffset]=65536;
  const unsigned repeats=fixture==2?65:3;
  const bool farOnly=fixture==6;
  const vec3 corner=farOnly?vec3{1200,32,2}:
   giant?vec3{-8,-8,2}:(offMap?vec3{-100,-100,2}:vec3{7,7,2});
  const vec3 u=giant?vec3{4096,0,0}:vec3{3,0,1.5};
  const vec3 v=degenerate?u:(giant?vec3{0,4096,0}:vec3{0,3,-.75});
  std::vector<std::thread> workers;
  for(uint lane=0;lane<64;++lane)workers.emplace_back([&,lane]{
   gl_LocalInvocationID.x=lane;
   for(unsigned repeat=0;repeat<repeats;++repeat){
    if((lane+repeat)%3==0)std::this_thread::yield();
    CALL
   }
  });
  for(auto& worker:workers)worker.join();
  const uint records=offMap||degenerate?0:repeats;
  if(sunDepthBuf[kSourceFaceHeaderOffset]!=records+(exhausted?65536:0))return 1;
  if(barrierCalls!=records*64*2)return 6;
  for(uint tile=0;tile<32768;++tile){
   const bool covered=records &&
    (farOnly?tile==16587:
     (giant||tile==0||tile==1||tile==128||tile==129||tile==16384||tile==16385));
   const uint base=kSourceFaceTileOffset+tile*65;
   if(sunDepthBuf[base]!=(covered?(exhausted?65:records):0))return 2;
   std::vector<uint> ids;
   for(uint slot=0;slot<std::min(covered&&!exhausted?records:0,64u);++slot)
    ids.push_back(sunDepthBuf[base+1+slot]);
   std::sort(ids.begin(),ids.end());
   for(uint i=0;i<ids.size();++i)if(ids[i]!=i)return 3;
  }
  for(uint record=0;record<(exhausted?0:records);++record){
   const vec3 vectors[]={corner,u,v};
   for(uint a=0;a<3;++a)for(uint b=0;b<3;++b)
    if(sunDepthBuf[kSourceFaceRecordOffset+record*9+a*3+b]!=floatBitsToUint(vectors[a][b]))return 4;
  }
  for(uint i=0;i<guard;++i)
   if(buffer[i]!=canary||buffer[buffer.size()-1-i]!=canary)return 5;
 }
 std::cout<<"64 lanes: shared records, complete tile partitions and guards passed\n";
}
"""


@unittest.skipUnless(COMPILER, "cooperative index controls require a C++20 compiler")
class CooperativeFaceIndexTest(unittest.TestCase):
    def test_production_and_partition_mutations(self):
        preamble = PREAMBLE.replace(
            "#include <algorithm>", "#include <algorithm>\n#include <atomic>")
        preamble = preamble.replace("std::uint64_t atomicAdds=0, atomicMaxes=0;",
                                    "std::atomic<std::uint64_t> atomicAdds{0}, atomicMaxes{0};")
        preamble = preamble.replace("uint old=p;p+=n;return old;",
                                    "return std::atomic_ref<uint>(p).fetch_add(n);")
        preamble = preamble.replace(
            "uint old=p;p=std::max(p,n);return old;",
            "std::atomic_ref<uint> ref(p);uint old=ref.load();"
            "while(old<n&&!ref.compare_exchange_weak(old,n)){}return old;")
        preamble = preamble.replace("*p=n;", "std::atomic_ref<uint>(*p).store(n);")
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            shaders = ROOT / "engine/render/src/shaders" / folder
            layout = (shaders / f"ir_sun_face_query_layout.{suffix}").read_text()
            geometry = (shaders / f"ir_projected_face.{suffix}").read_text()
            layout = layout.replace(f'#include "ir_projected_face.{suffix}"', geometry)
            source = (shaders / f"ir_sun_face_index.{suffix}").read_text()
            barrier = ("barrier();" if suffix == "glsl" else
                       "threadgroup_barrier(mem_flags::mem_threadgroup);")
            variants = {
                "production": source,
                "missing_publication_barrier": source.replace(barrier, "", 1),
                "missing_lifetime_barrier": "".join(source.rsplit(barrier, 1)),
                "missing_tiles": source.replace("stride = 64u", "stride = 65u"),
                "duplicate_tiles": source.replace("item = lane", "item = 0u"),
                "duplicate_record": source.replace("lane == 0u", "lane < 2u"),
            }
            call = ("indexSourceSunFace(corner,u,v);" if suffix == "glsl" else
                    "indexSourceSunFace(sunDepthBuf.data,corner,u,v,sunFrame,lane,sharedFaceIndex);")
            for name, variant in variants.items():
                with (self.subTest(backend=suffix, variant=name),
                      tempfile.TemporaryDirectory() as tmp):
                    if name != "production":
                        self.assertNotEqual(variant, source)
                    shader = layout + "\n" + variant
                    shader = (shader.replace("constant uint", "const uint")
                              .replace("constant FrameDataSun&", "const FrameDataSun&")
                              .replace("device atomic_uint*", "atomic_uint*")
                              .replace("threadgroup uint&", "std::atomic<uint>&")
                              .replace("float2", "vec2").replace("float3", "vec3")
                              .replace("int2", "ivec2").replace(".xy", ".xy()"))
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

"""Execute visibility allocation and draw orchestration with recording GPU adapters."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_block

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")
SOURCE = ROOT / "engine/prefabs/irreden/render/systems/system_trixel_to_framebuffer.hpp"


PREAMBLE = r"""
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>
void check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"%s\n",why);std::exit(1);}}
enum class BufferTarget{SHADER_STORAGE};
enum class BarrierType{ALL,SHADER_STORAGE};
enum class PolygonMode{FILL};
enum class DrawMode{TRIANGLES};
enum class IndexType{UNSIGNED_SHORT};
constexpr int kBufferIndex_IndirectDispatchParams=26,kBufferIndex_PerAxisCellCompacted=25;
constexpr int kPerAxisCellIndirectStrideBytes=32,BUFFER_STORAGE_DYNAMIC=1;
struct ivec2{int x,y;};
struct vec2{float x,y;};
struct vec4{float x,y,z,w;vec4(vec2 a,float b,float c):x(a.x),y(a.y),z(b),w(c){}};
struct Buffer;
Buffer* storage[32]{};
std::ptrdiff_t offsets[32]{};
int textures[8]{},used=0,residentMode=1,allocations=0,destructions=0,finishes=0,reads=0,logs=0;
bool depthTest=true,depthWrite=true,stats=false,prepassSelector=false;
bool sunReady=false,surfaceReady=false;
std::vector<std::string> events;
std::array<std::size_t,4> logged{};
void event(std::string value){events.push_back(value);}
struct Buffer{
 int id;bool owned=false;std::vector<std::uint32_t> words;
 explicit Buffer(int value):id(value){}
 Buffer(const void*,std::size_t bytes,int):id(26),owned(true),words(bytes/4){
  check(bytes%4==0,"allocation alignment");++allocations;
 }
 ~Buffer(){if(owned){check(storage[26]!=this,"destroyed bound visibility buffer");++destructions;}}
 void bindBase(BufferTarget,int slot){
  storage[slot]=this;offsets[slot]=0;event("bind"+std::to_string(slot));
 }
 void bindRange(BufferTarget,int slot,std::ptrdiff_t offset,std::size_t){
  storage[slot]=this;offsets[slot]=offset;
 }
 void getSubData(std::ptrdiff_t offset,std::size_t size,void* out){
  check(finishes>reads,"readback must wait");check(size==12,"only statistics tail read");
  check(std::size_t(offset)+size==words.size()*4,"statistics tail offset");
  std::memcpy(out,reinterpret_cast<char*>(words.data())+offset,size);++reads;
 }
};
struct Texture{int id;void bind(int slot){textures[slot]=id;if(slot==4)event("ao");}};
struct ShaderProgram{int id;void use(){used=id;event("program"+std::to_string(id));}};
struct C_FrameDataTrixelToFramebuffer{
 struct Data{int overflowMode_=9;vec4 scatterFbResolution_{{0,0},0,0};}frameData_;
 void updateFrameData(Buffer*){
  residentMode=frameData_.overflowMode_;
  const int flags=int(frameData_.scatterFbResolution_.z);
  stats=(flags&1)!=0;prepassSelector=(flags&2)!=0;
  event("upload"+std::to_string(residentMode));
 }
};
struct C_PerAxisTrixelCanvases{
 static constexpr int kAxisCount=3;
 struct AxisTextures{std::pair<int,Texture*> colors_,distances_,ao_;};
 std::array<Texture,9> textures{{{100},{101},{102},{200},{201},{202},{300},{301},{302}}};
 std::array<AxisTextures,3> axes_;
 Buffer cells{25},indirect{126},overflow{125};
 std::pair<int,Buffer*> cellCompacted_{0,&cells},cellIndirect_{0,&indirect},winnerIds_{0,&overflow};
 int cellRegionStride_=16,entriesBaseUints_=64,overflowCap_=8,ctrlBaseUints_=12;
 C_PerAxisTrixelCanvases(){for(int i=0;i<3;++i)
  axes_[i]={{0,&textures[i]},{0,&textures[i+3]},{0,&textures[i+6]}};
 }
};
struct Device{
 void setPolygonMode(PolygonMode){}
 void memoryBarrier(BarrierType value){event(value==BarrierType::ALL?"all":"storage");}
 void fillBuffer(const Buffer* buffer,std::size_t bytes,std::uint8_t value){
  check(buffer&&bytes==buffer->words.size()*4&&value==255,"full visibility sentinel clear");
  for(auto& word:const_cast<Buffer*>(buffer)->words)word=0xffffffffu;
  event("fill");
 }
 void setDepthTest(bool value){depthTest=value;event(value?"depth1":"depth0");}
 void setDepthWrite(bool value){depthWrite=value;event(value?"write1":"write0");}
 void drawElementsInstancedIndirect(DrawMode,IndexType,const Buffer* args,std::ptrdiff_t offset){
  const bool prepass=used==3&&prepassSelector,replay=used==3&&!prepassSelector;
  check(depthTest==!prepass&&depthWrite==!prepass,"depth state per pass");
  if(used==1||used==3)check(sunReady&&surfaceReady,"lighting bindings before either phase");
  check(residentMode==0||residentMode==1,"resident overflow selector");
  if(residentMode==0){
   const int axis=int(offset/kPerAxisCellIndirectStrideBytes);
   check(axis>=0&&axis<3&&offset==axis*32&&args->id==126,"regular indirect arguments");
   check(storage[25]&&storage[25]->id==25&&offsets[25]==axis*16*4,"regular cell region");
   check(textures[0]==100+axis&&textures[1]==200+axis,"regular textures");
   if(used==1||used==3)check(textures[4]==300+axis,"axis AO texture");
  }else{
   check(args->id==125&&offset==12*4,"overflow indirect arguments");
   check(storage[25]&&storage[25]->id==125&&offsets[25]==64*4,"overflow region");
  }
  if(prepass||replay){
   check(storage[26]&&storage[26]->owned,"visibility resource at slot 26");
   auto& words=storage[26]->words;const auto tail=words.size()-3;
   if(prepass){words[0]=7;if(stats)words[tail]+=3;}
   else{check(words[0]==7,"prepass must precede replay");
    if(stats){words[tail+1]+=2;words[tail+2]+=1;}}
  }
  event("draw"+std::to_string(residentMode));
 }
 void finish(){++finishes;}
} gpu;
namespace IRRender{Device* device(){return &gpu;}}
struct GpuSubStageScope{explicit GpuSubStageScope(const char*){}};
void log(const char*,std::uint32_t a,std::uint32_t b,std::uint32_t c,std::size_t pixels){
 ++logs;logged={a,b,c,pixels};
}
#define IR_LOG_INFO log
struct Framebuffer{ivec2 size;ivec2 getResolutionPlusBuffer(){return size;}};
struct Adapter{
 bool visibilityPrepassEnabled_=false,visibilityStatsEnabled_=false;
 bool scatterLightingEnabled_=false,scatterProbeEnabled_=false,overflowDrawDisabled_=false;
 int visibilityStatsFrameCount_=0;
 std::unique_ptr<Buffer> visibilityBuffer_;std::size_t visibilityPixelCount_=0;
 Buffer frame{3},voxelCells{225},voxelIndirect{226};Buffer* frameDataBuf_=&frame;
 ShaderProgram gather{0},lighting{1},visible{3},beauty{4},probe{5};
 ShaderProgram *program_=&gather,*scatterLightingProgram_=&lighting;
 ShaderProgram *visibleLightingProgram_=&visible,*scatterProgram_=&beauty;
 ShaderProgram *scatterProbeProgram_=&probe;
 void bindSunShadowResources(){sunReady=true;event("sun");}
 void bindSurfaceLightingResources(){surfaceReady=true;event("surface");}
 void restoreSurfaceLightingResources(){textures[4]=400;event("restoreSurface");}
 void restoreVoxelCompactionSlots(){
  storage[25]=&voxelCells;storage[26]=&voxelIndirect;event("restoreCompaction");
 }
"""

CASES = r"""
};
void appendDraws(std::vector<std::string>& wanted,bool overflow,bool ao){
 wanted.push_back("upload0");for(int axis=0;axis<3;++axis){
  if(ao)wanted.push_back("ao");wanted.push_back("draw0");
 }
 if(overflow){wanted.push_back("upload1");wanted.push_back("draw1");}
}
int main(){
 int cases=0;
 const char* flags[]={nullptr,"","0","1","01","11","true"};
 for(const char* flag:flags){
  Adapter a;a.readFlags(flag,flag);const bool expected=flag&&std::strcmp(flag,"1")==0;
  check(a.visibilityPrepassEnabled_==expected&&a.visibilityStatsEnabled_==expected,
        "exact environment opt-in");
 }
 for(bool enabled:{false,true})for(bool lighting:{false,true})for(bool probe:{false,true})
 for(bool overflow:{false,true})for(bool statistics:{false,true}){
  const int allocBefore=allocations,destroyBefore=destructions;
  {
   Adapter a;a.visibilityPrepassEnabled_=enabled;a.scatterLightingEnabled_=lighting;
   a.scatterProbeEnabled_=probe;a.overflowDrawDisabled_=!overflow;a.visibilityStatsEnabled_=statistics;
   const bool active=enabled&&lighting;Framebuffer framebuffer{{37,19}};
   a.prepare(framebuffer);check(allocations==allocBefore+int(active),"eligible allocation only");
   if(active)check(a.visibilityBuffer_->words.size()==37*19+3,"framebuffer and counter capacity");
   a.prepare(framebuffer);check(allocations==allocBefore+int(active),"allocation reuse");
   C_FrameDataTrixelToFramebuffer frameData;C_PerAxisTrixelCanvases axes;
   a.restoreVoxelCompactionSlots();events.clear();finishes=reads=logs=0;
   sunReady=surfaceReady=false;
   a.run(frameData,axes,vec2{37,19});
   check(frameData.frameData_.scatterFbResolution_.x==37&&
         frameData.frameData_.scatterFbResolution_.y==19&&
         frameData.frameData_.scatterFbResolution_.z==float(active&&statistics),
         "extent and stats flag");
   std::vector<std::string> wanted;
   if(probe||lighting)wanted.push_back("sun");if(lighting)wanted.push_back("surface");
   wanted.push_back("program"+std::to_string(active?3:lighting?1:probe?5:4));
   if(active){wanted.insert(wanted.end(),{"all","fill","storage","bind26","depth0","write0"});
    appendDraws(wanted,overflow,true);
    wanted.insert(wanted.end(),{"storage","depth1","write1"});}
   appendDraws(wanted,overflow,lighting);wanted.push_back("restoreCompaction");
   if(lighting)wanted.push_back("restoreSurface");wanted.push_back("program0");
   check(events==wanted,"clear/barrier/program/draw/restore order");
   check(storage[25]==&a.voxelCells&&storage[26]==&a.voxelIndirect,"restored borrowed slots");
   check(depthTest&&depthWrite&&used==0&&frameData.frameData_.overflowMode_==0&&!prepassSelector,
         "restored draw state");
   for(int frame=1;frame<181;++frame){events.clear();a.run(frameData,axes,vec2{37,19});}
   check(finishes==int(active&&statistics)&&reads==finishes&&logs==finishes,
         "bounded diagnostic readback");
   if(logs)check(logged==std::array<std::size_t,4>{std::size_t((overflow?4:3)*3),
      std::size_t((overflow?4:3)*2),std::size_t(overflow?4:3),37*19},"wrapping counter correction");
   framebuffer.size={19,37};a.prepare(framebuffer);
   check(allocations==allocBefore+int(active),"same capacity reuse");
   framebuffer.size={38,19};a.prepare(framebuffer);
   check(allocations==allocBefore+2*int(active)&&destructions==destroyBefore+int(active),
         "resize lifetime");
  }
  check(destructions==destroyBefore+2*int(enabled&&lighting),"owned buffer destruction");++cases;
 }
 std::printf("%d routing/lifecycle configurations\n",cases);
}
"""


def harness():
    source = SOURCE.read_text()
    scatter = extract_block(source, "    void drawPerAxisScatter(")
    gate = re.search(r"const bool visibilityPrepass\s*=\s*[^;]+;", scatter)[0]
    extent = re.search(r"frameData\.frameData_\.scatterFbResolution_\s*=\s*vec4\(.*?\);",
                       scatter, re.DOTALL)[0]
    start = scatter.index("        {\n", scatter.index("scatterDebugMode_ ="))
    route = scatter[start:scatter.rfind("}")]
    prepare = extract_block(
        source, "        if (visibilityPrepassEnabled_ && scatterLightingEnabled_)")
    flags = "\n".join(re.search(rf"sys->{name}\s*=.*?;", source, re.DOTALL)[0]
                      for name in ("visibilityPrepassEnabled_", "visibilityStatsEnabled_"))
    return (PREAMBLE + extract_block(source, "    void ensurePerAxisVisibilityBuffer(")
            + extract_block(source, "    void drawPerAxisFaces(")
            + "void readFlags(const char* visibilityPrepass,const char* visibilityStats){"
            + "auto* sys=this;" + flags + "}\n"
            + "void prepare(Framebuffer& framebuffer){\n" + prepare + "}\n"
            + "void run(C_FrameDataTrixelToFramebuffer& frameData,"
            + "const C_PerAxisTrixelCanvases& axes,vec2 framebufferResolution){\n"
            + gate + extent + route + "}\n" + CASES)


@unittest.skipUnless(COMPILER, "visibility routing controls require a C++ compiler")
class VisibilityRoutingTest(unittest.TestCase):
    def test_executed_allocation_draws_and_lifetime(self):
        source = harness()
        mutations = {
            "nonexact-environment": (r"&&\s*visibilityPrepass\[1\] == '\\0'", ""),
            "allocation-without-lighting": (
                r"if \(visibilityPrepassEnabled_ && scatterLightingEnabled_\)",
                "if (visibilityPrepassEnabled_)"),
            "prepass-without-lighting": (
                r"const bool visibilityPrepass = visibilityPrepassEnabled_"
                r" && scatterLightingEnabled_;",
                "const bool visibilityPrepass = visibilityPrepassEnabled_;"),
            "lost-clear": (r"IRRender::device\(\)->fillBuffer\(.*?\);", ""),
            "lost-clear-barrier": (
                r"IRRender::device\(\)->memoryBarrier\(BarrierType::ALL\);"
                r"(?=\s*IRRender::device\(\)->fillBuffer)",
                ""),
            "wrong-slot": (
                r"(?<=BufferTarget::SHADER_STORAGE,)\s*kBufferIndex_IndirectDispatchParams",
                " kBufferIndex_PerAxisCellCompacted"),
            "lost-replay-barrier": (
                r"(drawPerAxisFaces\(frameData, axes, true\);)\s*"
                r"IRRender::device\(\)->memoryBarrier\(BarrierType::SHADER_STORAGE\);", r"\1"),
            "lost-depth-restore": (r"IRRender::device\(\)->setDepthTest\(true\);", ""),
            "lost-write-restore": (r"IRRender::device\(\)->setDepthWrite\(true\);", ""),
            "lost-mode-reset": (
                r"frameData.frameData_.overflowMode_ = 0;(?=\s*frameData.updateFrameData)", ""),
            "lost-mode-upload": (
                r"(frameData.frameData_.overflowMode_ = 0;)"
                r"\s*frameData.updateFrameData\(frameDataBuf_\);",
                r"\1"),
            "missing-prepass-ao": (r"drawPerAxisFaces\(frameData, axes, true\)",
                                   "drawPerAxisFaces(frameData, axes, false)"),
            "lost-prepass-selector": (r"\(visibilityStatsEnabled_ \? 1 : 0\) \| 2",
                                      "(visibilityStatsEnabled_ ? 1 : 0)"),
            "lost-replay-selector": (
                r"frameData.frameData_.scatterFbResolution_.z\s*=\s*"
                r"visibilityPrepass && visibilityStatsEnabled_ \? 1.0f : 0.0f;", ""),
            "lost-prepass-sun": (r"(?<!void )\bbindSunShadowResources\(\);", ""),
            "lost-surface-bindings": (
                r"(?<!void )\bbindSurfaceLightingResources\(\);", ""),
            "separate-prepass-program": (
                r"drawPerAxisFaces\(frameData, axes, true\);",
                "scatterLightingProgram_->use(); drawPerAxisFaces(frameData, axes, true);"),
            "lost-compaction-restore": (r"(?<!\.)\brestoreVoxelCompactionSlots\(\);", ""),
            "lost-counter-tail": (r"\(pixels \+ 3u\) \* sizeof\(std::uint32_t\)",
                                  "pixels * sizeof(std::uint32_t)"),
            "lost-counter-correction": (r"counts\[0\] \+ 1u", "counts[0]"),
        }
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            for name, mutation in {"production": None, **mutations}.items():
                with self.subTest(name=name):
                    candidate = source
                    if mutation:
                        candidate, count = re.subn(*mutation, source, flags=re.DOTALL)
                        self.assertEqual(count, 1, f"mutation anchor: {name}")
                    cpp, binary = path / f"{name}.cpp", path / name
                    cpp.write_text(candidate)
                    compiled = subprocess.run([COMPILER, "-std=c++20", "-O0", str(cpp),
                                               "-o", str(binary)], capture_output=True, text=True)
                    self.assertEqual(compiled.returncode, 0, compiled.stderr)
                    result = subprocess.run([str(binary)], capture_output=True, text=True)
                    if mutation:
                        self.assertNotEqual(result.returncode, 0, f"survived: {name}")
                    else:
                        self.assertEqual(result.returncode, 0, result.stderr)
                        self.assertIn("32 routing/lifecycle configurations", result.stdout)


if __name__ == "__main__":
    unittest.main()

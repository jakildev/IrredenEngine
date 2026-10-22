"""Execute source-face routing and metadata with independent corner-depth bounds.

Serial atomics validate aggregation, not GPU publication. The corner oracle uses
a binary64 quaternion matrix; framebuffer depth precision needs native captures.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_block, extract_function

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "engine/render/src/shaders"
COMPILER = shutil.which("c++")

PREAMBLE = r"""
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <utility>
using uint=std::uint32_t;
using atomic_uint=uint;
using std::abs;
using std::max;
struct vec2 {float x,y;};
struct vec3 {float x,y,z;};
struct vec4 {
    float x,y,z,w;
    vec3 xyz()const{return {x,y,z};}
};
struct uvec2 {uint x,y;};
vec3 abs(vec3 v){return {std::abs(v.x),std::abs(v.y),std::abs(v.z)};}
uint floatBitsToUint(float v){uint result;std::memcpy(&result,&v,4);return result;}
float uintBitsToFloat(uint v){float result;std::memcpy(&result,&v,4);return result;}
template<class T,class U>T as_type(U v){T result;std::memcpy(&result,&v,4);return result;}
uint atomicMax(uint& target,uint value){uint old=target;target=std::max(target,value);return old;}
uint atomicOr(uint& target,uint value){uint old=target;target|=value;return old;}
constexpr int memory_order_relaxed=0;
uint atomic_fetch_max_explicit(uint* p,uint v,int){return atomicMax(*p,v);}
uint atomic_fetch_or_explicit(uint* p,uint v,int){return atomicOr(*p,v);}
uint atomic_load_explicit(const uint* p,int){return *p;}
void require(bool condition,const char* label){
    if(!condition){std::cerr<<label<<"\n";std::exit(1);}
}
"""

CASES = r"""
bool route(uint magnitude,vec4 q={0,0,0,1},float density=1,float scale=1,int offset=0,
           uint priority=0,uint invalid=0,int mode=0){
    return sourceCanvasEarlyDepth(magnitude,priority,invalid,q,density,scale,offset,mode,
                                  -65535,65535);
}
double cornerDepth(vec3 point,vec4 q,double density,double scale,int offset){
    const double x=q.x,y=q.y,z=q.z,w=q.w;
    const double rx=(1-2*y*y-2*z*z)*point.x+(2*x*y-2*w*z)*point.y+(2*x*z+2*w*y)*point.z;
    const double ry=(2*x*y+2*w*z)*point.x+(1-2*x*x-2*z*z)*point.y+(2*y*z-2*w*x)*point.z;
    const double rz=(2*x*z-2*w*y)*point.x+(2*y*z+2*w*x)*point.y+(1-2*x*x-2*y*y)*point.z;
    return (rx+ry+rz)*density*8*scale+offset;
}
int main(){
    const float infinity=std::numeric_limits<float>::infinity();
    const float nan=std::numeric_limits<float>::quiet_NaN();
    require(route(0),"ordinary empty metadata admits affine depth");
    for(uint priority:{1u,2u,3u,0xffffffffu})
        require(!route(0,{0,0,0,1},1,1,0,priority),"canvas priority requires fallback");
    for(uint priority:{0u,1u,2u,3u})for(uint invalid:{0u,1u})for(int mode:{0,1,2,3}){
        const bool expected=priority==0 && invalid==0 && mode==0;
        require(vertexAccept(0,{0,0,0,1},1,1,0,priority,invalid,mode,1)==expected &&
                vertexAccept(0,{0,0,0,1},1,1,0,priority,invalid,mode,0)!=expected,
                "vertex routing consumes all canvas metadata and priority inputs");
    }
    for(int mode:{-1,1,2,3})
        require(!route(0,{0,0,0,1},1,1,0,0,0,mode),"canvas depth mode requires fallback");
    for(uint invalid:{1u,2u,0xffffffffu})
        require(!route(0,{0,0,0,1},1,1,0,0,invalid),"invalid producer metadata requires fallback");
    for(float bad:{0.f,-1.f,infinity,-infinity,nan}){
        require(!route(0,{0,0,0,1},bad,1),"invalid density requires fallback");
        require(!route(0,{0,0,0,1},1,bad),"invalid depth scale requires fallback");
    }
    for(float bad:{infinity,-infinity,nan}){
        for(vec4 q:{vec4{bad,0,0,1},vec4{0,bad,0,1},vec4{0,0,bad,1},vec4{0,0,0,bad}})
            require(!route(0,q),"nonfinite quaternion requires fallback");
        require(!route(floatBitsToUint(bad)),"nonfinite or signed radius requires fallback");
    }
    require(!route(floatBitsToUint(-1)),"negative encoded radius requires fallback");
    require(!route(floatBitsToUint(3e38f)),"overflowed bound requires fallback");
    require(!route(0,{3e38f,0,0,1}),"overflowed rotation bound requires fallback");
    require(!route(0,{0,0,0,1},3e38f,3e38f),"overflowed scale bound requires fallback");
    require(!route(0,{0,0,0,1},1,1,-49094),"lower routing boundary is strict");
    require(route(0,{0,0,0,1},1,1,-49093),"first interior lower offset is admitted");
    require(route(0,{0,0,0,1},1,1,65478),"last interior upper offset is admitted");
    require(!route(0,{0,0,0,1},1,1,65479),"upper routing boundary is strict");

    uint admitted=0,rejected=0,corners=0;
    for(float magnitude:{0.f,.25f,1.f,16.f,128.f,1024.f})
    for(vec4 q:{vec4{0,0,0,1},vec4{0,0,.38268343f,.9238795f},
               vec4{.2f,-.3f,.4f,.84261495f},vec4{1,-2,.25f,-.5f},vec4{0,0,0,0}})
    for(float density:{.125f,1.f,3.f,16.f})for(float scale:{.25f,1.f,4.f})
    for(int offset:{-65535,-50000,-48000,0,32000,65000,65535}){
        const bool early=route(floatBitsToUint(magnitude),q,density,scale,offset);
        require(vertexAccept(floatBitsToUint(magnitude),q,density,scale,offset,0,0,0,1)==early &&
                vertexAccept(floatBitsToUint(magnitude),q,density,scale,offset,0,0,0,0)!=early,
                "specialized vertex draws select exactly one whole canvas");
        admitted+=early;rejected+=!early;
        if(!early)continue;
        for(int face=0;face<6;++face)for(int signX:{-1,1})
        for(int signY:{-1,1})for(int signZ:{-1,1})
        for(int u=0;u<2;++u)for(int v=0;v<2;++v){
            float components[3]={signX*magnitude,signY*magnitude,signZ*magnitude};
            const int axis=face/2;
            components[axis]+=(face&1)?.5f:-.5f;
            components[(axis+1)%3]+=u?.5f:-.5f;
            components[(axis+2)%3]+=v?.5f:-.5f;
            const double depth=cornerDepth({components[0],components[1],components[2]},
                                          q,density,scale,offset);
            require(std::isfinite(depth)&&depth>-49150 && depth<65535,
                    "admitted emitted corner lies outside unclamped clip interval");
            const float faceDepth=float(depth-offset);
            for(float w:{.5f,1.f,2.f})
                require(std::abs(vertexDepth(faceDepth,offset,w)-(depth+65535)/131070)<2e-7,
                        "vertex clip depth matches affine fragment depth");
            ++corners;
        }
    }
    require(admitted>100 && rejected>100,"routing fixture exercises both draws");
    metadataReset();
    metadataEmit({-3,7,-11},0);
    metadataEmit({2,-1,4},2);
    metadataEmit({-100,1,0},1);
    metadataEmit({1,2,3},0);
    require(metadataMagnitude()==floatBitsToUint(100) && metadataPriority()==2 &&
            metadataInvalid()==0,"emitted metadata aggregates maxima");
    for(int axis=0;axis<3;++axis)for(float bad:{infinity,-infinity,nan}){
        metadataReset();
        vec3 p{1,2,3};if(axis==0)p.x=bad;else if(axis==1)p.y=bad;else p.z=bad;
        metadataEmit(p,1);metadataEmit({4,5,6},0);
        require(metadataInvalid()==1 && metadataPriority()==1,
                "nonfinite emission poisons the entire canvas persistently");
        require(!route(metadataMagnitude(),{0,0,0,1},1,1,0,
                       metadataPriority(),metadataInvalid()),"poisoned canvas selects fallback");
    }
    metadataReset();metadataEmit({-0.f,0,0},0);
    require(metadataMagnitude()==0,"signed zero magnitude is nonnegative");
    metadataEmit({1024,0,0},3);metadataReset();metadataEmit({1,0,0},0);
    require(metadataMagnitude()==floatBitsToUint(1) && metadataPriority()==0 &&
            metadataInvalid()==0,"fresh metadata cannot retain a previous canvas");
    checkHeaderReset();
    std::cout<<admitted<<" admitted canvases; "<<rejected<<" fallback canvases; "
             <<corners<<" independently transformed corners; metadata/reset controls passed\n";
}
"""


def host_source(suffix, directory):
    base = SHADERS / directory
    common = (base / f"ir_iso_common.{suffix}").read_text()
    helper = (base / f"ir_source_face_depth.{suffix}").read_text()
    constants = "\n".join(re.search(
        rf"(?:const|constant) (?:int|uint) {name} = [^;]+;", common).group()
        for name in ("kDepthEncodeShift", "kDepthForegroundBandWidth",
                     "kEntityIdPriorityShiftInHighWord"))
    producer = (base / f"c_voxel_to_trixel_stage_2_body.{suffix}").read_text()
    start = producer.index("if (sourceFacePositionFinite(voxelPosition.xyz))")
    end = producer.index("sourceFaces[index].centerAndFace" if suffix == "glsl"
                         else "sourceFaces.faces[index].centerAndFace", start)
    metadata = producer[start:end]
    metadata = re.sub(r"//[^\n]*", "", metadata)
    fields = ("uint sourceMaxAbsPositionBits=0,sourceMaxPriority=0,sourceDepthInvalid=0;"
              if suffix == "glsl" else
              "struct {uint maxAbsPositionBits=0,maxPriority=0,depthInvalid=0;} sourceFaces;")
    names = (("sourceMaxAbsPositionBits", "sourceMaxPriority", "sourceDepthInvalid")
             if suffix == "glsl" else
             ("sourceFaces.maxAbsPositionBits", "sourceFaces.maxPriority",
              "sourceFaces.depthInvalid"))
    adapter = fields + "\nvoid metadataEmit(vec3 position,uint priority){\n"
    adapter += ("const vec4 voxelPosition{position.x,position.y,position.z,0};\n"
                "const uvec2 packedEntityId{7u,priority<<30u};\n" + metadata + "}\n")
    adapter += "void metadataReset(){" + "=".join(names) + "=0;}\n"
    for method, name in zip(("Magnitude", "Priority", "Invalid"), names):
        adapter += f"uint metadata{method}(){{return {name};}}\n"
    types = (ROOT / "engine/render/include/irreden/render/ir_render_types.hpp").read_text()
    header = extract_block(types, "struct SourceVoxelFaceHeader") + ";\n"
    system = (ROOT / "engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp").read_text()
    reset = re.search(r"const SourceVoxelFaceHeader drawArgs\{\};\s*"
                      r"triangleCanvasTextures.sourceFaces_.*?;\s*"
                      r"triangleCanvasTextures.sourceFaceOrder_.*?;", system, re.DOTALL).group()
    adapter += header + r"""
static_assert(sizeof(SourceVoxelFaceHeader)==32);
static_assert(offsetof(SourceVoxelFaceHeader,maxAbsPositionBits_)==20);
static_assert(offsetof(SourceVoxelFaceHeader,maxPriority_)==24);
static_assert(offsetof(SourceVoxelFaceHeader,depthInvalid_)==28);
struct Buffer {
    std::array<uint,8> words;
    void subData(std::size_t offset,std::size_t size,const void* data){
        require(offset+size<=32,"header reset stays within allocation");
        std::memcpy(reinterpret_cast<char*>(words.data())+offset,data,size);
    }
};
void checkHeaderReset(){
    Buffer faces,order;faces.words.fill(0xababababu);order.words.fill(0xababababu);
    struct {std::pair<int,Buffer*> sourceFaces_,sourceFaceOrder_;} triangleCanvasTextures;
    triangleCanvasTextures.sourceFaces_={0,&faces};triangleCanvasTextures.sourceFaceOrder_={0,&order};
""" + reset + r"""
    for(const Buffer* buffer:{&faces,&order})for(uint i=0;i<8;++i)
        require(buffer->words[i]==(i==0?6u:0u),
                "full header reset preserves draw ABI and clears metadata");
}
"""
    vertex = (base / ("ir_source_face_scatter_vertex.glsl" if suffix == "glsl" else
                      "ir_source_face_scatter_body.metal")).read_text()
    start = vertex.index("    const bool earlyDepth =")
    gate = vertex[start:vertex.index("    const SourceVoxelFace face =", start)]
    gate = re.sub(r"(?:gl_Position|out.position) = [^;]+;", "", gate)
    gate = gate.replace("return out;", "return false;").replace("return;", "return false;")
    gate = gate.replace("IR_SOURCE_FACE_EARLY_DEPTH", "pass")
    context = r"""
    const int kMinTriangleDistance=-65535,kMaxTriangleDistance=65535;
    const vec4 _detachedResidualPad=q;
    const vec2 effectiveSubdivisionsForHover{density,scale};
    const int distanceOffset=offset,depthPriorityMode=mode;
    const uint sourceMaxAbsPositionBits=magnitude,sourceMaxPriority=priority;
    const uint sourceDepthInvalid=invalid;
    struct {uint maxAbsPositionBits,maxPriority,depthInvalid;}
        sourceFaces{magnitude,priority,invalid};
    struct {vec4 _detachedResidualPad; vec2 effectiveSubdivisionsForHover;
        int distanceOffset,depthPriorityMode;} frameData{q,{density,scale},offset,mode};
    struct {int kMinTriangleDistance,kMaxTriangleDistance;} globals{-65535,65535};
"""
    adapter += ("bool vertexAccept(uint magnitude,vec4 q,float density,float scale,int offset,"
                "uint priority,uint invalid,int mode,int pass){\n" + context + gate +
                "\nreturn true;\n}\n")
    assignment = re.search(r"#if IR_SOURCE_FACE_EARLY_DEPTH\n(.*?)#endif", vertex, re.DOTALL)[1]
    adapter += r"""
float vertexDepth(float faceDepth,int offset,float w){
    const int kMinTriangleDistance=-65535,kMaxTriangleDistance=65535,distanceOffset=offset;
    vec4 gl_Position{0,0,0,w};
    struct {vec4 position;float depth;} out{{0,0,0,w},faceDepth};
    struct {int distanceOffset;} frameData{offset};
    struct {int kMinTriangleDistance,kMaxTriangleDistance;} globals{-65535,65535};
""" + assignment
    adapter += ("return (gl_Position.z/gl_Position.w+1)*.5f;\n}\n" if suffix == "glsl" else
                "return out.position.z/out.position.w;\n}\n")
    header_sources = [common] if suffix == "metal" else [
        producer, vertex, (base / "c_lighting_to_trixel_body.glsl").read_text()]
    checks = ""
    for index, text in enumerate(header_sources):
        declaration = text[text.index("SourceVoxelFaces {") + len("SourceVoxelFaces {"):]
        declaration = declaration[:declaration.index("SourceVoxelFace ")]
        name = f"ShaderHeader{index}"
        adapter += f"struct {name} {{{declaration}}};\n"
        fields = (("sourceMaxAbsPositionBits", "sourceMaxPriority", "sourceDepthInvalid")
                  if suffix == "glsl" else
                  ("maxAbsPositionBits", "maxPriority", "depthInvalid"))
        checks += f'require(sizeof({name})==32,"shader header size matches indirect ABI");\n'
        for field, offset in zip(fields, (20, 24, 28)):
            checks += (f'require(offsetof({name},{field})=={offset},'
                       '"shader metadata offsets match CPU header");\n')
    adapter += "void checkShaderHeaders(){\n" + checks + "}\n"
    cases = CASES.replace("    checkHeaderReset();", "    checkHeaderReset();checkShaderHeaders();")
    code = "\n".join((PREAMBLE, constants, extract_function(common, "decodePriority"),
                      helper, adapter, cases))
    return (code.replace("constant ", "const ").replace("float3", "vec3")
            .replace("float4", "vec4").replace("uint2", "uvec2").replace(".xyz", ".xyz()"))


@unittest.skipUnless(COMPILER, "early-depth routing controls require a C++ compiler")
class SourceFaceEarlyDepthTest(unittest.TestCase):
    def preprocess_fragment(self, suffix, directory, early, inverted=False):
        name = ("f_source_face_scatter" if suffix == "glsl" else "source_face_scatter")
        wrapper = (SHADERS / directory / f"{name}{'_early' if early else ''}.{suffix}").read_text()
        body_name = ("ir_source_face_scatter_fragment.glsl" if suffix == "glsl" else
                     "ir_source_face_scatter_body.metal")
        body = (SHADERS / directory / body_name).read_text()
        if inverted:
            self.assertIn("#if !IR_SOURCE_FACE_EARLY_DEPTH", body)
            body = body.replace("#if !IR_SOURCE_FACE_EARLY_DEPTH", "#if IR_SOURCE_FACE_EARLY_DEPTH")
        source = wrapper.replace(f'#include "{body_name}"', body)
        source = re.sub(r"^#(?:include|version)[^\n]*", "", source, flags=re.MULTILINE)
        run = subprocess.run([COMPILER, "-E", "-P", "-x", "c++", "-"], input=source,
                             capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        return run.stdout

    def test_specializations_keep_queries_and_omit_only_early_fragment_depth(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            for early in (False, True):
                for inverted in (False, True):
                    with self.subTest(backend=suffix, early=early, inverted=inverted):
                        shader = self.preprocess_fragment(suffix, directory, early, inverted)
                        if suffix == "metal":
                            marker = "fragment SourceFaceFragment"
                            shader = shader[shader.index(marker):]
                        depth_write = "gl_FragDepth" if suffix == "glsl" else "out.depth"
                        has_fragment_depth = depth_write in shader
                        self.assertEqual(has_fragment_depth, early if inverted else not early)
                        self.assertIn("worldSurfaceSunShadowFactor", shader)
                        self.assertIn("sourceFaceLitColor", shader)

    def compile_and_run(self, source):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            cpp, exe = path / "routing.cpp", path / "routing"
            cpp.write_text(source)
            build = subprocess.run([COMPILER, "-std=c++17", "-O2", str(cpp), "-o", str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            return subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)

    def test_production_routing_metadata_and_reset(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            with self.subTest(backend=suffix):
                run = self.compile_and_run(host_source(suffix, directory))
                self.assertEqual(run.returncode, 0, run.stderr)
                print(f"{suffix}: {run.stdout}", end="")

    def test_mutations(self):
        mutations = {
            "drop_priority": ("maxPriority != 0u", "false"),
            "drop_invalid": ("invalid != 0u", "false"),
            "drop_mode": ("depthPriorityMode != 0", "false"),
            "ignore_nonfinite_coordinate": ("sourceFaceFinite(position.z)", "true"),
            "drop_positive_scale": ("depthScale <= 0.0", "false"),
            "drop_lower_clamp": ("low > float(minDepth + kDepthForegroundBandWidth + 1) + 4.0",
                                 "true"),
            "drop_upper_clip": ("high < float(maxDepth) - 4.0", "true"),
            "upper_boundary_inclusive": ("high < float(maxDepth)", "high <= float(maxDepth)"),
            "reset_only_indirect": ("sizeof(drawArgs)", "20u"),
            "drop_rotation_enclosure": ("(1.0 + 2.0 * sum * (sum + abs(rotation.w)))", "1.0"),
            "both_draws": ("if (earlyDepth != (pass != 0))", "if (false)"),
            "drop_emitted_radius": ("sourceFacePositionMagnitudeBits(voxelPosition.xyz())", "0u"),
            "drop_emitted_priority": ("decodePriority(packedEntityId)", "0u"),
        }
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            production = host_source(suffix, directory)
            for name, (before, after) in mutations.items():
                with self.subTest(backend=suffix, mutation=name):
                    self.assertIn(before, production)
                    run = self.compile_and_run(production.replace(before, after))
                    self.assertNotEqual(run.returncode, 0, "mutation escaped routing controls")
            padding = "sourcePadding[3]" if suffix == "glsl" else "padding[3]"
            with self.subTest(backend=suffix, mutation="stale_shader_header_layout"):
                self.assertIn(padding, production)
                run = self.compile_and_run(production.replace(padding, padding.replace("3", "6")))
                self.assertNotEqual(run.returncode, 0)
                self.assertIn("shader header size matches indirect ABI", run.stderr)
            invalid_write = ("atomicOr(sourceDepthInvalid, 1u);" if suffix == "glsl" else
                             "atomic_fetch_or_explicit(&sourceFaces.depthInvalid, 1u, "
                             "memory_order_relaxed);")
            with self.subTest(backend=suffix, mutation="drop_invalid_metadata_write"):
                self.assertIn(invalid_write, production)
                run = self.compile_and_run(production.replace(invalid_write, ""))
                self.assertNotEqual(run.returncode, 0)
                self.assertIn("nonfinite emission poisons", run.stderr)


if __name__ == "__main__":
    unittest.main()

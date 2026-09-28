"""Execute the sun-shadow wrappers' receiver positions and PCF-layer offset."""

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

PREAMBLE = r"""
#include <cmath>
#include <cstdio>
#include <tuple>
#define constant
#define device
typedef unsigned uint;
struct vec2 {float x,y; vec2(float a=0,float b=0):x(a),y(b){}};
struct vec3 {
 float x,y,z; vec3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}
 vec2 xy()const{return {x,y};}
};
struct vec4 {
 float x,y,z,w; vec4(float a=0,float b=0,float c=0,float d=0):x(a),y(b),z(c),w(d){}
 vec3 xyz()const{return {x,y,z};}
};
vec3 operator+(vec3 a,vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
vec3 operator*(vec3 a,float s){return {a.x*s,a.y*s,a.z*s};}
float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float mix(float a,float b,float t){return a+(b-a)*t;}
float smoothstep(float e0,float e1,float x){
 float t=std::fmin(std::fmax((x-e0)/(e1-e0),0.f),1.f);return t*t*(3-2*t);
}
const int kCascadeTexelCount=1024*1024;
bool nearInteriorFlag=true;
bool sunCascadeKernelInterior(vec2,vec2,vec2){return nearInteriorFlag;}
struct Call {vec2 uv; float z; int bufferOffset; vec3 pcf;};
Call calls[4]; int callCount=0;
// Records what each cascade tap receives; the coverage-layer offset is the
// last argument on both backends.
template<class... Rest>
float sampleCascadeShadow(vec2 uv,float z,vec3,vec3,vec3,vec3,vec2,vec2,int bufferOffset,
                          Rest... rest){
 if(callCount<4)calls[callCount]={uv,z,bufferOffset,
                                   std::get<sizeof...(Rest)-1>(std::make_tuple(rest...))};
 ++callCount; return 0.f;
}
struct FrameDataSun {
 vec4 sunDirection,sunBasisU,sunBasisV;
 vec2 sunBufferOriginUV,sunBufferTexelSize,cascadeOriginUV_0,cascadeTexelSize_0,
      cascadeOriginUV_1,cascadeTexelSize_1;
 float cascadeSplitDepth=0,sunMaxShadowThrow=64; int cascadeCount=2;
} frame;
"""

GLSL_FRAME = r"""
#define sunDirection frame.sunDirection
#define sunBasisU frame.sunBasisU
#define sunBasisV frame.sunBasisV
#define sunBufferOriginUV frame.sunBufferOriginUV
#define sunBufferTexelSize frame.sunBufferTexelSize
#define cascadeOriginUV_0 frame.cascadeOriginUV_0
#define cascadeTexelSize_0 frame.cascadeTexelSize_0
#define cascadeOriginUV_1 frame.cascadeOriginUV_1
#define cascadeTexelSize_1 frame.cascadeTexelSize_1
#define cascadeSplitDepth frame.cascadeSplitDepth
#define sunMaxShadowThrow frame.sunMaxShadowThrow
#define cascadeCount frame.cascadeCount
"""

GLSL_CALLS = "\n" + "".join(
    f"#undef {line.split()[1]}\n" for line in GLSL_FRAME.strip().splitlines()) + r"""
float receiverCall(int kind,vec3 p,vec3 n,float depth){
 const vec4 caster(0,0,0,1);
 if(kind==0)return worldSunShadowFactor(p,n,depth);
 if(kind==1)return worldSurfaceSunShadowFactor(p,n,depth,caster);
 return worldShapeSurfaceSunShadowFactor(p,n,depth,caster);
}
"""

METAL_CALLS = r"""
const uint depthBuf[1]={0};
float receiverCall(int kind,vec3 p,vec3 n,float depth){
 const vec4 caster(0,0,0,1);
 if(kind==0)return worldSunShadowFactor(p,n,depth,frame,depthBuf);
 if(kind==1)return worldSurfaceSunShadowFactor(p,n,depth,caster,frame,depthBuf);
 return worldShapeSurfaceSunShadowFactor(p,n,depth,caster,frame,depthBuf);
}
"""

# Every tap of every cascade path must query the source-face and nearest-tap
# layers at the receiver's own position. The raster receiver's own position is
# pos + normal * kNormalBiasVoxels; the finite shape fragment's is exact, but
# its coverage taps must land where the raster receiver's do (the canvas splats
# of the shape's own cells are calibrated against that offset). The exact
# surface receiver takes no offset at all.
CHECKS = r"""
bool near(float a,float b){return std::fabs(a-b)<=1e-4f*(1.f+std::fabs(b));}
bool sameTap(vec2 uv,float z,vec3 e){return near(uv.x,e.x)&&near(uv.y,e.y)&&near(z,e.z);}
vec3 cross(vec3 a,vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
vec3 unit(vec3 a){return a*(1.f/std::sqrt(dot(a,a)));}
int main(){
 int checks=0,failures=0,perKind[3]={0,0,0};
 const vec3 suns[]={{.3f,.5f,-.81f},{-.6f,.2f,-.77f},{0.f,0.f,-1.f}};
 const vec3 normals[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
 const vec3 points[]={{0,0,0},{10.25f,-20.5f,3.75f},{-7.5f,4.f,-12.f}};
 for(vec3 sunRaw:suns) for(int count:{1,2}) for(int interior:{0,1})
 for(float offset:{-24.f,0.f,3.f,24.f}) for(vec3 n:normals) for(vec3 p:points)
 for(int kind=0;kind<3;++kind){
  const vec3 sun=unit(sunRaw);
  const vec3 u=unit(cross(sun,std::fabs(sun.z)>.9f?vec3(1,0,0):vec3(0,0,1)));
  const vec3 v=cross(u,sun);
  frame.sunDirection=vec4(sun.x,sun.y,sun.z,0);
  frame.sunBasisU=vec4(u.x,u.y,u.z,0); frame.sunBasisV=vec4(v.x,v.y,v.z,0);
  frame.cascadeCount=count; frame.cascadeSplitDepth=-51.2f; nearInteriorFlag=interior!=0;
  const vec3 exact=sunSpaceProject(p,u,v,sun);
  const vec3 raster=sunSpaceProject(p+n*kNormalBiasVoxels,u,v,sun);
  callCount=0;
  receiverCall(kind,p,n,frame.cascadeSplitDepth+offset);
  bool ok=callCount>=1&&callCount<=2;
  for(int c=0;c<callCount&&c<4;++c){
   const Call& t=calls[c];
   const vec2 tapUV(t.uv.x+t.pcf.x,t.uv.y+t.pcf.y);
   const float tapZ=t.z+t.pcf.z;
   const bool unbiased=near(t.pcf.x,0)&&near(t.pcf.y,0)&&near(t.pcf.z,0);
   if(kind==0) ok=ok&&sameTap(t.uv,t.z,raster)&&unbiased;
   if(kind==1) ok=ok&&sameTap(t.uv,t.z,exact)&&unbiased;
   if(kind==2) ok=ok&&sameTap(t.uv,t.z,exact)&&sameTap(tapUV,tapZ,raster);
  }
  ++checks;
  if(!ok){
   ++perKind[kind];
   if(++failures<=4)printf("fail kind=%d count=%d interior=%d offset=%g calls=%d\n",
                           kind,count,interior,offset,callCount);
  }
 }
 printf("kind_failures=%d,%d,%d\n",perKind[0],perKind[1],perKind[2]);
 printf("checks=%d failures=%d\n",checks,failures);
 return failures?1:0;
}
"""

WRAPPERS = ("worldSunShadowFactor", "worldSurfaceSunShadowFactor",
            "worldShapeSurfaceSunShadowFactor")


def _replace_last(text, old, new):
    head, sep, tail = text.rpartition(old)
    return head + new + tail if sep else text


def _wrapper_mutation(functions, wrapper, old, new):
    body = extract_function(functions, wrapper)
    return functions.replace(body, body.replace(old, new))


@unittest.skipUnless(COMPILER, "pcf bias checks require a C++ compiler")
class ShapePcfBiasTest(unittest.TestCase):
    def test_wrappers_query_their_own_receiver_and_offset_only_the_pcf_layer(self):
        for suffix, folder, frame, calls in (("glsl", "", GLSL_FRAME, GLSL_CALLS),
                                             ("metal", "metal/", "", METAL_CALLS)):
            shaders = SHADERS / folder
            projection = (shaders / f"ir_sun_projection.{suffix}").read_text()
            sample = (shaders / f"ir_sun_shadow_sample.{suffix}").read_text()
            constants = "".join(
                f"const float {name} = "
                + re.search(rf"{name} = ([0-9.]+);", sample).group(1) + ";\n"
                for name in ("kNormalBiasVoxels", "kCascadeBlendRange"))
            impl = extract_function(sample, "worldSunShadowFactorImpl")
            functions = "\n".join(
                [extract_function(projection, "sunSpaceProject"), impl]
                + [extract_function(sample, name) for name in WRAPPERS])
            variants = {
                "production": functions,
                "shape_unbiased": _wrapper_mutation(
                    functions, "worldShapeSurfaceSunShadowFactor",
                    "casterViewToWorld, kNormalBiasVoxels)", "casterViewToWorld, 0.0)"),
                "surface_biased": _wrapper_mutation(
                    functions, "worldSurfaceSunShadowFactor",
                    "casterViewToWorld, 0.0)", "casterViewToWorld, kNormalBiasVoxels)"),
                "blend_far_tap_unbiased": functions.replace(
                    impl, _replace_last(impl, "pcfOffset\n", "pcfOffset * 0.0\n")),
                "offset_moves_the_query": functions.replace(
                    "pos3D, uHat, vHat, sunDir",
                    "pos3D + normal * pcfNormalBias, uHat, vHat, sunDir"),
            }
            for name, candidate in variants.items():
                with (self.subTest(backend=suffix, variant=name),
                      tempfile.TemporaryDirectory() as tmp):
                    if name != "production":
                        self.assertNotEqual(candidate, functions)
                    code = PREAMBLE + frame + constants + candidate + calls + CHECKS
                    code = re.sub(r"\.xyz\b", ".xyz()", code)
                    code = re.sub(r"\.xy\b", ".xy()", code)
                    for source, target in (("float2", "vec2"), ("float3", "vec3"),
                                           ("float4", "vec4"), ("1.0f - ", "1.0 - ")):
                        code = code.replace(source, target)
                    cpp, exe = Path(tmp) / "pcf.cpp", Path(tmp) / "pcf"
                    cpp.write_text(code)
                    build = subprocess.run([COMPILER, "-std=c++17", str(cpp), "-o", str(exe)],
                                           capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(exe)], capture_output=True, text=True)
                    if name == "production":
                        self.assertEqual(run.returncode, 0, run.stdout)
                        print(suffix, run.stdout.strip().splitlines()[-1])
                    else:
                        self.assertEqual(run.returncode, 1, run.stdout)

    def test_offset_applies_after_the_exact_queries(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            sample = (SHADERS / folder / f"ir_sun_shadow_sample.{suffix}").read_text()
            body = extract_function(sample, "sampleCascadeShadow")
            body = body[body.index("{"):]
            moved = body.replace("    sunUV += pcfOffset.xy;\n", "").replace(
                "{", "{\n    sunUV += pcfOffset.xy;\n", 1)
            for name, candidate in (("production", body), ("offset_first", moved)):
                with self.subTest(backend=suffix, variant=name):
                    applied = candidate.index("sunUV += pcfOffset.xy")
                    ordered = ("pcfOffset" not in candidate[:applied]
                               and "sourceFaceRaySeparation" not in candidate[applied:]
                               and "nearestPixel" not in candidate[applied:]
                               and candidate.index("sunPxF") > applied)
                    self.assertEqual(ordered, name == "production")


if __name__ == "__main__":
    unittest.main()

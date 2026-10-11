"""Execute production cascade routing, sampling order and visibility blending."""

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

HARNESS = r"""
#include <cmath>
#include <cstdio>
#include <initializer_list>
#define constant
#define device
using uint=unsigned;
struct vec2 {float x,y;};
struct vec3 {float x,y,z;vec3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}
 vec2 xy()const{return {x,y};}};
struct vec4 {float x,y,z,w;vec3 xyz()const{return {x,y,z};}};
vec3 operator*(vec3 a,float b){return {a.x*b,a.y*b,a.z*b};}
float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float mix(float a,float b,float t){return a*(1.f-t)+b*t;}
float smoothstep(float a,float b,float v){
 float t=std::fmin(std::fmax((v-a)/(b-a),0.f),1.f);return t*t*(3.f-2.f*t);
}
const int kCascadeTexelCount=1024*1024;
struct FrameDataSun {
 vec4 sunDirection{0,0,1,0},sunBasisU{1,0,0,0},sunBasisV{0,1,0,0};
 vec2 sunBufferOriginUV{1,2},sunBufferTexelSize{3,4};
 vec2 cascadeOriginUV_0{5,6},cascadeTexelSize_0{7,8};
 vec2 cascadeOriginUV_1{9,10},cascadeTexelSize_1{11,12};
 float sunMaxShadowThrow=64,cascadeSplitDepth=32;int cascadeCount=2;
} frame;
bool interior=true;
int interiorQueries=0;
bool sunCascadeKernelInterior(vec2,vec2,vec2){++interiorQueries;return interior;}
struct Call {vec2 origin,texel;int offset;bool surface;vec3 pcf;};
Call calls[4];int callCount=0;
float sampleCascadeShadow(vec2,float,vec3,vec3,vec3,vec3,vec2 origin,vec2 texel,
                         int offset,float,bool surface,vec4,vec3 pcf){
 if(callCount<4)calls[callCount]={origin,texel,offset,surface,pcf};
 ++callCount;return origin.x==1?.25f:(offset==0?.125f:.875f);
}
float sampleCascadeShadow(vec2 uv,float z,vec3 normal,vec3 sun,vec3 u,vec3 v,
                         vec2 origin,vec2 texel,int offset,const uint*,float distance,
                         bool surface,vec4 rotation,vec3 pcf){
 return sampleCascadeShadow(uv,z,normal,sun,u,v,origin,texel,offset,distance,surface,rotation,pcf);
}
"""

CHECKS = r"""
bool same(vec2 a,vec2 b){return a.x==b.x&&a.y==b.y;}
int main(){
 int checks=0;
 for(int count:{0,1,2,3})for(bool nearInterior:{false,true})
 for(float distance:{-20.f,-8.001f,-8.f,-7.999f,-4.f,0.f,4.f,7.999f,8.f,8.001f,20.f})
 for(bool surface:{false,true})for(float bias:{0.f,.5f}){
  frame.cascadeCount=count;interior=nearInterior;callCount=interiorQueries=0;
  const vec3 position{2,3,4},normal{0,0,1};const vec4 rotation{0,0,0,1};
  const uint buffer[1]={0};(void)buffer;
  const float actual=QUERY;
  int expectedCalls=1,first=0;float expected=.75f;
  if(count>1){
   if(!nearInterior||distance>8.f){first=1;expected=.125f;}
   else if(distance < -8.f){expected=.875f;}
   else {
    expectedCalls=2;
    const double t=(double(distance)+8.0)/16.0;
    const double weight=t*t*(3.0-2.0*t);
    expected=float(1.0-(.125*(1.0-weight)+.875*weight));
   }
  }
  if(callCount!=expectedCalls||interiorQueries!=(count>1)||
     !std::isfinite(actual)||std::abs(actual-expected)>2e-7f)
   return 1;
  for(int i=0;i<callCount;++i){
   const int cascade=first+i;
   const vec2 origin=count<=1?vec2{1,2}:(cascade==0?vec2{5,6}:vec2{9,10});
   const vec2 texel=count<=1?vec2{3,4}:(cascade==0?vec2{7,8}:vec2{11,12});
   const auto& call=calls[i];
   if(!same(call.origin,origin)||!same(call.texel,texel)||
      call.offset!=cascade*kCascadeTexelCount||call.surface!=surface||
      call.pcf.x!=0||call.pcf.y!=0||call.pcf.z!=-bias)return 2;
  }
  ++checks;
 }
 printf("checks=%d\n",checks);return 0;
}
"""


@unittest.skipUnless(COMPILER, "cascade routing controls require a C++ compiler")
class SunCascadeRoutingTest(unittest.TestCase):
    def test_cascade_routes_and_blend_match_independent_oracle(self):
        fields = ("sunDirection", "sunBasisU", "sunBasisV", "sunBufferOriginUV",
                  "sunBufferTexelSize", "cascadeOriginUV_0", "cascadeTexelSize_0",
                  "cascadeOriginUV_1", "cascadeTexelSize_1", "sunMaxShadowThrow",
                  "cascadeSplitDepth", "cascadeCount")
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            shaders = SHADERS / folder
            source = (shaders / f"ir_sun_shadow_sample.{suffix}").read_text()
            body = extract_function(source, "worldSunShadowFactorImpl")
            self.assertEqual(body.count("sampleCascadeShadow("), 1)
            projection = extract_function(
                (shaders / f"ir_sun_projection.{suffix}").read_text(), "sunSpaceProject")
            constant = re.search(r"kCascadeBlendRange = ([^;]+);", source).group(1)
            preamble = HARNESS + f"\nconst float kCascadeBlendRange={constant};\n"
            frame_macros = "".join(f"#define {field} frame.{field}\n" for field in fields)
            undef_macros = "".join(f"#undef {field}\n" for field in fields)
            query = ("worldSunShadowFactorImpl(position,normal,frame.cascadeSplitDepth+distance,"
                     + ("frame,buffer," if suffix == "metal" else "")
                     + "surface,rotation,bias)")
            variants = {
                "production": body,
                "nonfinite_visibility": re.sub(r"return 1\.0f? - shadowAccum;", "return NAN;", body),
                "lost_covering_cascade": body.replace("!nearInterior || ", ""),
                "lost_blend_sample": body.replace("cascadeSamples = 2", "cascadeSamples = 1"),
                "wrong_far_offset": body.replace("cascade * kCascadeTexelCount", "0"),
                "reversed_blend": body.replace("mix(shadowAccum, shadow, blend)",
                                               "mix(shadow, shadowAccum, blend)"),
                "near_boundary_single": body.replace("distToSplit < -", "distToSplit <= -"),
                "far_boundary_single": body.replace("distToSplit > k", "distToSplit >= k"),
                "linear_blend": body.replace(
                    "smoothstep(-kCascadeBlendRange, kCascadeBlendRange, distToSplit)",
                    "(distToSplit + kCascadeBlendRange) / (2.0 * kCascadeBlendRange)"),
            }
            for name, candidate in variants.items():
                with (self.subTest(backend=suffix, variant=name),
                      tempfile.TemporaryDirectory() as tmp):
                    if name != "production":
                        self.assertNotEqual(candidate, body)
                    code = preamble + projection + "\n"
                    if suffix == "glsl":
                        code += frame_macros + candidate + "\n" + undef_macros
                    else:
                        code += candidate
                    code += CHECKS.replace("QUERY", query)
                    code = re.sub(r"\.xyz\b", ".xyz()", code)
                    code = re.sub(r"\.xy\b", ".xy()", code)
                    for size in (2, 3, 4):
                        code = code.replace(f"float{size}", f"vec{size}")
                    cpp, exe = Path(tmp) / "cascade.cpp", Path(tmp) / "cascade"
                    cpp.write_text(code)
                    build = subprocess.run([COMPILER, "-std=c++17", str(cpp), "-o", str(exe)],
                                           capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(exe)], capture_output=True, text=True)
                    if name == "production":
                        self.assertEqual(run.returncode, 0, run.stdout)
                        print(suffix, run.stdout.strip())
                    else:
                        self.assertIn(run.returncode, (1, 2), run.stdout)


if __name__ == "__main__":
    unittest.main()

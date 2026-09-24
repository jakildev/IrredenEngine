"""Execute finite-fragment lighting with independent composition expectations."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_render_source_face_lighting import COMPILER, PREAMBLE, SHADERS

ADAPTERS = r"""
#include <cstdlib>
#include <iostream>
vec3& operator+=(vec3& a,vec3 b){a=a+b;return a;}
float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
struct ivec2 {int x,y;};
struct Shape {uint color,flags;};
struct Tile {int shapeIndex;};
uint receiverOwners[8]{0,0,0,0,0,0,0,0};
Tile receiverTiles[1]{{1}};
Shape receiverShapes[2]{{99,0},{7,0}};
constexpr uint kShapeSamplesPerTile=384,kShapeProceduralColorFlags=32u|64u;
int lutEnabled=0,shadowsEnabled=1,lightVolumeEnabled=0,hdrEnabled=0;
float aoInput=.4f,visibilityInput=0,sunAmbient=.25f,sunIntensity=1.5f,normalZ=-1;
float exposure=2,skyIntensity=.7f;
vec3 sunDirection{0,0,-1},skyColor{.2f,.4f,.6f};
int shadowCalls=0,lightCalls=0,aoCalls=0;
vec4 unpackColor(uint c){if(c!=7)std::exit(20);return {.2f,.3f,.4f,1};}
float sampleAO(ivec2 p){++aoCalls;if(p.x!=1||p.y!=1)std::exit(21);return aoInput;}
vec3 samplePalette(float ao,float l){
 if(!near(ao,aoInput)||!near(l,.2815f))std::exit(22);
 return {.7f,.5f,.3f};
}
float pos3DtoDistance(vec3 p){return p.x+p.y+p.z;}
float worldSurfaceSunShadowFactor(vec3 p,vec3 n,float d,vec4 r){
 ++shadowCalls;
 if(!near(p.x,10.25f)||!near(p.y,-20.5f)||!near(p.z,30.75f)||
    !near(n.z,normalZ)||!near(d,20.5f)||!near(r.w,.7f))std::exit(23);
 return visibilityInput;
}
vec3 localLight(vec3 p){++lightCalls;if(!near(p.x,10.25f))std::exit(24);return {.1f,.2f,.3f};}
"""

CASES = r"""
float expectedDisplay(float x,bool hdr){
 if(!hdr)return std::clamp(x,0.f,1.f);
 double e=double(x)*2.;
 return float(std::clamp((e*(2.51*e+.03))/(e*(2.43*e+.59)+.14),0.,1.));
}
int main(){
 int checks=0;
 for(int lut:{0,1})for(int shadow:{0,1})for(int volume:{0,1})for(int hdr:{0,1})
 for(float ao:{0.f,.4f,1.f})for(float ambient:{0.f,.25f,1.f})for(float vis:{0.f,1.f})
 for(uint flags:{0u,32u,64u})for(float z:{-1.f,-.6f,0.f,1.f}){
  lutEnabled=lut;shadowsEnabled=shadow;lightVolumeEnabled=volume;hdrEnabled=hdr;
  normalZ=z;
  aoInput=ao;sunAmbient=ambient;visibilityInput=vis;receiverShapes[1].flags=flags;
  shadowCalls=lightCalls=aoCalls=0;
  vec3 out=query({1,1},4,{10.25f,-20.5f,30.75f},{float(std::sqrt(1-z*z)),0,z},
                 {0,0,0,.7f},{.9f,.8f,.7f});
  if(flags){
   if(!same(vec4(out,1),{.9f,.8f,.7f,1})||aoCalls||shadowCalls||lightCalls)return 1;
  }else{
   float a[]={.2f,.3f,.4f},palette[]={.7f,.5f,.3f},sky[]={.2f,.4f,.6f};
   float actual[]={out.x,out.y,out.z};
   for(int i=0;i<3;++i){
    float material=a[i]*(lut?palette[i]:ao);
    float sunlight=material*(ambient+(1-ambient)*std::max(0.f,-z)*(shadow?vis:1))*1.5f;
    float local=volume?a[i]*float(i+1)*.1f:0;
    float skyTerm=hdr?sky[i]*.7f*ao*std::max(0.f,-z):0;
    if(!near(actual[i],expectedDisplay(sunlight+local+skyTerm,hdr)))return 2;
   }
   if(shadowCalls!=shadow||lightCalls!=volume||aoCalls!=1)return 3;
  }
  ++checks;
 }
 std::cout<<checks<<" finite lighting cases\n";
}
"""


@unittest.skipUnless(COMPILER, "finite lighting controls require a C++ compiler")
class ShapeSurfaceLightingTest(unittest.TestCase):
    def test_composition_and_mutations(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            base = SHADERS / folder
            source = (base / f"ir_shape_surface_lighting.{suffix}").read_text()
            start = source.index("{", source.index("shapeSurfaceLighting("))
            body = source[start + 1:source.rindex("}")]
            body = re.sub(r"constexpr sampler .*?;", "", body)
            body = body.replace("float3", "vec3").replace("float4", "vec4")
            body = body.replace("lighting.", "").replace("sun.", "")
            body = re.sub(r",\s*sunFrameData, sunDepthBuf", "", body)
            body = re.sub(r",\s*sun, sunDepthBuf", "", body)
            body = body.replace("texelFetch(surfaceAO, ownerPixel, 0).r", "sampleAO(ownerPixel)")
            body = body.replace("surfaceAO.read(ownerPixel).r", "sampleAO(ownerPixel)")
            body = body.replace("textureLod(paletteLUT, vec2(ao, luminance), 0.0).rgb",
                                "samplePalette(ao, luminance)")
            body = body.replace("paletteLUT.sample(paletteSampler, float2(ao, luminance), "
                                "level(0.0)).rgb", "samplePalette(ao, luminance)")
            body = re.sub(r"surfaceLightVolume\(position,.*?\)", "localLight(position)",
                          body, flags=re.S)
            body = body.replace(".xyz", "").replace("skyColor.rgb", "skyColor")
            body = body.replace(".rgb", ".rgb()")
            tone = (base / f"ir_tonemap.{suffix}").read_text()
            surface = (base / f"ir_surface_lighting.{suffix}").read_text()
            helpers = tone + surface.replace(f'#include "ir_tonemap.{suffix}"', "")
            helpers = helpers.replace("float3", "vec3").replace("float4", "vec4")
            variants = {
                "production": body,
                "lost_material_owner": body.replace("receiverShapes[shapeIndex].color",
                                                    "receiverShapes[0].color"),
                "lost_procedural_fallback": body.replace("!= 0u", "== 999u"),
                "lost_shadow_toggle": body.replace("shadowsEnabled == 0", "false"),
                "ignored_lambert": body.replace("sunIntensity, lambert, visibility",
                                                "sunIntensity, 1.0, visibility"),
                "lost_local_light": body.replace("localLight(position)", "vec3(0.0)"),
                "wrong_sky_normal": body.replace("surfaceSkyLight(normal,",
                                                 "surfaceSkyLight(vec3(0,0,1),"),
                "tone_before_composition": body.replace("return surfaceDisplayColor(linearColor",
                                                        "return surfaceDisplayColor(material"),
            }
            for name, candidate in variants.items():
                with (self.subTest(backend=suffix, mutation=name),
                      tempfile.TemporaryDirectory() as tmp):
                    if name != "production":
                        self.assertNotEqual(candidate, body)
                    cpp, exe = Path(tmp) / "lighting.cpp", Path(tmp) / "lighting"
                    cpp.write_text(PREAMBLE + ADAPTERS + helpers +
                                   "vec3 query(ivec2 ownerPixel,int ownerWidth,vec3 position,"
                                   "vec3 normal,vec4 casterRotation,vec3 fallbackColor){" +
                                   candidate + "}" + CASES)
                    result = subprocess.run([COMPILER, "-std=c++17", str(cpp), "-o", str(exe)],
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    run = subprocess.run([str(exe)], capture_output=True, text=True)
                    if name == "production":
                        self.assertEqual(run.returncode, 0, run.stderr)
                        print(suffix, run.stdout.strip())
                    else:
                        self.assertNotEqual(run.returncode, 0)

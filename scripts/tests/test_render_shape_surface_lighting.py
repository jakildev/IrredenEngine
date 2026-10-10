"""Execute shape and per-axis lighting with their distinct shadow policies."""

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
int lutEnabled=0,shadowsEnabled=1,lightVolumeEnabled=0,hdrEnabled=0,debugOverlayMode=0;
float aoInput=.4f,visibilityInput=0,sunAmbient=.25f,sunIntensity=1.5f,normalZ=-1;
float exposure=2,skyIntensity=.7f;
vec3 sunDirection{0,0,-1},skyColor{.2f,.4f,.6f};
vec3 expectedLocalPosition{10.25f,-20.5f,30.75f};
int shapeShadowCalls=0,worldShadowCalls=0,lightCalls=0,aoCalls=0,paletteCalls=0,albedoCalls=0;
int paletteLUT=5;
vec4 unpackColor(uint c){++albedoCalls;if(c!=7)std::exit(20);return {.2f,.3f,.4f,1};}
float sampleAO(ivec2 p){++aoCalls;if(p.x!=1||p.y!=1)std::exit(21);return aoInput;}
vec3 samplePalette(int palette,float ao,float l){
 ++paletteCalls;
 if(palette!=5||!near(ao,aoInput)||!near(l,.2815f))std::exit(22);
 return {.7f,.5f,.3f};
}
float pos3DtoDistance(vec3 p){return p.x+p.y+p.z;}
float worldShapeSurfaceSunShadowFactor(vec3 p,vec3 n,float d,vec4 r){
 ++shapeShadowCalls;
 if(!near(p.x,10.25f)||!near(p.y,-20.5f)||!near(p.z,30.75f)||
    !near(n.z,normalZ)||!near(d,-81.f)||!near(r.w,.7f))std::exit(23);
 return visibilityInput;
}
float worldSurfaceSunShadowFactor(vec3 p,vec3 n,float d,vec4 r){
 ++worldShadowCalls;
 if(!near(p.x,10.25f)||!near(p.y,-20.5f)||!near(p.z,30.75f)||
    !near(n.z,normalZ)||!near(d,20.5f)||!near(r.w,.7f))std::exit(25);
 return visibilityInput;
}
vec3 localLight(vec3 p){
 ++lightCalls;
 if(!same(vec4(p,1),vec4(expectedLocalPosition,1)))std::exit(24);
 return {.1f,.2f,.3f};
}
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
 for(float ao:{0.f,.4f,1.f})
 for(float ambient:{0.f,.25f,std::nextafter(1.f,0.f),1.f})
 for(float intensity:{0.f,0x1p-20f,1.5f})for(float vis:{0.f,1.f})
 for(uint flags:{0u,32u,64u})
 for(float z:{-1.f,-.6f,-0x1p-20f,0.f,0x1p-20f,1.f}){
  lutEnabled=lut;shadowsEnabled=shadow;lightVolumeEnabled=volume;hdrEnabled=hdr;
  normalZ=z;
  aoInput=ao;sunAmbient=ambient;sunIntensity=intensity;
  visibilityInput=vis;receiverShapes[1].flags=flags;
  expectedLocalPosition=vec3(10.25f,-20.5f,30.75f);
  shapeShadowCalls=worldShadowCalls=lightCalls=aoCalls=paletteCalls=albedoCalls=0;
  vec3 out=query({1,1},4,{10.25f,-20.5f,30.75f},{float(std::sqrt(1-z*z)),0,z},
                 -81.f,{0,0,0,.7f},{.9f,.8f,.7f});
  if(flags){
   if(!same(vec4(out,1),{.9f,.8f,.7f,1})||aoCalls||shapeShadowCalls||worldShadowCalls||lightCalls||
      paletteCalls||albedoCalls)return 1;
  }else{
   float a[]={.2f,.3f,.4f},palette[]={.7f,.5f,.3f},sky[]={.2f,.4f,.6f};
   float actual[]={out.x,out.y,out.z};
   for(int i=0;i<3;++i){
    float material=a[i]*(lut?palette[i]:ao);
    float sunlight=material*(ambient+(1-ambient)*std::max(0.f,-z)*(shadow?vis:1))*intensity;
    float local=volume?a[i]*float(i+1)*.1f:0;
    float skyTerm=hdr?sky[i]*.7f*ao*std::max(0.f,-z):0;
    if(!near(actual[i],expectedDisplay(sunlight+local+skyTerm,hdr)))return 2;
   }
   const int expectedQueries=shadow&&z<0.f&&intensity!=0.f&&ambient!=1.f;
   if(shapeShadowCalls!=expectedQueries||worldShadowCalls||lightCalls!=volume||aoCalls!=1||
      paletteCalls!=lut||albedoCalls!=1)return 3;
   expectedLocalPosition=vec3(7.25f,-2.5f,4.75f);
   shapeShadowCalls=worldShadowCalls=lightCalls=paletteCalls=0;
   const vec3 separateLocal=worldSurfaceLighting(vec3(.2f,.3f,.4f),ao,
       vec3(10.25f,-20.5f,30.75f),vec3(float(std::sqrt(1-z*z)),0,z),
       vec4(0,0,0,.7f),expectedLocalPosition);
   if(!same(vec4(separateLocal,1),vec4(out,1))||shapeShadowCalls||
      worldShadowCalls!=expectedQueries||
      lightCalls!=volume||paletteCalls!=lut)return 4;
  }
  ++checks;
 }
 debugOverlayMode=8;
 for(uint flags:{0u,32u,64u})
 for(vec3 normal:{vec3(1,0,0),vec3(-1,0,0),vec3(0,1,0),vec3(0,-1,0),
                  vec3(0,0,1),vec3(0,0,-1),vec3(.6f,0,-.8f)}){
  receiverShapes[1].flags=flags;
  shapeShadowCalls=worldShadowCalls=lightCalls=aoCalls=paletteCalls=albedoCalls=0;
  const vec3 out=query({1,1},4,{10.25f,-20.5f,30.75f},normal,
                       -81.f,{0,0,0,.7f},{.9f,.8f,.7f});
  const vec3 expected=flags?vec3(.9f,.8f,.7f):normal*.5f+vec3(.5f);
  if(!same(vec4(out,1),vec4(expected,1)))return 5;
  if(shapeShadowCalls||worldShadowCalls||lightCalls||aoCalls||paletteCalls||albedoCalls)return 6;
 }
 std::cout<<checks<<" finite lighting cases and 21 normal controls\n";
}
"""


def function_body(source, name):
    match = re.search(r"\b" + name + r"\s*\([^{}]*\)\s*\{", source)
    if match is None:
        raise ValueError(f"missing function {name}")
    depth = 1
    end = match.end()
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.end():end - 1]


def host_body(body):
    body = re.sub(r"constexpr sampler .*?;", "", body)
    body = body.replace("float3", "vec3").replace("float4", "vec4")
    body = body.replace("lighting.", "").replace("sun.", "")
    body = body.replace("worldSurfaceNeedsSunShadow(lambert, sun)",
                        "worldSurfaceNeedsSunShadow(lambert)")
    body = re.sub(r",\s*lighting\s*,\s*volumeParams\s*,\s*sun\s*,\s*lights\s*,"
                  r"\s*paletteLUT\s*,\s*lightVolume\s*,\s*lightVolumeId", "", body)
    body = re.sub(r",\s*sunFrameData\s*,\s*sunDepthBuf", "", body)
    body = re.sub(r",\s*sun\s*,\s*sunDepthBuf", "", body)
    body = re.sub(r"texelFetch\(surfaceAO\s*,\s*ownerPixel\s*,\s*0\).r",
                  "sampleAO(ownerPixel)", body)
    body = body.replace("surfaceAO.read(ownerPixel).r", "sampleAO(ownerPixel)")
    body = re.sub(r"surfaceLightVolume\(localLightPosition\s*,.*?\)",
                  "localLight(localLightPosition)", body, flags=re.S)
    return body.replace(".xyz", "").replace("skyColor.rgb", "skyColor").replace(".rgb", ".rgb()")


def replace_last(source, before, after):
    start = source.rfind(before)
    if start < 0:
        raise ValueError(f"missing mutation target: {before}")
    return source[:start] + after + source[start + len(before):]


@unittest.skipUnless(COMPILER, "finite lighting controls require a C++ compiler")
class ShapeSurfaceLightingTest(unittest.TestCase):
    def test_composition_and_mutations(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            base = SHADERS / folder
            source = (base / f"ir_shape_surface_lighting.{suffix}").read_text()
            shared = (base / f"ir_world_surface_lighting.{suffix}").read_text()
            body = (
                "bool worldSurfaceNeedsSunShadow(float lambert){" +
                host_body(function_body(shared, "worldSurfaceNeedsSunShadow")) + "}\n"
                "vec3 composeWorldSurfaceLighting(vec3 albedo,float ao,vec3 normal,"
                "float lambert,float visibility,vec3 localLightPosition){" +
                host_body(function_body(shared, "composeWorldSurfaceLighting")) + "}\n"
                "vec3 worldSurfaceLighting(vec3 albedo,float ao,vec3 position,vec3 normal,"
                "vec4 casterRotation,vec3 localLightPosition){" +
                host_body(function_body(shared, "worldSurfaceLighting")) + "}\n"
                "vec3 query(ivec2 ownerPixel,int ownerWidth,vec3 position,vec3 normal,"
                "float cascadeDepth,vec4 casterRotation,vec3 fallbackColor){" +
                host_body(function_body(source, "shapeSurfaceLighting")) + "}\n"
            )
            tone = (base / f"ir_tonemap.{suffix}").read_text()
            surface = (base / f"ir_surface_lighting.{suffix}").read_text()
            material = (base / f"ir_surface_material.{suffix}").read_text()
            material = re.sub(r"constexpr sampler .*?;", "", material)
            material = material.replace("sampler2D", "int").replace("texture2d<float>", "int")
            material = material.replace("textureLod(paletteLUT, vec2(ao, luminance), 0.0).rgb",
                                        "samplePalette(paletteLUT, ao, luminance)")
            material = material.replace(
                "paletteLUT.sample(paletteSampler, float2(ao, luminance), level(0.0)).rgb",
                "samplePalette(paletteLUT, ao, luminance)")
            helpers = tone + surface.replace(f'#include "ir_tonemap.{suffix}"', "") + material
            helpers = helpers.replace("float3", "vec3").replace("float4", "vec4")
            variants = {
                "production": body,
                "lost_normal_overlay": body.replace("debugOverlayMode == 8", "false"),
                "reversed_normal_overlay": body.replace(
                    "return normal * 0.5 + 0.5", "return normal * -0.5 + 0.5"),
                "lost_material_owner": body.replace("receiverShapes[shapeIndex].color",
                                                    "receiverShapes[0].color"),
                "lost_procedural_fallback": body.replace("!= 0u", "== 999u"),
                "lost_palette_toggle": body.replace("lutEnabled != 0", "true"),
                "lost_material_ao": body.replace("surfaceMaterialColor(albedo, ao,",
                                                  "surfaceMaterialColor(albedo, 1.0,"),
                "shape_uses_generic_query": body.replace(
                    "worldShapeSurfaceSunShadowFactor(position, normal, cascadeDepth,",
                    "worldSurfaceSunShadowFactor(position, normal, cascadeDepth,"),
                "lost_shadow_toggle": body.replace("shadowsEnabled != 0", "true"),
                "skip_grazing_light": body.replace("lambert != 0.0", "lambert > 0.00001"),
                "skip_tiny_intensity": body.replace(
                    "sunIntensity != 0.0", "sunIntensity > 0.00001"),
                "skip_near_full_ambient": body.replace("sunAmbient != 1.0", "sunAmbient < 0.99999"),
                "unconditional_world_query": body.replace(
                    "worldSurfaceNeedsSunShadow(lambert) ?", "true ?", 1),
                "unconditional_shape_query": replace_last(
                    body, "worldSurfaceNeedsSunShadow(lambert) ?", "true ?"),
                "query_back_facing": re.sub(
                    r"\s*&&\s*lambert\s*!=\s*0\.0", "", body, count=1),
                "query_zero_intensity": re.sub(
                    r"\s*&&\s*sunIntensity\s*!=\s*0\.0", "", body, count=1),
                "query_full_ambient": re.sub(
                    r"\s*&&\s*sunAmbient\s*!=\s*1\.0", "", body, count=1),
                "ignored_lambert": body.replace("sunIntensity, lambert, visibility",
                                                "sunIntensity, 1.0, visibility"),
                "lost_local_light": body.replace("localLight(localLightPosition)", "vec3(0.0)"),
                "local_uses_shadow_point": body.replace("localLight(localLightPosition)",
                                                        "localLight(vec3(0.0))"),
                "wrong_sky_normal": body.replace("surfaceSkyLight(normal,",
                                                 "surfaceSkyLight(vec3(0,0,1),"),
                "shape_uses_world_depth": body.replace(
                    "normal, cascadeDepth, casterRotation",
                    "normal, pos3DtoDistance(position), casterRotation"),
                "tone_before_composition": body.replace("return surfaceDisplayColor(linearColor",
                                                        "return surfaceDisplayColor(material"),
            }
            for name, candidate in variants.items():
                with (self.subTest(backend=suffix, mutation=name),
                      tempfile.TemporaryDirectory() as tmp):
                    if name != "production":
                        self.assertTrue(candidate != body, f"{name}: missing mutation target")
                    cpp, exe = Path(tmp) / "lighting.cpp", Path(tmp) / "lighting"
                    cpp.write_text(PREAMBLE + ADAPTERS + helpers + candidate + CASES)
                    result = subprocess.run([COMPILER, "-std=c++17", str(cpp), "-o", str(exe)],
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    run = subprocess.run([str(exe)], capture_output=True, text=True)
                    if name == "production":
                        self.assertEqual(run.returncode, 0, run.stderr)
                        print(suffix, run.stdout.strip())
                    else:
                        self.assertNotEqual(run.returncode, 0)


if __name__ == "__main__":
    unittest.main()

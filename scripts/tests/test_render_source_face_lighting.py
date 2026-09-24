"""Execute deferred sun composition from each production shader backend."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

SHADERS = Path(__file__).resolve().parents[2] / "engine/render/src/shaders"
COMPILER = shutil.which("c++")

PREAMBLE = r"""
#include <algorithm>
#include <cmath>
using uint = unsigned;
float max(float a,float b){return std::max(a,b);}
struct vec3 {
    float x,y,z;
    vec3(float a): x(a),y(a),z(a) {}
    vec3(float a,float b,float c): x(a),y(b),z(c) {}
};
struct vec4 {
    float x,y,z; union {float w; float a;};
    vec4(float a,float b,float c,float d): x(a),y(b),z(c),w(d) {}
    vec4(vec3 v,float d): x(v.x),y(v.y),z(v.z),w(d) {}
    vec3 rgb() const {return {x,y,z};}
};
vec3 operator+(vec3 a,vec3 b) {return {a.x+b.x,a.y+b.y,a.z+b.z};}
vec3 operator*(vec3 a,vec3 b) {return {a.x*b.x,a.y*b.y,a.z*b.z};}
vec3 operator/(vec3 a,vec3 b) {return {a.x/b.x,a.y/b.y,a.z/b.z};}
vec3 clamp(vec3 a,float lo,float hi) {
    return {std::clamp(a.x,lo,hi),std::clamp(a.y,lo,hi),std::clamp(a.z,lo,hi)};
}
bool near(float a,float b) {return std::abs(a-b)<1e-5f;}
bool same(vec4 a,vec4 b) {
    return near(a.x,b.x)&&near(a.y,b.y)&&near(a.z,b.z)&&near(a.w,b.w);
}
"""

CASES = r"""
int main() {
    const vec3 sky{.2f,.4f,.8f};
    const vec3 axes[]={{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
    for(int face=0;face<6;++face){
        const vec3 expected=face==4?sky*2.f:vec3(0.f);
        if(!same(vec4(surfaceSkyLight(axes[face],sky,4.f,.5f),1),vec4(expected,1)))return 9;
    }
    for(int angle=0;angle<360;angle+=5)for(int tilt=-90;tilt<=90;tilt+=5){
        const double yaw=angle*3.141592653589793/180., pitch=tilt*3.141592653589793/180.;
        const vec3 normal{float(std::cos(pitch)*std::cos(yaw)),
                          float(std::cos(pitch)*std::sin(yaw)),float(std::sin(pitch))};
        for(float ao:{0.f,.25f,1.f}){
            const vec3 expected=sky*float(2.*std::max(0.,-std::sin(pitch))*ao);
            if(!same(vec4(surfaceSkyLight(normal,sky,2.f,ao),1),vec4(expected,1)))return 10;
        }
    }

    for(int a=0;a<=10;++a)for(int l=0;l<=10;++l)
    for(int v=0;v<=10;++v)for(int intensity=0;intensity<=8;++intensity){
        const float ambient=a/10.f,lambert=l/10.f,visibility=v/10.f;
        const float scale=intensity/2.f;
        const double expected=(double(ambient)+(1.-ambient)*lambert*visibility)*scale;
        if(std::abs(surfaceSunFactor(ambient,scale,lambert,visibility)-expected)>1e-6)
            return 8;
    }
    const vec4 base{.2f,.1f,.05f,.37f}, sun{.6f,.4f,.2f,2};
    if(!same(sourceFaceLitColor(base,sun,.6f,kSourceLightingLinear,0),base)) return 1;
    if(!same(sourceFaceLitColor(base,sun,.6f,kSourceLightingLinear,.5f),
             {.5f,.3f,.15f,.37f})) return 2;
    if(!same(sourceFaceLitColor(base,sun,.6f,kSourceLightingLinear,1),
             {.8f,.5f,.25f,.37f})) return 3;
    // Precomputed ACES values for linear (.5,.3,.15) at exposure 2.
    if(!same(sourceFaceLitColor(base,sun,.6f,kSourceLightingHDR,.5f),
             {.803797468f,.673290474f,.438491693f,.37f})) return 4;
    if(!same(sourceFaceLitColor(base,sun,.6f,kSourceLightingAOShadow,.5f),
             {.3f,.3f,1,.37f})) return 5;
    if(!same(sourceFaceLitColor(base,sun,.6f,kSourceLightingShadow,1),
             {0,0,0,.37f})) return 6;
    if(!same(sourceFaceLitColor(base,sun,.6f,kSourceLightingShadow,0),
             {1,0,1,.37f})) return 7;
    return 0;
}
"""


@unittest.skipUnless(COMPILER, "shader composition controls require a C++ compiler")
class SourceFaceLightingTest(unittest.TestCase):
    def test_composition_and_rejected_mutations(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            base = SHADERS / directory
            common = (base / f"ir_iso_common.{suffix}").read_text()
            constants = "\n".join(re.findall(
                r"(?:constant|const) uint kSourceLighting\w+ = \d+u;", common))
            tone = (base / f"ir_tonemap.{suffix}").read_text()
            compose = (base / f"ir_source_face_lighting.{suffix}").read_text()
            surface = (base / f"ir_surface_lighting.{suffix}").read_text()
            surface = surface.replace(f'#include "ir_tonemap.{suffix}"', "")
            compose = compose.replace(f'#include "ir_surface_lighting.{suffix}"', "")
            variants = {
                "production": (compose, 0),
                "shadow_indirect_light": (compose.replace(
                    "base.rgb + directSunAndExposure.rgb * visibility",
                    "(base.rgb + directSunAndExposure.rgb) * visibility"), 1),
                "tonemap_before_visibility": (compose.replace(
                    "surfaceDisplayColor(linear, directSunAndExposure.w, "
                    "mode == kSourceLightingHDR)",
                    "(mode == kSourceLightingHDR ? surfaceDisplayColor("
                    "base.rgb + directSunAndExposure.rgb, "
                    "directSunAndExposure.w, true) * visibility"
                    " : surfaceDisplayColor(linear, directSunAndExposure.w, false))"), 4),
                "opaque_alpha": (compose.replace("base.a", "1.0"), 1),
                "shadow_ambient_factor": (compose, 8),
                "wrong_sky_hemisphere": (compose, 9),
                "sky_ignores_ao": (compose, 9),
            }
            for variant, (body, expected) in variants.items():
                with self.subTest(backend=suffix, variant=variant):
                    candidate_surface = surface
                    if variant == "shadow_ambient_factor":
                        candidate_surface = surface.replace(
                            "ambient + (1.0 - ambient) * lambert * visibility",
                            "(ambient + (1.0 - ambient) * lambert) * visibility")
                        self.assertNotEqual(candidate_surface, surface)
                    if variant == "wrong_sky_hemisphere":
                        candidate_surface = surface.replace("-worldNormal.z", "worldNormal.z")
                    if variant == "sky_ignores_ao":
                        candidate_surface = surface.replace(" * ao;", ";")
                    if variant.startswith("wrong_sky") or variant.startswith("sky_ignores"):
                        self.assertNotEqual(candidate_surface, surface)
                    shader = (constants + tone + candidate_surface + body)
                    shader = shader.replace("constant uint", "const uint")
                    shader = shader.replace("float3", "vec3").replace("float4", "vec4")
                    shader = shader.replace(".rgb", ".rgb()")
                    with tempfile.TemporaryDirectory() as tmp:
                        path = Path(tmp)
                        (path / "lighting.cpp").write_text(PREAMBLE + shader + CASES)
                        build = subprocess.run(
                            [COMPILER, "-std=c++17", str(path / "lighting.cpp"),
                             "-o", str(path / "lighting")], capture_output=True, text=True)
                        self.assertEqual(build.returncode, 0, build.stderr)
                        run = subprocess.run([str(path / "lighting")], capture_output=True)
                        self.assertEqual(run.returncode, expected)


if __name__ == "__main__":
    unittest.main()

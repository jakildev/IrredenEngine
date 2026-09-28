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
    vec3() = default;
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

DEFERRED_CASES = r"""
double displayed(double light,bool hdr,double exposure){
    if(hdr){
        const double x=light*exposure;
        light=(x*(2.51*x+.03))/(x*(2.43*x+.59)+.14);
    }
    return std::clamp(light,0.,1.);
}
int main(){
    const vec3 materials[]={{0,0,0},{.12f,.33f,.71f},{2.f,.5f,4.f}};
    const vec3 fills[]={{0,0,0},{.17f,.03f,.24f}};
    for(vec3 material:materials)for(float ambient:{0.f,.2f,.75f,1.f})
    for(float intensity:{0.f,.5f,1.f,4.f})for(float lambert:{0.f,.3f,1.f}){
        const SurfaceSunTerms terms=surfaceSunTerms(material,ambient,intensity,lambert);
        const double albedo[]={material.x,material.y,material.z};
        const double ambientTerm[]={terms.ambient.x,terms.ambient.y,terms.ambient.z};
        const double directTerm[]={terms.direct.x,terms.direct.y,terms.direct.z};
        for(int i=0;i<3;++i){
            if(std::abs(ambientTerm[i]-albedo[i]*ambient*intensity)>2e-6)return 1;
            if(std::abs(directTerm[i]-albedo[i]*(1.-ambient)*lambert*intensity)>2e-6)return 2;
        }
        for(vec3 fill:fills)for(float visibility:{0.f,.25f,1.f})
        for(bool hdr:{false,true})for(float exposure:{.5f,2.f}){
            const vec4 actual=deferredLighting(material,ambient,intensity,lambert,
                                               fill,visibility,hdr,exposure);
            const double indirect[]={fill.x,fill.y,fill.z};
            const double result[]={actual.x,actual.y,actual.z};
            for(int i=0;i<3;++i){
                const double linear=albedo[i]*intensity*
                    (ambient+(1.-ambient)*lambert*visibility)+indirect[i];
                if(std::abs(result[i]-displayed(linear,hdr,exposure))>2e-6)return 3;
            }
            if(!near(actual.a,.37f))return 4;
        }
    }
    return 0;
}
"""


def lighting_sources(base, suffix):
    common = (base / f"ir_iso_common.{suffix}").read_text()
    constants = "\n".join(re.findall(
        r"(?:constant|const) uint kSourceLighting\w+ = \d+u;", common))
    tone = (base / f"ir_tonemap.{suffix}").read_text()
    compose = (base / f"ir_source_face_lighting.{suffix}").read_text().replace(
        f'#include "ir_surface_lighting.{suffix}"', "")
    surface = (base / f"ir_surface_lighting.{suffix}").read_text().replace(
        f'#include "ir_tonemap.{suffix}"', "")
    return constants, tone, compose, surface


def host_lighting(shader):
    return (shader.replace("constant uint", "const uint")
            .replace("float3", "vec3").replace("float4", "vec4")
            .replace(".rgb", ".rgb()"))


@unittest.skipUnless(COMPILER, "shader composition controls require a C++ compiler")
class SourceFaceLightingTest(unittest.TestCase):
    def test_composition_and_rejected_mutations(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            base = SHADERS / directory
            constants, tone, compose, surface = lighting_sources(base, suffix)
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
                    shader = host_lighting(constants + tone + candidate_surface + body)
                    with tempfile.TemporaryDirectory() as tmp:
                        path = Path(tmp)
                        (path / "lighting.cpp").write_text(PREAMBLE + shader + CASES)
                        build = subprocess.run(
                            [COMPILER, "-std=c++17", str(path / "lighting.cpp"),
                             "-o", str(path / "lighting")], capture_output=True, text=True)
                        self.assertEqual(build.returncode, 0, build.stderr)
                        run = subprocess.run([str(path / "lighting")], capture_output=True)
                        self.assertEqual(run.returncode, expected)

    def test_sun_terms_and_executed_source_record(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            base = SHADERS / directory
            constants, tone, compose, surface = lighting_sources(base, suffix)
            producer = (base / f"c_lighting_to_trixel_body.{suffix}").read_text()
            match = re.search(r"if \(continuousShadow\) \{\s*"
                              r"const SurfaceSunTerms.*?\n    \}", producer, re.DOTALL)
            self.assertIsNotNone(match)
            record = (match.group().replace("sunFrameData.", "").replace("frameData.", "")
                      .replace("sourceFaces.faces", "sourceFaces"))
            variants = {
                "production": (surface, record),
                "lambert_ambient": (surface.replace(
                    "material * ambient * intensity", "material * ambient * lambert * intensity"),
                    record),
                "direct_includes_ambient": (surface.replace(
                    "material * (1.0 - ambient) * lambert * intensity",
                    "material * lambert * intensity"), record),
                "direct_ignores_lambert": (surface.replace(
                    " * lambert * intensity;", " * intensity;"), record),
                "ambient_loses_intensity": (surface.replace(
                    "material * ambient * intensity", "material * ambient"), record),
                "clamped_hdr_terms": (surface.replace(
                    "terms.direct = material", "terms.direct = clamp(material, 0.0, 1.0)"), record),
                "record_swaps_direct": (surface, record.replace(
                    "sunTerms.direct", "sunTerms.ambient")),
                "record_loses_ambient": (surface, record.replace(
                    "baseRgb = sunTerms.ambient", "baseRgb = sunTerms.direct")),
                "record_ignores_exposure": (surface, record.replace(
                    "sunTerms.direct, exposure", "sunTerms.direct, 1.0")),
                "record_loses_hdr": (surface, record.replace("hdrEnabled != 0", "false")),
            }
            for name, (terms, fields) in variants.items():
                with self.subTest(backend=suffix, variant=name):
                    if name != "production":
                        self.assertNotEqual((terms, fields), (surface, record))
                    adapter = r"""
struct SourceFace {
    vec4 worldCenterAndAO{0,0,0,0};
    vec4 directSunAndExposure{0,0,0,0};
    struct {uint z;} owner{0};
};
vec4 deferredLighting(vec3 materialRgb,float sunAmbient,float sunIntensity,float lambert,
                      vec3 indirect,float visibility,int hdrEnabled,float exposure){
    SourceFace sourceFaces[1];
    const int sourceIndex=0;
    const bool continuousShadow=true;
    const vec3 worldReceivePos{1,2,3};
    const float ao=.6f;
    vec3 baseRgb(0);
""" + fields + r"""
    return sourceFaceLitColor(vec4(baseRgb+indirect,.37f),
                              sourceFaces[0].directSunAndExposure,ao,
                              sourceFaces[0].owner.z,visibility);
}
"""
                    shader = host_lighting(constants + tone + terms + compose + adapter)
                    with tempfile.TemporaryDirectory() as tmp:
                        path = Path(tmp)
                        (path / "lighting.cpp").write_text(PREAMBLE + shader + DEFERRED_CASES)
                        build = subprocess.run(
                            [COMPILER, "-std=c++17", str(path / "lighting.cpp"),
                             "-o", str(path / "lighting")], capture_output=True, text=True)
                        self.assertEqual(build.returncode, 0, build.stderr)
                        run = subprocess.run([str(path / "lighting")], capture_output=True)
                        if name == "production":
                            self.assertEqual(run.returncode, 0, run.stderr)
                        else:
                            self.assertIn(run.returncode, (1, 2, 3, 4))


if __name__ == "__main__":
    unittest.main()

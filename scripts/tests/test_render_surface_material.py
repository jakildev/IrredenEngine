"""Execute material sampling and compute call sites with instrumented textures."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_render_source_face_lighting import COMPILER, PREAMBLE, SHADERS

ADAPTER = r"""
struct vec2 {
    float x,y;
    vec2(float a,float b):x(a),y(b){}
};
float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
enum class filter {nearest,linear};
enum class address {clamp_to_edge,repeat};
struct sampler {
    filter filtering;
    address addressing;
    constexpr sampler(filter f,address a):filtering(f),addressing(a){}
};
struct level {
    float value;
    explicit level(float v):value(v){}
};
int paletteReads=0, lastTexture=-1;
float lastLod=-1;
vec2 lastCoordinate{-1,-1};
bool wrongSampler=false;
vec4 paletteTexel{.17f,.61f,1.4f,8.f};
vec4 readPalette(int texture,vec2 coordinate,float lod){
    ++paletteReads;
    lastTexture=texture;
    lastCoordinate=coordinate;
    lastLod=lod;
    return paletteTexel;
}
struct sampler2D {
    int id;
    vec4 sample(sampler s,vec2 coordinate,level lod) const {
        wrongSampler=s.filtering!=filter::nearest || s.addressing!=address::clamp_to_edge;
        return readPalette(id,coordinate,lod.value);
    }
    vec4 sample(sampler s,vec2 coordinate) const {
        return sample(s,coordinate,level(-1));
    }
};
vec4 textureLod(sampler2D texture,vec2 coordinate,float lod){
    return readPalette(texture.id,coordinate,lod);
}
vec4 texture(sampler2D texture,vec2 coordinate){
    return readPalette(texture.id,coordinate,-1);
}
"""

CASES = r"""
int main(){
    const vec3 colors[]={{0,0,0},{.2f,.3f,.4f},{1,1,1},{2.f,.5f,4.f}};
    for(int route=0;route<3;++route)
    for(vec3 albedo:colors)for(float ao:{0.f,.125f,.6f,1.f})
    for(bool enabled:{false,true})for(float alpha:{0.f,.37f,1.f})
    for(float paletteAlpha:{0.f,.2f,8.f}){
        paletteReads=0;lastTexture=-1;lastLod=-1;wrongSampler=false;
        paletteTexel=vec4(.17f,.61f,1.4f,paletteAlpha);
        const float factor=route==0?1.f:2.5f;
        const vec4 src(albedo,alpha);
        const sampler2D palette{17};
        const vec3 actual=route==0 ? surfaceMaterialColor(albedo,ao,enabled,palette)
            : route==1 ? regularMaterial(src,ao,enabled,palette,factor)
                       : overflowMaterial(src,ao,enabled,palette,factor);
        const double color[]={albedo.x,albedo.y,albedo.z};
        const double lut[]={.17f,.61f,1.4f};
        const double result[]={actual.x,actual.y,actual.z};
        for(int i=0;i<3;++i){
            const double expected=color[i]*(enabled?lut[i]:double(ao))*factor;
            if(std::abs(result[i]-expected)>1e-5)return 1;
        }
        if(paletteReads!=(enabled?1:0))return 2;
        if(enabled){
            const double luminance=color[0]*.299+color[1]*.587+color[2]*.114;
            if(!near(lastCoordinate.x,ao)||std::abs(lastCoordinate.y-luminance)>1e-6)return 3;
            if(lastTexture!=17||lastLod!=0.f||wrongSampler)return 4;
        }
    }
    return 0;
}
"""


def host_shader(source):
    return (source.replace("texture2d<float>", "sampler2D")
            .replace("float2", "vec2").replace("float3", "vec3")
            .replace("float4", "vec4").replace("frameData.", "")
            .replace(".rgb", ".rgb()"))


def material_call_site(source):
    match = re.search(
        r"const (?:vec3|float3) materialRgb = .*?;\s*"
        r"(?:vec3|float3) baseRgb = .*?;", source, re.DOTALL)
    if match is None:
        raise ValueError("material calculation was not found")
    return match.group()


@unittest.skipUnless(COMPILER, "material controls require a C++ compiler")
class SurfaceMaterialTest(unittest.TestCase):
    def test_sampling_and_compute_routes_reject_mutations(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            base = SHADERS / directory
            material = (base / f"ir_surface_material.{suffix}").read_text()
            regular = material_call_site(
                (base / f"c_lighting_to_trixel_body.{suffix}").read_text())
            overflow = material_call_site(
                (base / f"c_light_overflow_faces.{suffix}").read_text())
            variants = {
                "production": (material, regular, overflow),
                "ignore_ao": (material.replace("return albedo * ao;", "return albedo;"),
                              regular, overflow),
                "sample_when_disabled": (material.replace("if (!usePalette)", "if (false)"),
                                         regular, overflow),
                "lut_alpha": (material.replace(
                    ").rgb;", ").a;"), regular, overflow),
                "shaded_luminance": (material.replace(
                    "dot(albedo,", "dot(albedo * ao,"), regular, overflow),
                "swapped_coordinates": (material.replace(
                    "2(ao, luminance)", "2(luminance, ao)"), regular, overflow),
                "clamped_hdr_material": (material.replace(
                    "return albedo *", "return clamp(albedo, 0.0, 1.0) *"), regular, overflow),
                "regular_lut_disabled": (material, re.sub(
                    r"(?:frameData\.)?lutEnabled != 0", "false", regular), overflow),
                "overflow_wrong_ao": (material, regular, overflow.replace(
                    "albedo.rgb, ao,", "albedo.rgb, 0.0,")),
            }
            if suffix == "glsl":
                variants["wrong_lod"] = (material.replace(
                    "luminance), 0.0)", "luminance), 1.0)"), regular, overflow)
                variants["implicit_lod"] = (material.replace("textureLod(", "texture(")
                                            .replace("luminance), 0.0)", "luminance))"),
                                            regular, overflow)
            else:
                variants["wrong_lod"] = (material.replace("level(0.0)", "level(1.0)"),
                                         regular, overflow)
                variants["implicit_lod"] = (material.replace(", level(0.0)", ""),
                                            regular, overflow)
                variants["linear_filter"] = (material.replace("filter::nearest", "filter::linear"),
                                             regular, overflow)
                variants["repeat_address"] = (material.replace(
                    "address::clamp_to_edge", "address::repeat"), regular, overflow)
            for name, (helper, cell, extra) in variants.items():
                with self.subTest(backend=suffix, variant=name):
                    if name != "production":
                        self.assertNotEqual((helper, cell, extra), (material, regular, overflow))
                    source = PREAMBLE + ADAPTER + host_shader(helper)
                    source += "\nvec3 regularMaterial(vec4 src,float ao,int lutEnabled," \
                              "sampler2D paletteLUT,float faceFactor){\n" + host_shader(cell) \
                              + "\nreturn baseRgb;\n}\n"
                    source += "\nvec3 overflowMaterial(vec4 albedo,float ao,int lutEnabled," \
                              "sampler2D paletteLUT,float faceFactor){\n" + host_shader(extra) \
                              + "\nreturn baseRgb;\n}\n"
                    with tempfile.TemporaryDirectory() as tmp:
                        path = Path(tmp)
                        (path / "material.cpp").write_text(source + CASES)
                        build = subprocess.run(
                            [COMPILER, "-std=c++17", str(path / "material.cpp"),
                             "-o", str(path / "material")], capture_output=True, text=True)
                        self.assertEqual(build.returncode, 0, build.stderr)
                        run = subprocess.run([str(path / "material")], capture_output=True)
                        if name == "production":
                            self.assertEqual(run.returncode, 0, run.stderr)
                        else:
                            self.assertIn(run.returncode, (1, 2, 3, 4))


if __name__ == "__main__":
    unittest.main()

"""Execute surface depth-layer selection from both production shader backends.

The fixture supplies flat sun-facing planes and a source footprint that covers
the map tap but misses the receiver ray. It tests sampler layer ownership, not
GPU atomics, tile construction, cascade selection or final pixel coverage.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

SHADERS = Path(__file__).resolve().parents[2] / "engine/render/src/shaders"
COMPILER = shutil.which("c++")


def function(source, name):
    match = re.search(r"(?:inline )?(?:float|bool|uint|int) " + name + r"\([^)]*\) \{", source)
    if match is None:
        raise ValueError(f"missing shader function {name}")
    depth, end = 1, match.end()
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


PREAMBLE = r"""
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>
using uint = std::uint32_t;
using std::abs;
using std::min;
using std::max;
struct ivec2 {int x, y;};
struct vec2 {
    float x, y;
    vec2(float a, float b): x(a), y(b) {}
    vec2(ivec2 a): x(a.x), y(a.y) {}
};
struct vec3 {float x, y, z;};
struct vec4 {float x, y, z, w;};
vec2 operator+(vec2 a, vec2 b) {return {a.x+b.x, a.y+b.y};}
vec2 operator-(vec2 a, vec2 b) {return {a.x-b.x, a.y-b.y};}
vec2 operator*(vec2 a, vec2 b) {return {a.x*b.x, a.y*b.y};}
vec2 operator+(vec2 a, float b) {return {a.x+b, a.y+b};}
vec2 operator/(vec2 a, float b) {return {a.x/b, a.y/b};}
float dot(vec2 a, vec2 b) {return a.x*b.x+a.y*b.y;}
float dot(vec3 a, vec3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
// The fixture uses the +Z face and identity rotation exclusively.
vec3 faceOutwardNormal6(int id) {if(id!=5) std::abort(); return {0,0,1};}
vec3 rotateByQuat(vec3 a, vec4 q) {if(q.w!=1) std::abort(); return a;}
const int kSunShadowMapDim = 1024;
const float kSunDepthScale = 1024;
const float kSunDepthOffset = 512;
const float kShadowBiasQuantNoise = 4.0 / kSunDepthScale;
uint depth(float z, uint marker) {return (uint((z+512)*1024)<<8)|marker;}
"""

MAIN = r"""
float sample(bool sourceQueryComplete, uint primary, uint fallback, int cascade) {
    std::vector<uint> sunDepthBuf(kSourceFaceHeaderOffset, 0xffffffffu);
    const int bufferOffset = cascade * kSunShadowMapDim * kSunShadowMapDim;
    const ivec2 nearestPixel{2,3};
    const int address = bufferOffset + 3*kSunShadowMapDim + 2;
    sunDepthBuf[address] = primary;
    sunDepthBuf[kSourceFaceFallbackOffset + address] = fallback;
    const vec2 sunUV{2.9f,3.5f}, origin{0,0}, texelSz{1,1};
    const float sunZ=10, maxShadowThrow=100;
    const vec3 normal{0,0,1}, sunDir{0,0,1}, uHat{1,0,0}, vHat{0,1,0};
    const vec4 casterViewToWorld{0,0,0,1};
    SURFACE_LOOP
    return 0;
}
int main() {
    // The source covers tap (2.5,3.5), but not receiver (2.9,3.5).
    const vec3 corner{2,3,1}, u{.75f,0,0}, v{0,1,0};
    if(sourceFaceRaySeparation({2.5f,3.5f},10,corner,u,v)!=9) return 10;
    if(sourceFaceRaySeparation({2.9f,3.5f},10,corner,u,v)!=-1) return 11;
    const uint source=depth(1,0x89), other=depth(2,0x85), empty=0xffffffffu;
    for(int cascade=0; cascade<2; ++cascade) {
        if(sample(true,other,source,cascade)!=1) return 1;
        if(sample(true,empty,source,cascade)!=0) return 2;
        if(sample(false,empty,source,cascade)!=1) return 3;
        if(sample(false,other,empty,cascade)!=1) return 4;
        if(sample(false,empty,empty,cascade)!=0) return 5;
        if(sample(true,depth(11,0x85),source,cascade)!=0) return 6;
        if(sample(true,depth(-100,0x85),source,cascade)!=0) return 7;
    }
    return 0;
}
"""


@unittest.skipUnless(COMPILER, "surface sampler controls require a C++ compiler")
class MixedSourceShadowTest(unittest.TestCase):
    def test_layer_ownership_and_mutation_controls(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            projection = (SHADERS / directory / f"ir_sun_projection.{suffix}").read_text()
            layout = (SHADERS / directory / f"ir_sun_face_query_layout.{suffix}").read_text()
            sampler = (SHADERS / directory / f"ir_sun_shadow_sample.{suffix}").read_text()
            start = sampler.index("for (int layer = 0;")
            end = sampler.index("\n    float slope", start)
            # Exclude the enclosing surface-receiver condition's closing brace.
            loop = sampler[start:end].rstrip().rsplit("}", 1)[0]
            helpers = "\n".join(function(projection, name) for name in (
                "sunWriteIsSurface", "sunVoxelFaceId", "sunVoxelFaceViewAligned",
                "unpackSunDepth"))
            variants = {
                "production": (loop, 0),
                "discard_other_layer": (
                    loop.replace("layer == 1 && sourceQueryComplete",
                                 "sourceQueryComplete"), 1),
                "accept_source_after_exact_miss": (
                    loop.replace("if (layer == 1 && sourceQueryComplete) continue;", ""), 2),
                "drop_overflow_fallback": (
                    loop.replace("layer < 2", "layer < 1"), 3),
            }
            for variant, (body, expected) in variants.items():
                with self.subTest(backend=suffix, variant=variant):
                    code = PREAMBLE + layout + helpers + MAIN.replace("SURFACE_LOOP", body)
                    code = (code.replace("constant uint", "const uint")
                            .replace("float2", "vec2").replace("float3", "vec3"))
                    with tempfile.TemporaryDirectory() as tmp:
                        path = Path(tmp)
                        (path / "sampler.cpp").write_text(code)
                        build = subprocess.run(
                            [COMPILER, "-std=c++17", str(path / "sampler.cpp"),
                             "-o", str(path / "sampler")], capture_output=True, text=True)
                        self.assertEqual(build.returncode, 0, build.stderr)
                        run = subprocess.run([str(path / "sampler")], capture_output=True)
                        self.assertEqual(run.returncode, expected)


if __name__ == "__main__":
    unittest.main()

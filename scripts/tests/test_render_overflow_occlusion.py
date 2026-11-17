"""Execute the production occluder pyramid and footprint occlusion test on host data.

The reduce kernel and the test are compiled as C++ from the GLSL and Metal
sources and run against a brute-force reference over a synthetic depth image:
every pyramid texel must be the farthest depth code of its pixel block, and a
footprint judged hidden must be nearer-covered at every sample its bounds
hold. The footprint's projection, the prepass raster and GPU dispatch are not
exercised; a footprint here is four pixel corners and depths.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_render_per_axis_caster_position import COMPILER, ROOT, functions

SHADER_DIR = ROOT / "engine/render/src/shaders"

HOST = r"""
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
using uint = unsigned;
using std::clamp;
using std::max;
using std::min;
struct vec2 {
    float x, y;
    vec2(float a = 0) : x(a), y(a) {}
    vec2(float a, float b) : x(a), y(b) {}
    vec2 operator-(float b) const { return {x - b, y - b}; }
    vec2 operator+(float b) const { return {x + b, y + b}; }
};
vec2 min(vec2 a, vec2 b) { return {std::min(a.x, b.x), std::min(a.y, b.y)}; }
vec2 max(vec2 a, vec2 b) { return {std::max(a.x, b.x), std::max(a.y, b.y)}; }
vec2 ceil(vec2 a) { return {std::ceil(a.x), std::ceil(a.y)}; }
vec2 floor(vec2 a) { return {std::floor(a.x), std::floor(a.y)}; }
using std::ceil;
using std::floor;
struct ivec2 {
    int x, y;
    explicit ivec2(int a = 0) : x(a), y(a) {}
    ivec2(int a, int b) : x(a), y(b) {}
    explicit ivec2(vec2 a) : x(int(a.x)), y(int(a.y)) {}
    ivec2 operator+(ivec2 b) const { return {x + b.x, y + b.y}; }
    ivec2 operator-(ivec2 b) const { return {x - b.x, y - b.y}; }
    ivec2 operator>>(int s) const { return {x >> s, y >> s}; }
    ivec2 operator<<(int s) const { return {x << s, y << s}; }
};
ivec2 min(ivec2 a, ivec2 b) { return {std::min(a.x, b.x), std::min(a.y, b.y)}; }
ivec2 max(ivec2 a, ivec2 b) { return {std::max(a.x, b.x), std::max(a.y, b.y)}; }
struct ivec4 { int r; ivec4(int a, int, int, int) : r(a) {} };
struct PerAxisFaceFootprint { vec2 pixel[4]; float depth[4]; };

// The raster target's depth and the pyramid atlas, with a write census.
ivec2 targetSize(0);
std::vector<float> targetDepth;
ivec2 atlasSize(0);
std::vector<int> atlas;
std::vector<int> atlasWrites;
bool atlasOutOfRange = false;
void resetTarget(ivec2 size) {
    targetSize = size;
    targetDepth.assign(size_t(size.x) * size.y, 1.0f);
    atlasSize = ivec2(size.x + 8, size.y + 8);
    atlas.assign(size_t(atlasSize.x) * atlasSize.y, -7);
    atlasWrites.assign(atlas.size(), 0);
    atlasOutOfRange = false;
}
float depthAt(ivec2 p) { return targetDepth[size_t(p.y) * targetSize.x + p.x]; }
void storeAtlas(ivec2 p, int code) {
    if (p.x < 0 || p.y < 0 || p.x >= atlasSize.x || p.y >= atlasSize.y) {
        atlasOutOfRange = true;
        return;
    }
    atlas[size_t(p.y) * atlasSize.x + p.x] = code;
    ++atlasWrites[size_t(p.y) * atlasSize.x + p.x];
}
int loadAtlas(ivec2 p) {
    if (p.x < 0 || p.y < 0 || p.x >= atlasSize.x || p.y >= atlasSize.y) {
        atlasOutOfRange = true;
        return 0;
    }
    return atlas[size_t(p.y) * atlasSize.x + p.x];
}

// GLSL surface.
struct DepthSampler {} occluderPrepassDepth;
struct PyramidImage {} occluderPyramid;
struct PyramidSampler {} occluderDepth;
struct FloatTexel { float r; };
struct IntTexel { int r; };
ivec2 textureSize(DepthSampler, int) { return targetSize; }
FloatTexel texelFetch(DepthSampler, ivec2 p, int) { return {depthAt(p)}; }
IntTexel texelFetch(PyramidSampler, ivec2 p, int) { return {loadAtlas(p)}; }
void imageStore(PyramidImage, ivec2 p, ivec4 value) { storeAtlas(p, value.r); }
struct { ivec2 xy; } gl_GlobalInvocationID;

// Metal surface.
namespace access { struct read; struct write; }
template <class T, class A> struct depth2d {
    int get_width() const { return targetSize.x; }
    int get_height() const { return targetSize.y; }
    float read(ivec2 p) const { return depthAt(p); }
};
template <class T, class A> struct texture2d {
    IntTexel read(ivec2 p) const { return {loadAtlas(p)}; }
    void write(ivec4 value, ivec2 p) const { storeAtlas(p, value.r); }
};
struct uint3 { ivec2 xy; };
"""

CHECKS = r"""
int verdictOf(const PerAxisFaceFootprint& footprint) {
#ifdef METAL_ADAPTER
    return perAxisFaceOcclusion(footprint, targetSize, texture2d<int, access::read>{});
#else
    return perAxisFaceOcclusion(footprint, targetSize);
#endif
}

void reduceTarget() {
    for (int y = 0; y * 32 < targetSize.y; ++y) {
        for (int x = 0; x * 32 < targetSize.x; ++x) {
#ifdef METAL_ADAPTER
            c_per_axis_occluder_reduce(
                depth2d<float, access::read>{}, texture2d<int, access::write>{},
                uint3{ivec2(x, y)});
#else
            gl_GlobalInvocationID.xy = ivec2(x, y);
            main_reduce();
#endif
        }
    }
}

int referenceCode(ivec2 pixel) {
    return int(std::ceil(std::clamp(depthAt(pixel), 0.0f, 1.0f) * 16777216.0f));
}

int main() {
    std::mt19937 rng(20261112u);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const int sizes[][2] = {{77, 45}, {64, 32}, {33, 1}, {1, 1}, {130, 67}};
    long hidden = 0, kept = 0, unsampled = 0, referenceHidden = 0, missed = 0;
    for (const auto& size : sizes) {
        resetTarget(ivec2(size[0], size[1]));
        // A near sheet with far holes and a band of depths around the tested
        // faces, so verdicts fall on both sides of the threshold.
        for (float& depth : targetDepth) {
            const float pick = unit(rng);
            depth = pick < 0.04f ? 1.0f : (pick < 0.5f ? 0.25f : 0.25f + 2e-5f * unit(rng));
        }
        reduceTarget();
        if (atlasOutOfRange) return 1;

        // Each level's texels hold the farthest code of their block, and no
        // atlas texel belongs to two levels.
        for (int level = 1; level <= kOccluderLevels; ++level) {
            const ivec2 origin = occluderLevelOrigin(level, targetSize);
            const ivec2 extent = occluderLevelSize(level, targetSize);
            for (int y = 0; y < extent.y; ++y) {
                for (int x = 0; x < extent.x; ++x) {
                    int farthest = -1;
                    const int endY = std::min((y + 1) << level, targetSize.y);
                    const int endX = std::min((x + 1) << level, targetSize.x);
                    for (int py = y << level; py < endY; ++py)
                        for (int px = x << level; px < endX; ++px)
                            farthest = std::max(farthest, referenceCode(ivec2(px, py)));
                    const ivec2 texel = origin + ivec2(x, y);
                    if (loadAtlas(texel) != farthest) return 2;
                    if (atlasWrites[size_t(texel.y) * atlasSize.x + texel.x] != 1) return 3;
                }
            }
        }
        if (atlasOutOfRange) return 4;

        for (int face = 0; face < 20000; ++face) {
            const vec2 centre(
                unit(rng) * float(size[0] + 40) - 20.0f, unit(rng) * float(size[1] + 40) - 20.0f);
            const float halfU = unit(rng) * unit(rng) * 30.0f;
            const float halfV = unit(rng) * unit(rng) * 30.0f;
            const float near = 0.25f + 6e-5f * (unit(rng) - 0.3f);
            PerAxisFaceFootprint footprint{};
            for (int corner = 0; corner < 4; ++corner) {
                footprint.pixel[corner] = vec2(
                    centre.x + ((corner & 1) ? halfU : -halfU),
                    centre.y + ((corner >> 1) ? halfV : -halfV));
                footprint.depth[corner] = near + 1e-4f * unit(rng);
            }
            footprint.depth[face & 3] = near;

            // Reference: every sample whose centre the slack-widened bounds
            // hold, tested on the unreduced depth.
            const int nearCode = int(std::floor(near * 16777216.0f)) - kOcclusionSlackCodes;
            bool anySample = false, allNearer = true;
            for (int py = 0; py < targetSize.y; ++py) {
                for (int px = 0; px < targetSize.x; ++px) {
                    const float cx = float(px) + 0.5f, cy = float(py) + 0.5f;
                    if (cx < centre.x - halfU - kFootprintPixelSlack ||
                        cx > centre.x + halfU + kFootprintPixelSlack ||
                        cy < centre.y - halfV - kFootprintPixelSlack ||
                        cy > centre.y + halfV + kFootprintPixelSlack) continue;
                    anySample = true;
                    allNearer = allNearer && referenceCode(ivec2(px, py)) < nearCode;
                }
            }
            const int verdict = verdictOf(footprint);
            if (atlasOutOfRange) return 5;
            if ((verdict == kFaceUnsampled) != !anySample) return 6;
            if (verdict == kFaceHidden && !allNearer) return 7;
            hidden += verdict == kFaceHidden;
            kept += verdict == kFaceMayOwnPixel;
            unsampled += verdict == kFaceUnsampled;
            if (anySample && allNearer) {
                ++referenceHidden;
                missed += verdict != kFaceHidden;
            }
        }
    }
    // Both verdicts fire, and the pyramid finds most of what the unreduced
    // depth would.
    if (hidden < 1000 || kept < 1000 || unsampled < 100) return 8;
    if (missed * 2 > referenceHidden) return 9;
    std::printf("occlusion controls passed: hidden=%ld kept=%ld unsampled=%ld missed=%ld of %ld\n",
                hidden, kept, unsampled, missed, referenceHidden);
    return 0;
}
"""

CONSTANT = r"^(?:const|constant) (?:int|float) (k\w+) = [^;]+;"


def constants(source, names):
    found = {match.group(1): match.group(0) for match in re.finditer(CONSTANT, source, re.M)}
    return "\n".join(found[name].replace("constant ", "const ") for name in names)


def to_host(source):
    for shader, host in (("float2", "vec2"), ("int2", "ivec2"), ("uint2", "ivec2"),
                         ("int4", "ivec4")):
        source = re.sub(r"\b" + shader + r"\b", host, source)
    source = re.sub(r"\bout\s+(\w+)\s+(\w+)", r"\1& \2", source)
    source = re.sub(r"\bthread\s+", "", source)
    source = re.sub(r"\s*\[\[[^\]]*\]\]", "", source)
    # GLSL float literals are single precision; unsuffixed they would be C++ doubles.
    return re.sub(r"(?<![\w.])(\d+\.\d+)(?![\w.])", r"\1f", source)


def adapter(metal):
    folder = SHADER_DIR / "metal" if metal else SHADER_DIR
    suffix = ".metal" if metal else ".glsl"
    footprint = (folder / f"ir_per_axis_face_footprint{suffix}").read_text()
    layout = (folder / f"ir_per_axis_occluder_layout{suffix}").read_text()
    occlusion = (folder / f"ir_per_axis_occlusion{suffix}").read_text()
    reduce = (folder / f"c_per_axis_occluder_reduce{suffix}").read_text()

    parts = [
        constants(footprint, ["kFootprintPixelSlack"]),
        constants(layout, ["kOccluderLevels", "kOccluderDepthCodes"]),
        constants(occlusion, [
            "kOcclusionSlackCodes", "kOcclusionFineTaps",
            "kFaceMayOwnPixel", "kFaceUnsampled", "kFaceHidden"]),
        "\n".join(functions(footprint, name) for name in (
            "perAxisFootprintTexelBounds", "perAxisFootprintNearestDepth")),
        "\n".join(functions(layout, name) for name in (
            "occluderLevelSize", "occluderLevelOrigin")),
        "\n".join(functions(occlusion, name) for name in (
            "occluderRectNearer", "occluderRectTaps", "perAxisFaceOcclusion")),
        "\n".join(functions(reduce, name) for name in ("farthestCode2x2", "quadrant")),
    ]
    if metal:
        kernel = reduce[reduce.index("kernel void c_per_axis_occluder_reduce("):]
        parts.append(kernel.replace("kernel void", "void", 1))
    else:
        main = functions(reduce, "main")
        parts.append(main.replace("void main()", "void main_reduce()", 1))
    body = to_host("\n".join(parts))
    return HOST + ("#define METAL_ADAPTER\n" if metal else "") + body + CHECKS


def run_host(source):
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "occlusion.cpp"
        program = Path(folder) / "occlusion"
        path.write_text(source)
        built = subprocess.run([COMPILER, "-std=c++17", "-O1", str(path), "-o", str(program)],
                               capture_output=True, text=True)
        if built.returncode:
            raise AssertionError(built.stderr)
        return subprocess.run([str(program)], capture_output=True, text=True)


def mutate(host, pattern, replacement):
    mutant, count = re.subn(pattern, replacement, host)
    if count != 1:
        raise AssertionError(f"mutation {pattern!r} matched {count} times")
    return mutant


@unittest.skipUnless(COMPILER, "C++ host compiler required")
class OverflowOcclusionTest(unittest.TestCase):
    def test_production_pyramid_and_test_both_backends(self):
        for metal in (False, True):
            with self.subTest(backend="metal" if metal else "glsl"):
                host = adapter(metal)
                result = run_host(host)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("controls passed", result.stdout)
                print(("metal: " if metal else "glsl: ") + result.stdout.strip())

                # A pyramid that keeps the nearest depth hides faces behind holes.
                nearest = mutate(host, r"code = max\(code, int\(ceil\(",
                                 "code = min(code < 0 ? 1 << 30 : code, int(ceil(")
                self.assertNotEqual(run_host(nearest).returncode, 0)

                # Slack applied toward the camera rejects faces level with their occluder.
                slack = mutate(host, r"kOccluderDepthCodes\)\) - kOcclusionSlackCodes",
                               "kOccluderDepthCodes)) + 4 * kOcclusionSlackCodes")
                self.assertNotEqual(run_host(slack).returncode, 0)

                # A rectangle that stops one texel short misses its far edge.
                short = mutate(host, r"x <= texelHi\.x; \+\+x", "x < texelHi.x; ++x")
                self.assertNotEqual(run_host(short).returncode, 0)

                # Bounds without their outward slack drop an edge sample.
                bounds = mutate(host, r"floor\(pixelMax - 0\.5f? \+ kFootprintPixelSlack\)",
                                "floor(pixelMax - 1.5f)")
                self.assertNotEqual(run_host(bounds).returncode, 0)


if __name__ == "__main__":
    unittest.main()

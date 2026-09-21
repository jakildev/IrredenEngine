"""Execute both shader layouts and capacity guards against the C++ allocation."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")


@unittest.skipUnless(COMPILER, "shader layout controls require a C++ compiler")
class SourceFaceQueryTest(unittest.TestCase):
    def test_layout_bounds_and_incomplete_list_fallback(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            with self.subTest(backend=suffix), tempfile.TemporaryDirectory() as directory_name:
                shader = (ROOT / "engine/render/src/shaders" / directory /
                          f"ir_sun_face_query_layout.{suffix}").read_text()
                shader = (shader.replace("constant uint", "const uint")
                          .replace("float2", "vec2").replace("float3", "vec3"))
                names = re.findall(r"const uint (kSourceFace\w+) =", shader)
                checks = "\n".join(f"static_assert({n} == IRPrefab::SunShadow::{n});"
                                   for n in names)
                source = """#include <cstdint>
#include <cmath>
using std::abs;
struct vec2 {float x, y;};
struct vec3 {float x, y, z;};
#include "sun_face_query_layout.hpp"
using uint = std::uint32_t;
""" + shader + "\n" + checks + """
int main() {
    if (!sourceFaceQueryComplete(0u) || !sourceFaceQueryComplete(kSourceFaceTileCapacity)) return 1;
    if (sourceFaceQueryComplete(kSourceFaceTileCapacity + 1u) ||
        sourceFaceQueryComplete(0xffffffffu)) return 2;
    for (uint tile = 0; tile < kSourceFaceTileCount; ++tile) {
        const uint base = sourceFaceTileBase(tile);
        if (base < kSourceFaceTileOffset ||
            base + kSourceFaceTileCapacity >= kSourceFaceRecordOffset) return 3;
        if (tile > 0 &&
            base != sourceFaceTileBase(tile - 1) + kSourceFaceTileCapacity + 1u) return 4;
    }
    const uint end = kSourceFaceRecordOffset + kSourceFaceCapacity * kSourceFaceRecordWords;
    if (end != kSourceFaceBufferWords) return 5;
    for (uint marker = 0; marker < 256; ++marker) {
        if (sunWriteIsSourceFace((12345u << 8) | marker) != (marker == 0x89u)) return 6;
    }
    if (sunWriteIsSourceFace(0xffffffffu)) return 7;
    const vec2 uv{0.5f, 0.5f};
    const vec3 corner{0, 0, 2}, u{1, 0, .5f}, v{0, 1, -.25f};
    if (sourceFaceRaySeparation(uv, 3, corner, u, v) != .875f) return 8;
    if (sourceFaceRaySeparation(uv, 3, corner, v, u) != .875f) return 9;
    if (sourceFaceRaySeparation({1.01f, .5f}, 3, corner, u, v) != -1) return 10;
    if (sourceFaceRaySeparation(uv, 3, corner, u, u) != -1) return 11;
    if (sourceFaceRaySeparation(uv, 1, corner, u, v) >= 0) return 12;
    // A front source sample outside its footprint cannot erase the other layer.
    if (sourceFaceRaySeparation(uv, 3, {0, 0, 1}, {.4f, 0, 0}, {0, .4f, 0}) != -1)
        return 13;
    if (sourceFaceRaySeparation(uv, 3, {0, 0, 2}, {1, 0, 0}, {0, 1, 0}) != 1)
        return 14;
    if (kSourceFaceHeaderOffset != 2u * kSourceFaceFallbackOffset) return 15;
    return 0;
}
"""
                path = Path(directory_name)
                (path / "query.cpp").write_text(source)
                build = subprocess.run([COMPILER, "-std=c++17", "-I",
                                        str(ROOT / "engine/prefabs/irreden/render"),
                                        str(path / "query.cpp"), "-o", str(path / "query")],
                                       capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stderr)
                run = subprocess.run([str(path / "query")], capture_output=True, text=True)
                self.assertEqual(run.returncode, 0, f"layout control {run.returncode}")


if __name__ == "__main__":
    unittest.main()

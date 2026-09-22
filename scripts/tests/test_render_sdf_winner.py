"""Execute shader ownership keys and publish predicates under shuffled ties."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")

PROGRAM = r"""
#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>
using uint = std::uint32_t;
uint key(uint tileIdx, uint gl_LocalInvocationIndex, int face, int subPixel) {
    struct { uint x, y; } localId{gl_LocalInvocationIndex % 8, gl_LocalInvocationIndex / 8};
    return KEY;
}
bool publishes(int depthEncoded, int stored, uint sampleOwner, uint elected) {
    return PREDICATE;
}
int main() {
    std::vector<uint> keys;
    for(uint tile=0; tile<64; ++tile)
        for(uint lane=0; lane<64; ++lane)
            for(int face=0; face<3; ++face)
                for(int half=0; half<2; ++half) keys.push_back(key(tile,lane,face,half));
    std::sort(keys.begin(),keys.end());
    if(std::adjacent_find(keys.begin(),keys.end())!=keys.end()) return 1;
    if(key(262143,63,2,1)>=0xffffffffu) return 2;
    std::mt19937 rng(9127);
    for(int trial=0;trial<128;++trial) {
        std::shuffle(keys.begin(),keys.end(),rng);
        uint elected=0xffffffffu;
        for(uint candidate:keys) elected=std::min(elected,candidate);
        std::shuffle(keys.begin(),keys.end(),rng);
        int writers=0;
        for(uint candidate:keys) writers+=publishes(-91,-91,candidate,elected);
        if(writers!=1) return 3;
        if(publishes(-90,-91,elected,elected)) return 4;
        if(publishes(-91,-91,elected,0xffffffffu)) return 5;
    }
}
"""


@unittest.skipUnless(COMPILER, "SDF owner shader controls require a C++ compiler")
class SdfWinnerTest(unittest.TestCase):
    def test_unique_owners_and_publish_mutations(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            source = (ROOT / "engine/render/src/shaders" /
                      f"{folder}c_shapes_to_trixel_body.{suffix}").read_text()
            key = re.search(r"const uint sampleOwner = ([^;]+);", source).group(1)
            predicate = re.search(r"if \((depthEncoded == stored &&.*?)\) \{",
                                  source, re.DOTALL).group(1)
            predicate = re.sub(
                r"atomic_load_explicit\(&sampleOwners\[linearIndex\], memory_order_relaxed\)",
                "elected", predicate).replace("sampleOwners[linearIndex]", "elected")
            variants = {
                "production": (key, predicate, 0),
                "aliased_halves": (key.replace("+ uint(subPixel)", "+ 0u"), predicate, 1),
                "no_owner_gate": (key, "depthEncoded == stored", 3),
                "no_depth_gate": (key, "sampleOwner == elected", 4),
            }
            for name, (expression, condition, expected) in variants.items():
                with (self.subTest(backend=suffix, variant=name),
                      tempfile.TemporaryDirectory() as tmp):
                    path = Path(tmp)
                    code = PROGRAM.replace("KEY", expression).replace("PREDICATE", condition)
                    (path / "test.cpp").write_text(code)
                    result = subprocess.run(
                        [COMPILER, "-std=c++17", "-O2", str(path / "test.cpp"),
                         "-o", str(path / "test")],
                        capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(subprocess.run([str(path / "test")]).returncode, expected)

    def test_capacity_assertion_rejects_overflow(self):
        system = (ROOT / "engine/prefabs/irreden/render/systems" /
                  "system_shapes_to_trixel.hpp").read_text()
        assertion = re.search(r"static_assert\(.*?\);", system, re.DOTALL).group(0)
        declarations = "\n".join(re.search(r"constexpr int " + name + r" = [^;]+;",
                                          system).group(0)
                                 for name in ("kMaxShapeTileDescriptors", "kShapeTileSize"))
        for mutate in (False, True):
            with self.subTest(overflow=mutate), tempfile.TemporaryDirectory() as tmp:
                source = "#include <cstdint>\n#include <limits>\n" + declarations + assertion
                if mutate:
                    source = source.replace("262144", "16777216")
                path = Path(tmp) / "capacity.cpp"
                path.write_text(source)
                build = subprocess.run([COMPILER, "-std=c++17", "-fsyntax-only", str(path)],
                                       capture_output=True, text=True)
                self.assertEqual(build.returncode == 0, not mutate, build.stderr)

    def test_publish_work_excluded_from_other_variants(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            source = (ROOT / "engine/render/src/shaders" /
                      f"{folder}c_shapes_to_trixel_body.{suffix}").read_text()
            source = re.sub(r"^#(?:include|version).*", "", source, flags=re.MULTILINE)
            for mutate in (False, True):
                body = source
                if mutate:
                    body, replacements = re.subn(
                        r"#if IR_SHAPE_PASS == 1(\n    (?:vec4|float4) baseColor)",
                        r"#if 1\1", body)
                    self.assertEqual(replacements, 1)
                for pass_index in range(4):
                    with self.subTest(backend=suffix, mutation=mutate, variant=pass_index):
                        result = subprocess.run(
                            [COMPILER, "-E", "-P", "-x", "c", "-",
                             f"-DIR_SHAPE_PASS={pass_index}"],
                            input=body, capture_output=True, text=True)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        for token in ("baseColor", "packedEntityId", "xrayOccluded"):
                            self.assertEqual(token in result.stdout, mutate or pass_index == 1)

    def test_election_and_clear_order(self):
        system = (ROOT / "engine/prefabs/irreden/render/systems" /
                  "system_shapes_to_trixel.hpp").read_text()
        self.assertLess(system.index("memoryBarrier(BarrierType::ALL)"),
                        system.index("fillBuffer(winnerBuffer_, bytes, 0xFF)"))
        depth = system.index("shapeDepthProgram_->use()")
        election = system.index("shapeOwnerProgram_->use()", depth)
        publish = system.index("shapePublishProgram_->use()", election)
        self.assertIn("BarrierType::SHADER_IMAGE_ACCESS", system[depth:election])
        self.assertIn("BarrierType::SHADER_STORAGE", system[election:publish])
        self.assertEqual(system.count("shapesFrameDataBuf_->subData"), 1)
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            source = (ROOT / "engine/render/src/shaders" /
                      f"{folder}c_shapes_to_trixel_body.{suffix}").read_text()
            self.assertNotIn("passIndex ==", source)
            start = source.index("#if IR_SHAPE_PASS == 3")
            body = source[start:source.index("#elif", start)]
            self.assertIn("depthEncoded ==", body)
            self.assertRegex(body, r"atomic(?:Min|_fetch_min_explicit).*sampleOwners")
            for name, pass_index in (("depth", 0), ("publish", 1),
                                     ("caster", 2), ("owner", 3)):
                wrapper = (ROOT / "engine/render/src/shaders" / folder /
                           f"c_shapes_to_trixel_{name}.{suffix}").read_text()
                self.assertIn(f"#define IR_SHAPE_PASS {pass_index}", wrapper)
                self.assertIn(f'#include "c_shapes_to_trixel_body.{suffix}"', wrapper)


if __name__ == "__main__":
    unittest.main()

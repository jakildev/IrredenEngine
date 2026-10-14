"""Execute shader ownership keys and publish predicates under shuffled ties."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import shape_owner_contract

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

XRAY_PROGRAM = r"""
#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>
using uint = std::uint32_t;
CANDIDATE_BIT
uint key(uint tileIdx, uint gl_LocalInvocationIndex, int face, int subPixel) {
    struct { uint x, y; } localId{gl_LocalInvocationIndex % 8, gl_LocalInvocationIndex / 8};
    return KEY;
}
bool ownerPublishes(int depthEncoded, int stored, uint sampleOwner, uint elected) {
    return OWNER_PREDICATE;
}
bool xrayPublishes(int depthEncoded, int stored, uint sampleOwner, uint elected,
                   bool xrayOccluded) {
    return XRAY_PREDICATE;
}
void fixtureAtomicMin(uint& target, uint value) { target = std::min(target, value); }
void elect(int depthEncoded, int stored, uint sampleOwner, uint& elected,
           bool xrayOccluded) ELECTION
struct Sample { int depth; uint owner; bool xray; };
// Writers of one pixel as (owner stores, x-ray blends), elected and published
// in two independent orders.
struct Writers { int owners = 0, blends = 0; uint blender = 0; };
Writers publish(std::vector<Sample> samples, int stored, std::mt19937& rng) {
    std::shuffle(samples.begin(), samples.end(), rng);
    uint elected = 0xffffffffu;
    for (const Sample& s : samples) elect(s.depth, stored, s.owner, elected, s.xray);
    std::shuffle(samples.begin(), samples.end(), rng);
    Writers writers;
    for (const Sample& s : samples) {
        writers.owners += ownerPublishes(s.depth, stored, s.owner, elected);
        if (xrayPublishes(s.depth, stored, s.owner, elected, s.xray)) {
            ++writers.blends;
            writers.blender = s.owner;
        }
    }
    return writers;
}
int main() {
    const uint last = key(262143, 63, 2, 1);
    if (last >= 0x80000000u) return 2;
    if ((last | kXrayCandidateBit) == 0xffffffffu) return 3;
    std::mt19937 rng(4410);
    for (int trial = 0; trial < 128; ++trial) {
        // Lanes 0..31 are opaque, lanes 32..63 x-ray, so x-ray keys sit on
        // both sides of every opaque key of the neighbouring tiles.
        std::vector<Sample> samples;
        uint lowestXray = 0xffffffffu;
        for (uint tile : {0u, 1u, 262143u})
            for (uint lane = 0; lane < 64; ++lane)
                for (int face = 0; face < 3; ++face) {
                    const bool xray = lane >= 32;
                    const uint owner = key(tile, lane, face, int(rng() & 1u));
                    samples.push_back({int(rng() % 40u) - 90, owner, xray});
                    if (xray) lowestXray = std::min(lowestXray, owner);
                }
        // Content drawn before the shape stage is nearer than every sample.
        Writers behind = publish(samples, -200, rng);
        if (behind.owners != 0 || behind.blends != 1) return 4;
        if (behind.blender != lowestXray) return 5;
        // A sample at the stored depth owns the pixel; no candidate may blend,
        // whichever side of the owner's key the candidates fall on.
        for (uint ownerTile : {0u, 262143u}) {
            std::vector<Sample> owned = samples;
            owned.push_back({-91, key(ownerTile, 7, 1, 0), false});
            owned.push_back({-91, key(ownerTile, 40, 1, 0), true});
            Writers front = publish(owned, -91, rng);
            if (front.owners != 1) return 6;
            if (front.blends != 0) return 7;
        }
        // With no x-ray sample, an occluded pixel has no writer.
        std::vector<Sample> opaque;
        for (const Sample& s : samples) if (!s.xray) opaque.push_back(s);
        Writers none = publish(opaque, -200, rng);
        if (none.owners != 0 || none.blends != 0) return 8;
    }
}
"""


@unittest.skipUnless(COMPILER, "SDF owner shader controls require a C++ compiler")
class SdfWinnerTest(unittest.TestCase):
    def exit_code(self, code):
        """Build one fixture program and return what it exits with."""
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / "test.cpp").write_text(code)
            result = subprocess.run(
                [COMPILER, "-std=c++17", "-O2", str(path / "test.cpp"),
                 "-o", str(path / "test")],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            return subprocess.run([str(path / "test")]).returncode

    def test_unique_owners_and_publish_mutations(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            source = (ROOT / "engine/render/src/shaders" /
                      f"{folder}c_shapes_to_trixel_body.{suffix}").read_text()
            contract = shape_owner_contract(source)
            key, predicate = contract["key"], contract["owner_predicate"]
            variants = {
                "production": (key, predicate, 0),
                "aliased_halves": (key.replace("+ uint(subPixel)", "+ 0u"), predicate, 1),
                "no_owner_gate": (key, "depthEncoded == stored", 3),
                "no_depth_gate": (key, "sampleOwner == elected", 4),
            }
            for name, (expression, condition, expected) in variants.items():
                with self.subTest(backend=suffix, variant=name):
                    code = PROGRAM.replace("KEY", expression).replace("PREDICATE", condition)
                    self.assertEqual(self.exit_code(code), expected)

    def test_xray_blend_has_one_writer(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            contract = shape_owner_contract(
                (ROOT / "engine/render/src/shaders" /
                 f"{folder}c_shapes_to_trixel_body.{suffix}").read_text())
            candidate = "sampleOwner | kXrayCandidateBit"
            for text in (contract["xray_predicate"], contract["election"]):
                self.assertEqual(text.count(candidate), 1)
            variants = {
                "production": (contract["xray_predicate"], contract["election"], 0),
                "no_election_gate": ("xrayOccluded && depthEncoded > stored",
                                     contract["election"], 4),
                # Candidates then share the owners' key range, and one with a
                # lower key takes the pixel from the sample at the stored depth.
                "candidate_bit_dropped": (
                    contract["xray_predicate"].replace(candidate, "sampleOwner"),
                    contract["election"].replace(candidate, "sampleOwner"), 6),
            }
            for name, (xray, election, expected) in variants.items():
                with self.subTest(backend=suffix, variant=name):
                    code = (XRAY_PROGRAM.replace("CANDIDATE_BIT", contract["candidate_bit"])
                            .replace("KEY", contract["key"])
                            .replace("OWNER_PREDICATE", contract["owner_predicate"])
                            .replace("XRAY_PREDICATE", xray).replace("ELECTION", election))
                    self.assertEqual(self.exit_code(code), expected)

    def test_capacity_assertion_rejects_overflow(self):
        system = (ROOT / "engine/prefabs/irreden/render/systems" /
                  "system_shapes_to_trixel.hpp").read_text()
        start = system.index("constexpr int kMaxShapeDescriptors")
        contract = system[start:system.index("template <>", start)]
        self.assertEqual(contract.count("static_assert("), 2)
        # 5592405 is the largest tile count whose keys keep bit 31 clear;
        # 8388608 tiles give keys above it that still fit a uint32.
        for tiles, accepted in ((262144, True), (5592405, True), (5592406, False),
                                (8388608, False), (16777216, False)):
            with self.subTest(tiles=tiles), tempfile.TemporaryDirectory() as tmp:
                source = "#include <cstdint>\n#include <limits>\n" + contract.replace(
                    "kMaxShapeTileDescriptors = 262144",
                    f"kMaxShapeTileDescriptors = {tiles}")
                self.assertIn(f"= {tiles};", source)
                path = Path(tmp) / "capacity.cpp"
                path.write_text(source)
                build = subprocess.run([COMPILER, "-std=c++17", "-fsyntax-only", str(path)],
                                       capture_output=True, text=True)
                self.assertEqual(build.returncode == 0, accepted, build.stderr)

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
                        for token in ("baseColor", "packedEntityId"):
                            self.assertEqual(token in result.stdout, mutate or pass_index == 1)
                        # The owner pass elects the x-ray candidate the
                        # publish pass blends, so both read the flag.
                        self.assertEqual("xrayOccluded" in result.stdout, pass_index in (1, 3))

    def test_election_and_clear_order(self):
        system = (ROOT / "engine/prefabs/irreden/render/systems" /
                  "system_shapes_to_trixel.hpp").read_text()
        geometry = (ROOT / "engine/prefabs/irreden/render/components" /
                    "canvas_shape_geometry.hpp").read_text()
        self.assertLess(geometry.index("memoryBarrier(IRRender::BarrierType::ALL)"),
                        geometry.index("fillBuffer(sampleOwners_.second, bytes, 0xFF)"))
        self.assertLess(system.index("shapeGeometry_.prepareSampleOwners("),
                        system.index("shapeDepthProgram_->use()"))
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

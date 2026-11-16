"""Execute the voxel sun-face admission scan (pass 1 of `c_bake_voxel_sun_faces`)
from both dialects on 64 host threads joined by a real barrier, and require the
serial exclusive prefix the emit pass reads: every group's offset, the source-face
base and total, one header advance per bake, and the hierarchy-needed flag on
exactly the overflow condition. Unsigned arithmetic is exact in any order, so the
admission order and every face index depend only on these words.

The mutation controls make the scan inclusive, add the header once per lane and
cut the last round, and each must fail.
"""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_block, replace_once

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "engine/render/src/shaders"
COMPILER = shutil.which("c++")
LANES = 64

DIALECTS = {
    "glsl": (SHADERS / "c_bake_voxel_sun_faces.glsl", "if (admission.x == 1)"),
    "metal": (SHADERS / "metal/c_bake_voxel_sun_faces.metal", "if (faceFrame.admission.x == 1)"),
}

PRELUDE = r"""
#include <barrier>
#include <cstdint>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>
typedef std::uint32_t uint;
struct Int4 { int x = 0, y = 0, z = 0, w = 0; };
struct Frame { Int4 admission; };
static Frame faceFrame;
static Int4 &admission = faceFrame.admission;
static uint *admissionScratch = nullptr;
static uint sharedRecordCounts[64];
static int headerAdds = 0;
struct DepthWords {
    std::vector<uint> words;
    uint &operator[](uint i) { return words[i]; }
    uint length() const { return uint(words.size()); }
};
static DepthWords sunDepthBuf;
static std::barrier<> *workgroup = nullptr;
static void barrier() { workgroup->arrive_and_wait(); }
static void memoryBarrierShared() {}
namespace mem_flags { constexpr int mem_threadgroup = 0; }
static void threadgroup_barrier(int) { barrier(); }
constexpr int memory_order_relaxed = 0;
constexpr uint kSourceFaceCapacityOffset = 0u;
constexpr uint kSourceFaceHeaderOffset = 1u;
static uint g_capacity = 0u;
static uint sourceFaceCapacity(uint) { return g_capacity; }
static uint sourceFaceHeaderIndex(uint) { return kSourceFaceHeaderOffset; }
static uint sourceFaceHierarchyNeededIndex(uint) { return 2u; }
static uint atomicAdd(uint &word, uint value) { ++headerAdds; const uint old = word; word += value; return old; }
static uint atomicExchange(uint &word, uint value) { const uint old = word; word = value; return old; }
static uint atomic_load_explicit(uint *word, int) { return *word; }
static uint atomic_fetch_add_explicit(uint *word, uint value, int) { return atomicAdd(*word, value); }
static void atomic_store_explicit(uint *word, uint value, int) { *word = value; }
"""

DRIVER = r"""
static void scanLane(uint lane) {
    __SCAN__
}

static int runCase(const std::vector<uint> &counts, uint capacity, uint base) {
    const uint groups = uint(counts.size());
    std::vector<uint> scratch(counts);
    scratch.resize(counts.size() + 2u, 0xDEADBEEFu);
    admissionScratch = scratch.data();
    faceFrame.admission = Int4{1, int(groups), 0, 0};
    g_capacity = capacity;
    sunDepthBuf.words.assign(3u, 0u);
    sunDepthBuf.words[kSourceFaceCapacityOffset] = capacity;
    sunDepthBuf.words[kSourceFaceHeaderOffset] = base;
    headerAdds = 0;
    std::barrier<> sync(64);
    workgroup = &sync;
    std::vector<std::thread> lanes;
    for (uint lane = 0u; lane < 64u; ++lane) lanes.emplace_back(scanLane, lane);
    for (auto &lane : lanes) lane.join();

    uint total = 0u;
    for (uint group = 0u; group < groups; ++group) {
        if (scratch[group] != total) {
            std::printf("group %u offset %u, serial %u\n", group, scratch[group], total);
            return 1;
        }
        total += counts[group];
    }
    const bool overflow = total > 0u && (base >= capacity || total > capacity - base);
    if (scratch[groups] != base || scratch[groups + 1u] != total) return 2;
    if (headerAdds != 1 || sunDepthBuf.words[kSourceFaceHeaderOffset] != base + total) return 3;
    if (sunDepthBuf.words[2] != (overflow ? 1u : 0u)) return 4;
    return 0;
}

int main() {
    std::mt19937 random(5091u);
    int cases = 0;
    for (uint groups : {1u, 2u, 63u, 64u, 65u, 127u, 128u, 1000u, 15625u}) {
        std::vector<uint> counts(groups);
        for (uint &count : counts) count = random() % 193u;
        if (const int failure = runCase(counts, 1u << 30, 7u)) return failure;
        ++cases;
    }
    std::vector<uint> wrap(130u);
    for (uint &count : wrap) count = 0xF0000000u + random() % 4096u;
    if (const int failure = runCase(wrap, 0xFFFFFFFFu, 3u)) return 10 + failure;
    std::vector<uint> edge(200u, 5u);
    for (uint base : {0u, 24u, 25u, 26u, 1000u, 1001u}) {
        if (const int failure = runCase(edge, 1001u, base)) return 20 + failure;
        ++cases;
    }
    std::vector<uint> empty(70u, 0u);
    if (const int failure = runCase(empty, 4u, 9u)) return 30 + failure;
    std::printf("%d admission scan cases match the serial prefix\n", cases + 2);
    return 0;
}
"""


def scan_body(dialect, mutate=None):
    path, marker = DIALECTS[dialect]
    block = extract_block(path.read_text(), marker)
    body = block[block.index("{") + 1:block.rindex("}")]
    return mutate(body) if mutate else body


def run(body):
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "scan.cpp"
        source.write_text(PRELUDE + DRIVER.replace("__SCAN__", body))
        build = subprocess.run(
            [COMPILER, "-std=c++20", "-O2", "-pthread", str(source), "-o",
             str(Path(directory) / "scan")],
            capture_output=True, text=True)
        if build.returncode != 0:
            raise AssertionError(build.stderr)
        return subprocess.run([str(Path(directory) / "scan")], capture_output=True,
                              text=True, timeout=120)


@unittest.skipUnless(COMPILER, "the admission scan control needs a C++20 compiler")
class SunFaceAdmissionScanTest(unittest.TestCase):
    def test_scan_matches_the_serial_prefix(self):
        for dialect in DIALECTS:
            with self.subTest(dialect=dialect):
                result = run(scan_body(dialect))
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("match the serial prefix", result.stdout)

    def test_mutations_fail(self):
        mutations = {
            "inclusive": lambda body: replace_once(
                body, "total + sharedRecordCounts[lane] - count", "total + sharedRecordCounts[lane]"),
            "header_per_lane": lambda body: replace_once(
                body, "if (lane == 0u) {", "if (lane < 2u) {"),
            "short_rounds": lambda body: replace_once(
                body, "roundBase < groups;", "roundBase + 64u < groups;"),
        }
        for dialect in DIALECTS:
            for name, mutate in mutations.items():
                with self.subTest(dialect=dialect, mutation=name):
                    self.assertNotEqual(run(scan_body(dialect, mutate)).returncode, 0)


if __name__ == "__main__":
    unittest.main()

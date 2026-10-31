"""Depth-slab extrema for the raster-only sun layer: exactness proof and limits.

Part one proves, against brute force, that per-slab minimum and maximum
answer interval existence exactly when the slab is no wider than the query
window, and finds the counterexample when it is wider. Part two runs the
production PCF predicate on a slab whose two extrema are both rejected while
an interior write blocks the receiver, so extrema alone cannot replace the
full write set. Part three states the storage arithmetic.
"""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import test_render_source_face_overflow as overflow

COMPILER = shutil.which("c++")

SLAB_PROOF = r"""
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
// Depths are quantized integers; the window (lo, hi) is open at both ends.
struct Slabs {
    int width; std::vector<int> lo, hi;
    explicit Slabs(int w, int count) : width(w), lo(count, 1 << 30), hi(count, -(1 << 30)) {}
    void insert(int z) {
        const int s = z / width; lo[s] = std::min(lo[s], z); hi[s] = std::max(hi[s], z);
    }
    bool occupied(int s) const { return lo[s] <= hi[s]; }
    bool blocks(int low, int high) const {
        const int first = low / width, last = high / width;
        if (first == last) return occupied(first) && hi[first] > low && lo[first] < high;
        if (occupied(first) && hi[first] > low) return true;
        if (occupied(last) && lo[last] < high) return true;
        for (int s = first + 1; s < last; ++s) if (occupied(s)) return true;
        return false;
    }
};
bool brute(const std::vector<int>& set, int low, int high) {
    for (int z : set) if (z > low && z < high) return true;
    return false;
}
int main() {
    const int domain = 40;
    std::vector<std::vector<int>> sets;
    for (int a = 0; a < domain; ++a) {
        sets.push_back({a});
        for (int b = a + 1; b < domain; ++b) {
            sets.push_back({a, b});
            for (int c = b + 1; c < domain; ++c) sets.push_back({a, b, c});
        }
    }
    long exact = 0, mismatches = 0;
    for (int window : {8, 10, 13}) for (int width : {4, 7, 8, 10, 13}) {
        if (width > window) continue;
        for (const auto& set : sets) {
            Slabs slabs(width, domain / width + 2);
            for (int z : set) slabs.insert(z);
            for (int high = window; high <= domain; ++high) {
                const int low = high - window;
                if (slabs.blocks(low, high) != brute(set, low, high)) {
                    std::cerr << "slab extrema disagree with brute force\n"; return 1;
                }
                ++exact;
            }
        }
    }
    for (int window : {8}) for (int width : {9, 16}) {
        for (const auto& set : sets) {
            Slabs slabs(width, domain / width + 2);
            for (int z : set) slabs.insert(z);
            for (int high = window; high <= domain; ++high) {
                const int low = high - window;
                if (slabs.blocks(low, high) != brute(set, low, high)) ++mismatches;
            }
        }
    }
    if (mismatches == 0) { std::cerr << "wide slabs never mislead the extrema query\n"; return 1; }
    std::cout << exact << " exact slab queries; " << mismatches
              << " wide-slab mismatches when both window ends share a slab\n";
}
"""

# Receiver tilted toward +X so its plane gradient is one depth unit per sun
# texel: a splat displaced +7 texels is coplanar at depth 3, one displaced -7
# at depth 17, while a direct write at depth 6 is a genuine blocker.
PROVENANCE = overflow.CASES[:overflow.CASES.index("int main(){")] + r"""
uint word(float z,uint low){return (depth(z)&0xffffff00u)|low;}
void tap(uint cascade,uint primary,uint second){
    const uint address=cascade*kCascadeTexelCount+3*kSunShadowMapDim+2;
    sunDepthBuf.words[address]=primary;
    sunDepthBuf.words[kSourceFaceFallbackOffset+address]=second;
}
float query(uint cascade){
    return sample({2.5f,3.5f},10,cascade,false,{0.70710678f,0,0.70710678f},64);
}
int main(){
    const uint empty=0xffffffffu;
    const uint nearSplat=word(3,0x70),interior=word(6,0),farSplat=word(17,0x90);
    for(uint cascade:{0u,1u}){
        reset(0,0,cascade);
        tap(cascade,interior,empty);
        require(query(cascade)==1,"interior direct write blocks the tilted receiver");
        tap(cascade,nearSplat,empty);
        require(query(cascade)==0,"slab minimum is a coplanar splat");
        tap(cascade,farSplat,empty);
        require(query(cascade)==0,"slab maximum is a coplanar splat");
        secondDepthLayer=true;
        tap(cascade,nearSplat,farSplat);
        require(query(cascade)==0,"both slab extrema together miss the interior blocker");
        tap(cascade,nearSplat,interior);
        require(query(cascade)==1,"retaining the interior word restores the shadow");
        secondDepthLayer=false;
    }
    const unsigned long long slabs=1024/64,words=slabs*2;
    const unsigned long long bytes=2ull*1024*1024*words*4;
    require(bytes==268435456ull,"sixteen min/max slabs over the encoded range cost 256 MiB");
    std::cout<<"slab extrema counterexample executed; "<<bytes<<" bytes for "<<slabs
             <<" slabs; "<<(2ull*1024*1024*8*4)<<" bytes for a 256-unit frame range\n";
}
"""


@unittest.skipUnless(COMPILER, "slab proofs require a C++ compiler")
class SunDepthSlabTest(unittest.TestCase):
    def test_slab_extrema_exact_when_no_wider_than_the_window(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "slab.cpp").write_text(SLAB_PROOF)
            build = subprocess.run([COMPILER, "-std=c++17", "-O2", str(path / "slab.cpp"),
                                    "-o", str(path / "slab")], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(path / "slab")], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            print(run.stdout, end="")
            mutant = SLAB_PROOF.replace("hi[first] > low", "hi[first] >= low")
            (path / "slab.cpp").write_text(mutant)
            subprocess.run([COMPILER, "-std=c++17", "-O2", str(path / "slab.cpp"),
                            "-o", str(path / "slab")], check=True, capture_output=True)
            run = subprocess.run([str(path / "slab")], capture_output=True, text=True)
            self.assertNotEqual(run.returncode, 0)
            self.assertIn("disagree", run.stderr)

    def test_extrema_lose_an_interior_blocker_under_provenance_rejection(self):
        harness = overflow.SourceFaceOverflowTest()
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            with self.subTest(backend=suffix):
                run = harness.compile_and_run(overflow.host_source(
                    suffix, directory, False, cases=PROVENANCE, include_pcf=True))
                self.assertEqual(run.returncode, 0, run.stderr)
                print(f"{suffix}: {run.stdout}", end="")
            with self.subTest(backend=suffix, mutation="skip_same_plane_test"):
                run = harness.compile_and_run(overflow.host_source(
                    suffix, directory, False, cases=PROVENANCE, include_pcf=True,
                    mutation=("if (h <= bias) continue;", "")))
                self.assertNotEqual(run.returncode, 0)
                self.assertIn("coplanar splat", run.stderr)


if __name__ == "__main__":
    unittest.main()

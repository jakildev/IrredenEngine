"""Execute box-cast sample partitions extracted from both shader backends."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")


@unittest.skipUnless(COMPILER, "Box-cast dispatch checks require a C++ compiler")
class BoxCastDispatchTest(unittest.TestCase):
    def test_each_sample_has_exactly_one_writer(self):
        system = (ROOT / "engine/prefabs/irreden/render/systems/"
                  "system_bake_sun_shadow_map.hpp").read_text()
        start = system.index("constexpr int kTargetBoxCastWorkgroups")
        end = system.index("const ivec4 params", start)
        policy = system[start:end].replace("IRMath::clamp", "std::clamp")
        self.assertIn("params(count, grid.x, subdivisions, workgroupsPerShape)", system)
        self.assertIn("dispatchCompute(grid.x, grid.y, workgroupsPerShape)", system)
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            source = (ROOT / f"engine/render/src/shaders/{folder}"
                      f"c_bake_box_sun_shadow.{suffix}").read_text()
            loop = re.search(r"for \(uint sampleIndex = (.*?); "
                             r"sampleIndex < uint\(size.x \* size.y\); "
                             r"sampleIndex \+= (.*?)\) \{", source)
            initial, stride = loop.groups()
            initial = initial.replace("gl_WorkGroupID.z", "group").replace("groupId.z", "group")
            initial = initial.replace("gl_LocalInvocationID.x", "lane").replace("localId.x", "lane")
            stride = stride.replace("uint(dispatch.w)", "groups")
            template = r"""
#include <algorithm>
#include <cstdint>
#include <vector>
using uint = std::uint32_t;
int policy(int count) { POLICY return workgroupsPerShape; }
int main() {
    for(int count=1;count<=8192;++count) {
        int groups=policy(count);
        if(groups<1 || groups>32 || groups*count>std::max(count,256)) return 1;
        if(count>=256 && groups!=1) return 2;
    }
    for(uint groups=1;groups<=32;++groups)
        for(uint samples : {0u,1u,63u,64u,65u,1023u,2049u,1048576u}) {
            std::vector<int> visits(samples,0);
            for(uint group=0;group<groups;++group)
                for(uint lane=0;lane<64;++lane)
                    for(uint sampleIndex=INITIAL;sampleIndex<samples;sampleIndex+=STRIDE)
                        ++visits[sampleIndex];
            for(int count:visits) if(count!=1) return 3;
        }
}
"""
            for name, first, step, expected in (
                ("production", initial, stride, 0),
                ("overlapping_groups", initial.replace("group * 64u", "group * 32u"), stride, 3),
                ("unpartitioned_stride", initial, "64u", 3),
            ):
                with (self.subTest(backend=suffix, variant=name),
                      tempfile.TemporaryDirectory() as tmp):
                    path = Path(tmp)
                    code = template.replace("POLICY", policy).replace("INITIAL", first)
                    code = code.replace("STRIDE", step)
                    (path / "test.cpp").write_text(code)
                    build = subprocess.run([COMPILER, "-std=c++17", "-O2",
                                            str(path / "test.cpp"), "-o", str(path / "test")],
                                           capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    self.assertEqual(subprocess.run([str(path / "test")]).returncode, expected)


if __name__ == "__main__":
    unittest.main()

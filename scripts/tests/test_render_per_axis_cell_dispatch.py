"""Execute the shared per-axis integer dispatch contract from both shader backends.

The C++ adapter checks indexing and CPU/shader constants, not GPU compilation or
buffer binding. Native finalize tests and render captures cover those separately.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")

PREAMBLE = """
#include <array>
#include <cstdio>
#include <cstdint>
using uint = std::uint32_t;
struct Pixel {
    int x, y;
    Pixel(int x, int y) : x(x), y(y) {}
};
using ivec2 = Pixel;
using int2 = Pixel;
"""

CHECKS = """
int main() {
    // Enumerate invocations in dispatch order; no flattening formula in the oracle.
    for (auto grid : {std::array<uint, 2>{1, 1}, {3, 2}, {1024, 65}}) {
        uint expected = 0;
        for (uint y = 0; y < grid[1]; ++y)
            for (uint x = 0; x < grid[0]; ++x)
                for (uint local = 0; local < 256; ++local, ++expected)
                    if (perAxisCellInvocationIndex(x, y, grid[0], local) != expected) {
                        std::fprintf(stderr, "invocation %u lost or duplicated\\n", expected);
                        return 1;
                    }
    }
    // Enumerate row-major cells, including thin, odd-width and wide canvases.
    for (int width : {1, 3, 17, 4096}) {
        uint cell = 0;
        for (int y = 0; y < 257; ++y)
            for (int x = 0; x < width; ++x, ++cell) {
                const Pixel pixel = perAxisCellPixel(cell, width);
                if (pixel.x != x || pixel.y != y) {
                    std::fprintf(stderr, "cell %u decoded incorrectly\\n", cell);
                    return 2;
                }
            }
    }
    const Pixel last = perAxisCellPixel(2147483647u, 65536);
    if (last.x != 65535 || last.y != 32767) return 3;
    std::puts("multirow_dispatch_and_row_major_cells=passed");
}
"""


def shader_cpp(suffix: str) -> str:
    folder = "metal" if suffix == "metal" else ""
    source = (ROOT / "engine/render/src/shaders" / folder /
              f"ir_per_axis_cell_dispatch.{suffix}").read_text()
    source = re.sub(r"^#.*$", "", source, flags=re.MULTILINE)
    source = source.replace("using namespace metal;", "").replace("constant uint", "const uint")
    cpu = (ROOT / "engine/render/include/irreden/render/ir_render_types.hpp").read_text()

    def constant(name: str) -> int:
        return int(re.search(r"constexpr [\w:]+ " + name + r" = (\d+);", cpu)[1])

    checks = (
        f"static_assert(kDispatchArgsBaseUint * sizeof(uint) == "
        f"{constant('kPerAxisCellDispatchArgsOffsetBytes')});\n"
        f"static_assert(kPerAxisCellComputeTile == "
        f"{constant('kPerAxisCellComputeTile')});\n"
    )
    return PREAMBLE + source + checks + CHECKS


@unittest.skipUnless(COMPILER, "shader integer controls require a C++ compiler")
class PerAxisCellDispatchTest(unittest.TestCase):
    def test_dispatch_and_pixel_decoding_with_mutation_controls(self):
        for suffix in ("glsl", "metal"):
            source = shader_cpp(suffix)
            mutations = (
                ("correct", source, ""),
                ("lost-row", source.replace("groupX + groupY * groupsX", "groupX"),
                 "invocation"),
                ("wrong-column", source.replace("int(linearCell) % width",
                                                "int(linearCell) / width"), "cell"),
            )
            for name, text, failure in mutations:
                with self.subTest(backend=suffix, variant=name):
                    if failure:
                        self.assertNotEqual(text, source, "mutation did not alter the source")
                    with tempfile.TemporaryDirectory() as temporary:
                        path = Path(temporary)
                        cpp, executable = path / "dispatch.cpp", path / "dispatch"
                        cpp.write_text(text)
                        build = subprocess.run(
                            [COMPILER, "-std=c++17", "-O2", str(cpp), "-o", str(executable)],
                            capture_output=True, text=True)
                        self.assertEqual(build.returncode, 0, build.stderr)
                        result = subprocess.run(
                            [str(executable)], capture_output=True, text=True)
                        if failure:
                            self.assertNotEqual(result.returncode, 0)
                            self.assertIn(failure, result.stderr)
                        else:
                            self.assertEqual(result.returncode, 0, result.stderr)
                            self.assertIn("multirow_dispatch_and_row_major_cells=passed",
                                          result.stdout)


if __name__ == "__main__":
    unittest.main()

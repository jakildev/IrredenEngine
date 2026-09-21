"""Execute the scalar shader metadata helpers against the sun-map wire contract."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

SHADERS = Path(__file__).resolve().parents[2] / "engine/render/src/shaders"
COMPILER = shutil.which("c++")
FUNCTIONS = ("sunWriteIsSurface", "sunVoxelFaceMarker", "sunVoxelFaceId",
             "sunVoxelFaceViewAligned")


def scalar_functions(source):
    bodies = []
    for name in FUNCTIONS:
        match = re.search(r"(?:inline )?(?:bool|uint|int) " + name + r"\([^)]*\) \{", source)
        if match is None:
            raise ValueError(f"missing shader helper {name}")
        depth, end = 1, match.end()
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        bodies.append(source[match.start():end])
    return "\n".join(bodies)


@unittest.skipUnless(COMPILER, "scalar shader contract requires a C++ compiler")
class SunFaceMetadataTest(unittest.TestCase):
    def test_both_backends_preserve_legacy_offsets_and_six_oriented_faces(self):
        for relative in ("ir_sun_projection.glsl", "metal/ir_sun_projection.metal"):
            with self.subTest(backend=relative), tempfile.TemporaryDirectory() as directory:
                code = "#include <cstdint>\nusing uint = std::uint32_t;\n"
                code += scalar_functions((SHADERS / relative).read_text())
                code += r"""
int main() {
    bool used[256] = {};
    for (int dx = -7; dx <= 7; ++dx) {
        for (int dy = -7; dy <= 7; ++dy) {
            const uint marker = (uint(dx & 15) << 4) | uint(dy & 15);
            if (sunWriteIsSurface(marker) || sunVoxelFaceId(marker) != -1) return 1;
            used[marker] = true;
        }
    }
    for (int view = 0; view < 2; ++view) {
        for (int face = 0; face < 6; ++face) {
            const uint marker = sunVoxelFaceMarker(face, view != 0);
            if (marker > 255 || used[marker] || marker == 0x88) return 2;
            used[marker] = true;
            for (uint depth : {0u, 1u, 524288u, 1048576u}) {
                const uint packed = (depth << 8) | marker;
                if ((packed >> 8) != depth || !sunWriteIsSurface(packed)) return 3;
                if (sunVoxelFaceId(packed) != face) return 4;
                if (sunVoxelFaceViewAligned(packed) != (view != 0)) return 5;
            }
        }
    }
    if (!sunWriteIsSurface(0x88) || sunVoxelFaceId(0x88) != -1) return 6;
    if (sunWriteIsSurface(0xFFFFFFFFu) || sunVoxelFaceId(0xFFFFFFFFu) != -1) return 7;
    return 0;
}
"""
                code = "#include <initializer_list>\n" + code
                source, binary = Path(directory) / "metadata.cpp", Path(directory) / "metadata"
                source.write_text(code)
                build = subprocess.run([COMPILER, "-std=c++17", str(source), "-o", str(binary)],
                                       capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stderr)
                run = subprocess.run([str(binary)], capture_output=True, text=True)
                self.assertEqual(run.returncode, 0, f"wire-contract check {run.returncode}")


if __name__ == "__main__":
    unittest.main()

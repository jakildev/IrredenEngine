"""Guard the GLSL gather interface shared by linked vertex/fragment stages."""

import re
import unittest
from pathlib import Path

SHADERS = Path(__file__).resolve().parents[2] / "engine/render/src/shaders"


def gather_block_tokens(source):
    source = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
    blocks = re.findall(
        r"layout\s*\([^)]*\)\s*uniform\s+FrameDataIsoTriangles\s*\{[^}]*\}\s*;",
        source,
    )
    if len(blocks) != 1:
        raise ValueError("expected exactly one gather uniform block")
    return re.findall(r"\w+|[^\s]", blocks[0])


class GatherUniformBlockTest(unittest.TestCase):
    def test_linked_stages_match(self):
        vertex = (SHADERS / "v_trixel_to_framebuffer.glsl").read_text()
        fragment = (SHADERS / "ir_trixel_to_framebuffer_body.glsl").read_text()
        self.assertEqual(gather_block_tokens(vertex), gather_block_tokens(fragment))

    def test_mismatch_controls(self):
        vertex = (SHADERS / "v_trixel_to_framebuffer.glsl").read_text()
        expected = gather_block_tokens(vertex)
        variants = {
            "member_name": vertex.replace("casterViewToWorld", "_detachedDepthAxisPad"),
            "member_type": vertex.replace("vec4 casterViewToWorld", "vec3 casterViewToWorld"),
            "binding": vertex.replace("binding = 3", "binding = 4"),
            "missing_member": vertex.replace("    int trixelSampleLayout;", ""),
        }
        for name, source in variants.items():
            with self.subTest(variant=name):
                self.assertNotEqual(source, vertex)
                self.assertNotEqual(gather_block_tokens(source), expected)
        with self.assertRaises(ValueError):
            gather_block_tokens(vertex.replace("FrameDataIsoTriangles", "WrongBlock"))


if __name__ == "__main__":
    unittest.main()

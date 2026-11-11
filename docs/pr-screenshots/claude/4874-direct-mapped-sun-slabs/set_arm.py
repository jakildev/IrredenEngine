#!/usr/bin/env python3
"""Set the slab arm in all three layout twins: set_arm.py <worktree> <slots> <direct 0|1>."""
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
slots, direct = int(sys.argv[2]), "true" if int(sys.argv[3]) else "false"
twins = {
    "engine/prefabs/irreden/render/sun_face_query_layout.hpp":
        (r"(constexpr std::uint32_t kSunDepthSlabSlots = )\d+u;", r"(constexpr bool kSunDepthSlabDirectMapped = )\w+;"),
    "engine/render/src/shaders/ir_sun_face_query_layout.glsl":
        (r"(const uint kSunDepthSlabSlots = )\d+u;", r"(const bool kSunDepthSlabDirectMapped = )\w+;"),
    "engine/render/src/shaders/metal/ir_sun_face_query_layout.metal":
        (r"(constant uint kSunDepthSlabSlots = )\d+u;", r"(constant bool kSunDepthSlabDirectMapped = )\w+;"),
}
for rel, (slot_re, direct_re) in twins.items():
    path = root / rel
    text = path.read_text()
    text, a = re.subn(slot_re, rf"\g<1>{slots}u;", text)
    text, b = re.subn(direct_re, rf"\g<1>{direct};", text)
    assert a == 1 and b == 1, (rel, a, b)
    path.write_text(text)
print(f"arm slots={slots} direct={direct}")

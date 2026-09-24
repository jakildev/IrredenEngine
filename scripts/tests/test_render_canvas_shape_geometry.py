"""Execute the canvas descriptor owner with a checked in-memory GPU resource adapter."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "engine/prefabs/irreden/render/components/canvas_shape_geometry.hpp"
COMPILER = shutil.which("c++")

ADAPTER = r"""
#pragma once
#include <bit>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>
#define IR_ASSERT(p, message) if (!(p)) throw std::runtime_error(message)
namespace IRMath {
inline std::uint32_t nextPowerOfTwo(std::uint32_t n) { return std::bit_ceil(n); }
}
namespace IRRender {
using ResourceId = unsigned;
constexpr int BUFFER_STORAGE_DYNAMIC = 1;
struct GPUShapeDescriptor { int identity; float position; };
struct GPUShapesFrameData { int shapeCount = 0; float visualYaw = 0; int density = 0; };
struct Buffer {
    std::vector<unsigned char> bytes;
    Buffer(const void*, std::size_t size, int) : bytes(size) {}
    void subData(std::ptrdiff_t offset, std::size_t size, const void* source) {
        if (offset < 0 || offset + size > bytes.size()) throw std::runtime_error("overflow");
        std::memcpy(bytes.data() + offset, source, size);
    }
};
inline std::map<ResourceId, std::unique_ptr<Buffer>> resources;
inline unsigned nextId = 1;
template<class T, class... Args> auto createResource(Args... args) {
    auto id = nextId++;
    auto p = std::make_unique<T>(args...);
    auto* raw = p.get(); resources[id] = std::move(p);
    return std::pair{id, raw};
}
template<class T> void destroyResource(ResourceId id) {
    if (resources.erase(id) != 1) throw std::runtime_error("double release");
}
}
"""

PROGRAM = r"""
#include "canvas_shape_geometry.hpp"
#include <array>
using namespace IRRender;
using IRComponents::CanvasShapeGeometry;
int main() {
    CanvasShapeGeometry a, b, empty;
    if (a.descriptors_.second || a.frameData_.shapeCount || a.capacity_) return 1;
    GPUShapesFrameData first{1, .4f, 3}, second{1, 1.2f, 8};
    std::array<GPUShapeDescriptor, 1> one{{{7, 2.5f}}}, other{{{91, -9.f}}};
    a.upload(one, first);
    const auto handle = a.descriptors_.first;
    b.upload(other, second);
    GPUShapeDescriptor retained{};
    std::memcpy(&retained, a.descriptors_.second->bytes.data(), sizeof(retained));
    if (retained.identity != 7 || retained.position != 2.5f) return 2;
    if (a.frameData_.visualYaw != .4f || a.frameData_.density != 3) return 3;
    if (a.descriptors_.first == b.descriptors_.first || resources.size() != 2) return 4;
    a.reset();
    if (a.frameData_.shapeCount != 0) return 5;
    if (a.descriptors_.first != handle || a.capacity_ != 1) return 6;
    a.upload(other, second);
    if (a.descriptors_.first != handle || nextId != 3) return 7;
    std::array<GPUShapeDescriptor, 3> larger{{{1, 1}, {2, 2}, {3, 3}}};
    first.shapeCount = 3;
    a.upload(larger, first);
    if (a.capacity_ != 4 || resources.contains(handle) || resources.size() != 2) return 8;
    const auto grown = a.descriptors_.first;
    a.upload(one, second);
    if (a.descriptors_.first != grown || a.capacity_ != 4 || a.frameData_.shapeCount != 1) return 9;
    a.upload({}, {});
    if (a.frameData_.shapeCount || a.descriptors_.first != grown) return 10;
    empty.upload({}, {});
    if (empty.descriptors_.second || resources.size() != 2) return 11;
    bool rejected = false;
    try { a.upload(one, first); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) return 12;
    a.onDestroy(); b.onDestroy(); empty.onDestroy();
    if (!resources.empty() || a.descriptors_.second || a.capacity_ ||
        a.frameData_.shapeCount) return 13;
}
"""


@unittest.skipUnless(COMPILER, "Canvas descriptor lifecycle requires a C++ compiler")
class CanvasShapeGeometryTest(unittest.TestCase):
    def test_lifetime_and_mutations(self):
        source = HEADER.read_text()
        variants = {
            "production": (source, 0),
            "stale_frame": (source.replace("frameData_ = {};", ""), 5),
            "lost_projection": (source.replace("frameData_ = frameData;", ""), 3),
            "unreleased_growth": (source.replace(
                "IRRender::destroyResource<IRRender::Buffer>(descriptors_.first);",
                "(void)0;", 1), 8),
        }
        for name, (header, expected) in variants.items():
            with self.subTest(variant=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                (root / "irreden").mkdir()
                (root / "irreden/ir_render.hpp").write_text(ADAPTER)
                (root / "canvas_shape_geometry.hpp").write_text(header)
                (root / "test.cpp").write_text(PROGRAM)
                built = subprocess.run(
                    [COMPILER, "-std=c++20", "-I", str(root), str(root / "test.cpp"),
                     "-o", str(root / "test")], capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stderr)
                self.assertEqual(subprocess.run([str(root / "test")]).returncode, expected)

    def test_system_publishes_after_tile_selection(self):
        system = (ROOT / "engine/prefabs/irreden/render/systems" /
                  "system_shapes_to_trixel.hpp").read_text()
        begin = system[system.index("void beginTick()"):system.index("void endTick()")]
        self.assertIn("textures.shapeGeometry_.reset()", begin)
        self.assertLess(system.index("if (tileCount == 0)"),
                        system.index("shapeGeometry_.upload(gpuShapes, frameData_)"))
        self.assertLess(system.index("frameData_.tileGridX = gridX"),
                        system.index("shapeGeometry_.upload(gpuShapes, frameData_)"))
        self.assertIn("canvasTextures.shapeGeometry_.descriptors_.second", system)
        self.assertIn("shapeDescriptors->bindBase", system)
        self.assertNotIn('"ShapeDescriptorBuffer"', system)
        textures = HEADER.with_name("component_triangle_canvas_textures.hpp").read_text()
        self.assertIn("shapeGeometry_.onDestroy()", textures)


if __name__ == "__main__":
    unittest.main()

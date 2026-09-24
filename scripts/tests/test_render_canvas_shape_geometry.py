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
struct ivec2 { int x, y; ivec2(int v=0):x(v),y(v){} ivec2(int a,int b):x(a),y(b){} };
inline std::uint32_t nextPowerOfTwo(std::uint32_t n) { return std::bit_ceil(n); }
}
namespace IRRender {
using ResourceId = unsigned;
constexpr int BUFFER_STORAGE_DYNAMIC = 1;
struct ShapeTileDescriptor { int shapeIndex; int pad; IRMath::ivec2 tileIsoOrigin; };
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
enum class BarrierType { ALL };
struct Device {
    bool ordered = false;
    void memoryBarrier(BarrierType) { ordered = true; }
    void fillBuffer(Buffer* b, std::size_t bytes, unsigned char value) {
        if (!ordered || bytes > b->bytes.size()) throw std::runtime_error("unordered fill");
        std::memset(b->bytes.data(), value, bytes); ordered = false;
    }
};
inline Device adapterDevice;
inline Device* device() { return &adapterDevice; }
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
int samples() {
    CanvasShapeGeometry a, b;
    std::array<ShapeTileDescriptor, 1> tile{{{7, 0, {13, -9}}}};
    a.uploadTiles(tile);
    a.prepareSampleOwners({2, 3});
    const auto ownerHandle = a.sampleOwners_.first;
    const auto tileHandle = a.tiles_.first;
    const std::uint32_t key = (17 * 3 + 2) * 2 + 1;
    a.sampleOwners_.second->subData(0, sizeof(key), &key);
    tile[0].shapeIndex = 91;
    b.uploadTiles(tile);
    b.prepareSampleOwners({3, 2});
    std::uint32_t retained;
    std::memcpy(&retained, a.sampleOwners_.second->bytes.data(), sizeof(retained));
    ShapeTileDescriptor source{};
    std::memcpy(&source, a.tiles_.second->bytes.data(), sizeof(source));
    if (retained != key || source.shapeIndex != 7 || source.tileIsoOrigin.y != -9) return 20;
    if (a.sampleOwners_.first == b.sampleOwners_.first || a.tiles_.first == b.tiles_.first)
        return 21;
    a.reset();
    if (a.tileCount_ || a.ownerSize_.x || a.ownerSize_.y) return 22;
    if (a.sampleOwners_.first != ownerHandle || a.tiles_.first != tileHandle) return 23;
    a.prepareSampleOwners({3, 2});
    if (a.sampleOwners_.first != ownerHandle || a.ownerSize_.x != 3 || a.ownerSize_.y != 2)
        return 24;
    for (auto byte : a.sampleOwners_.second->bytes) if (byte != 255) return 25;
    a.uploadTiles(tile);
    if (a.tiles_.first != tileHandle || a.tileCount_ != 1) return 26;
    std::array<ShapeTileDescriptor, 3> larger{};
    a.uploadTiles(larger);
    a.prepareSampleOwners({4, 4});
    if (resources.contains(tileHandle) || resources.contains(ownerHandle) ||
        a.tileCapacity_ != 4 || a.ownerCapacityBytes_ != 64) return 27;
    const auto grownOwner = a.sampleOwners_.first, grownTile = a.tiles_.first;
    a.prepareSampleOwners({1, 1});
    a.uploadTiles({});
    if (a.tileCount_ || a.sampleOwners_.first != grownOwner || a.tiles_.first != grownTile)
        return 28;
    bool rejected = false;
    try { a.prepareSampleOwners({0, 3}); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) return 29;
    a.onDestroy(); b.onDestroy();
    if (!resources.empty() || a.sampleOwners_.second || a.tiles_.second ||
        a.ownerCapacityBytes_ || a.tileCapacity_ || a.ownerSize_.x || a.tileCount_) return 30;
    return 0;
}
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
    try { return samples(); } catch (const std::runtime_error&) { return 31; }
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
            "unordered_clear": (source.replace(
                "IRRender::device()->memoryBarrier(IRRender::BarrierType::ALL);", ""), 31),
            "stale_tiles": (source.replace("        tileCount_ = 0;", "", 1), 22),
            "stale_owners": (source.replace(
                "IRRender::device()->fillBuffer(sampleOwners_.second, bytes, 0xFF);", ""), 25),
            "unreleased_growth": (source.replace(
                "IRRender::destroyResource<IRRender::Buffer>(descriptors_.first);",
                "(void)0;", 1), 8),
        }
        for name, (header, expected) in variants.items():
            with self.subTest(variant=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                (root / "irreden").mkdir()
                (root / "irreden/ir_render.hpp").write_text(ADAPTER)
                (root / "irreden/ir_math.hpp").write_text("#pragma once\n")
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
        self.assertNotIn('"ShapeTileDescriptorBuffer"', system)
        self.assertNotIn("winnerBuffer_", system)
        self.assertIn("geometry.uploadTiles(tiles)", system)
        self.assertIn("shapeGeometry_.prepareSampleOwners(canvasTextures.size_)", system)
        textures = HEADER.with_name("component_triangle_canvas_textures.hpp").read_text()
        self.assertIn("shapeGeometry_.onDestroy()", textures)


if __name__ == "__main__":
    unittest.main()

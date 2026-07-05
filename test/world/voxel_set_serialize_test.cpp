// Unit tests for SaveSerialize<C_VoxelSetNew> — persist P6 / W-10 (#2217).
//
// The serializer round-trips a C_VoxelSetNew's canonical, pool-independent
// content ({size_, boundsMin, per-voxel C_Voxel records, owning canvas id})
// and reconstructs the set in STAGED mode (numVoxels_ == 0, pendingVoxels_
// populated) with zero pool interaction — the exact contract the loader's
// mutation-free validate pass and the post-load attachToCanvas seed pass rely
// on. These are headless: staged construction and the serializer touch no
// voxel pool, so no RenderManager / canvas is needed. The pool-seed half
// (attachToCanvas, boundsMin recovery from a live span) is exercised by the
// persist_roundtrip render demo, which has a real render context.

#include <irreden/voxel/voxel_set_serialize.hpp>

#include <irreden/asset/binary_io.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

using namespace IRComponents;
using IRWorld::SaveSerialize;

namespace {

std::vector<C_Voxel> makeVoxels(int count) {
    std::vector<C_Voxel> voxels;
    voxels.reserve(count);
    for (int i = 0; i < count; ++i) {
        C_Voxel voxel;
        voxel.color_ = IRMath::Color{
            static_cast<std::uint8_t>(i * 7 + 1),
            static_cast<std::uint8_t>(i * 3 + 2),
            static_cast<std::uint8_t>(i * 11 + 5),
            static_cast<std::uint8_t>(i % 2 == 0 ? 255 : 0)
        };
        voxels.push_back(voxel);
    }
    return voxels;
}

C_VoxelSetNew serializeThenRead(const C_VoxelSetNew &set, IRAsset::Result<C_VoxelSetNew> &resOut) {
    IRAsset::MemoryBinaryWriter writer;
    SaveSerialize<C_VoxelSetNew>::write(writer, set);
    IRAsset::MemoryBinaryReader reader(
        writer.buffer().data(),
        writer.buffer().size(),
        "voxel_set_serialize_test"
    );
    resOut = SaveSerialize<C_VoxelSetNew>::read(reader);
    return resOut.value_;
}

} // namespace

// A staged set round-trips its size, boundsMin, canvas id, and every voxel
// record byte-for-byte, landing back in staged mode.
TEST(VoxelSetSerialize, StagedRoundTrip) {
    const IRMath::ivec3 size{2, 1, 3}; // 6 voxels
    const IRMath::ivec3 boundsMin{-4, 7, 2};
    const IREntity::EntityId canvas = 12345;
    const std::vector<C_Voxel> voxels = makeVoxels(6);

    C_VoxelSetNew set{C_VoxelSetNew::StagedInit{}, size, boundsMin, voxels, canvas};
    ASSERT_EQ(set.numVoxels_, 0);
    ASSERT_EQ(set.recordCount(), 6u);

    IRAsset::Result<C_VoxelSetNew> res;
    const C_VoxelSetNew out = serializeThenRead(set, res);
    ASSERT_TRUE(res.ok());

    EXPECT_EQ(out.size_.x, size.x);
    EXPECT_EQ(out.size_.y, size.y);
    EXPECT_EQ(out.size_.z, size.z);
    EXPECT_EQ(out.pendingBoundsMin_.x, boundsMin.x);
    EXPECT_EQ(out.pendingBoundsMin_.y, boundsMin.y);
    EXPECT_EQ(out.pendingBoundsMin_.z, boundsMin.z);
    EXPECT_EQ(out.canvasEntity_, canvas);
    // Reconstructed in staged mode, never pool-resident.
    EXPECT_EQ(out.numVoxels_, 0);
    ASSERT_EQ(out.pendingVoxels_.size(), 6u);
    ASSERT_EQ(out.recordCount(), 6u);
    for (std::size_t i = 0; i < voxels.size(); ++i) {
        EXPECT_EQ(0, std::memcmp(&out.pendingVoxels_[i], &voxels[i], sizeof(C_Voxel)))
            << "voxel record " << i << " differs after round-trip";
    }
}

// An empty (zero-voxel) set round-trips without reading past the buffer.
TEST(VoxelSetSerialize, EmptySetRoundTrip) {
    C_VoxelSetNew set{C_VoxelSetNew::StagedInit{}, IRMath::ivec3(0), IRMath::ivec3(0), {}, 0};
    ASSERT_EQ(set.recordCount(), 0u);

    IRAsset::Result<C_VoxelSetNew> res;
    const C_VoxelSetNew out = serializeThenRead(set, res);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(out.recordCount(), 0u);
    EXPECT_TRUE(out.pendingVoxels_.empty());
    EXPECT_EQ(out.numVoxels_, 0);
}

// Same set serialized twice is byte-identical (world-snapshot determinism).
TEST(VoxelSetSerialize, WriteIsDeterministic) {
    const std::vector<C_Voxel> voxels = makeVoxels(12);
    C_VoxelSetNew set{
        C_VoxelSetNew::StagedInit{},
        IRMath::ivec3(3, 2, 2),
        IRMath::ivec3(1, 2, 3),
        voxels,
        99
    };

    IRAsset::MemoryBinaryWriter a;
    IRAsset::MemoryBinaryWriter b;
    SaveSerialize<C_VoxelSetNew>::write(a, set);
    SaveSerialize<C_VoxelSetNew>::write(b, set);
    ASSERT_EQ(a.buffer().size(), b.buffer().size());
    EXPECT_EQ(0, std::memcmp(a.buffer().data(), b.buffer().data(), a.buffer().size()));
}

// Build a synthetic *non-staged* (pool-resident) set from public fields backed
// by caller-owned storage — no voxel pool required. `voxels_` / `positions_`
// point at the passed vectors; `rotationSourceVoxels_` (when non-empty) marks
// the set as GRID-rotating. Mirrors the state REBUILD_GRID_VOXELS leaves a set
// in without needing a render context.
C_VoxelSetNew makePoolResidentSet(
    const IRMath::ivec3 &size,
    const IRMath::ivec3 &boundsMin,
    IREntity::EntityId canvas,
    std::vector<C_Voxel> &spanBacking,
    std::vector<IRRender::VoxelGpuPosition> &positionsBacking,
    std::vector<C_Voxel> rotationSource
) {
    C_VoxelSetNew set; // pool-free default ctor
    set.numVoxels_ = static_cast<int>(spanBacking.size());
    set.size_ = size;
    set.canvasEntity_ = canvas;
    set.voxels_ = std::span<C_Voxel>(spanBacking);
    // Non-staged boundsMin is recovered from positions_[0].pos_.
    positionsBacking[0].pos_ = IRMath::vec3(boundsMin);
    set.positions_ = std::span<IRRender::VoxelGpuPosition>(positionsBacking);
    set.rotationSourceVoxels_ = std::move(rotationSource);
    return set;
}

// A GRID-mode set captured mid-rotation persists its AUTHORED voxel records
// (rotationSourceVoxels_), not the derived dest-cell arrangement sitting in
// the pool span. Reloading a mid-spin save must render the source, not the
// resampled frame. boundsMin is still recovered from positions_[0].
TEST(VoxelSetSerialize, GridMidRotationPersistsAuthoredNotDerived) {
    const IRMath::ivec3 size{2, 1, 3}; // 6 voxels
    const IRMath::ivec3 boundsMin{-4, 7, 2};
    const IREntity::EntityId canvas = 12345;

    const std::vector<C_Voxel> authored = makeVoxels(6);
    // The pool span mid-spin: a deliberately different (derived) arrangement so
    // a byte-compare distinguishes which source the serializer read from.
    std::vector<C_Voxel> derived = makeVoxels(6);
    for (C_Voxel &v : derived) {
        v.color_.red_ = 200;
        v.color_.green_ = 1;
    }
    std::vector<IRRender::VoxelGpuPosition> positions(6);

    C_VoxelSetNew set = makePoolResidentSet(size, boundsMin, canvas, derived, positions, authored);
    ASSERT_TRUE(set.pendingVoxels_.empty()); // non-staged
    ASSERT_EQ(set.recordCount(), 6u);

    IRAsset::Result<C_VoxelSetNew> res;
    const C_VoxelSetNew out = serializeThenRead(set, res);
    ASSERT_TRUE(res.ok());

    EXPECT_EQ(out.pendingBoundsMin_.x, boundsMin.x);
    EXPECT_EQ(out.pendingBoundsMin_.y, boundsMin.y);
    EXPECT_EQ(out.pendingBoundsMin_.z, boundsMin.z);
    EXPECT_EQ(out.canvasEntity_, canvas);
    ASSERT_EQ(out.pendingVoxels_.size(), 6u);
    for (std::size_t i = 0; i < authored.size(); ++i) {
        EXPECT_EQ(0, std::memcmp(&out.pendingVoxels_[i], &authored[i], sizeof(C_Voxel)))
            << "record " << i << " must match the authored source snapshot";
        EXPECT_NE(0, std::memcmp(&out.pendingVoxels_[i], &derived[i], sizeof(C_Voxel)))
            << "record " << i << " must NOT persist the derived mid-rotation span";
    }
}

// A non-rotated pool-resident set (no rotationSourceVoxels_ snapshot) persists
// straight from its `voxels_` span — the fast path the mid-rotation guard falls
// back to.
TEST(VoxelSetSerialize, PoolResidentNonRotatedPersistsSpan) {
    const IRMath::ivec3 size{2, 2, 2}; // 8 voxels
    const IRMath::ivec3 boundsMin{3, -1, 5};
    const IREntity::EntityId canvas = 777;

    std::vector<C_Voxel> span = makeVoxels(8);
    std::vector<IRRender::VoxelGpuPosition> positions(8);

    C_VoxelSetNew set =
        makePoolResidentSet(size, boundsMin, canvas, span, positions, /*rotationSource=*/{});
    ASSERT_TRUE(set.rotationSourceVoxels_.empty());
    ASSERT_EQ(set.recordCount(), 8u);

    IRAsset::Result<C_VoxelSetNew> res;
    const C_VoxelSetNew out = serializeThenRead(set, res);
    ASSERT_TRUE(res.ok());

    EXPECT_EQ(out.pendingBoundsMin_.x, boundsMin.x);
    EXPECT_EQ(out.pendingBoundsMin_.y, boundsMin.y);
    EXPECT_EQ(out.pendingBoundsMin_.z, boundsMin.z);
    ASSERT_EQ(out.pendingVoxels_.size(), 8u);
    for (std::size_t i = 0; i < span.size(); ++i) {
        EXPECT_EQ(0, std::memcmp(&out.pendingVoxels_[i], &span[i], sizeof(C_Voxel)))
            << "record " << i << " must round-trip the pool span";
    }
}

// A truncated buffer surfaces a read error instead of over-reading.
TEST(VoxelSetSerialize, TruncatedReadFails) {
    const std::vector<C_Voxel> voxels = makeVoxels(8);
    C_VoxelSetNew
        set{C_VoxelSetNew::StagedInit{}, IRMath::ivec3(2, 2, 2), IRMath::ivec3(0), voxels, 7};

    IRAsset::MemoryBinaryWriter writer;
    SaveSerialize<C_VoxelSetNew>::write(writer, set);
    // Chop the payload mid-record.
    const std::size_t truncated = writer.buffer().size() - 10;
    IRAsset::MemoryBinaryReader reader(writer.buffer().data(), truncated, "truncated");
    IRAsset::Result<C_VoxelSetNew> res = SaveSerialize<C_VoxelSetNew>::read(reader);
    EXPECT_FALSE(res.ok());
}

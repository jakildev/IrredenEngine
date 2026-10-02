// Unit tests for SaveSerialize<C_VoxelSetNew>.
//
// The serializer round-trips a C_VoxelSetNew's canonical, pool-independent
// content ({size_, boundsMin, authored LOD band, per-voxel C_Voxel records,
// owning canvas id}) and reconstructs the set in STAGED mode (numVoxels_ == 0, pendingVoxels_
// populated) with zero pool interaction — the exact contract the loader's
// mutation-free validate pass and the post-load attachToCanvas seed pass rely
// on. These are headless: staged construction and the serializer touch no
// voxel pool, so no RenderManager / canvas is needed. The pool-seed half
// (attachToCanvas, boundsMin recovery from a live span) is exercised by the
// persist_roundtrip render demo, which has a real render context.

#include <irreden/voxel/voxel_set_serialize.hpp>

#include <irreden/asset/binary_io.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <stdexcept>
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

TEST(VoxelSetSerialize, AuthoredLodBandRoundTripsWithoutTransientGates) {
    const std::vector<C_Voxel> voxels = makeVoxels(1);
    C_VoxelSetNew set{C_VoxelSetNew::StagedInit{}, IRMath::ivec3(1), IRMath::ivec3(0), voxels, 9};
    set.lodMin_ = IRRender::LodLevel::LOD_3;
    set.lodMax_ = IRRender::LodLevel::LOD_2;
    set.visible_ = false;
    set.lodCulled_ = true;

    IRAsset::Result<C_VoxelSetNew> res;
    const C_VoxelSetNew out = serializeThenRead(set, res);
    ASSERT_TRUE(res.ok());

    EXPECT_EQ(out.lodMin_, IRRender::LodLevel::LOD_3);
    EXPECT_EQ(out.lodMax_, IRRender::LodLevel::LOD_2);
    EXPECT_TRUE(out.visible_);
    EXPECT_FALSE(out.lodCulled_);
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

// ---------------------------------------------------------------------------
// Versioned scalar persistence
// ---------------------------------------------------------------------------

namespace {

std::vector<std::uint8_t> makeRetiredPayload(
    IRMath::ivec3 size,
    IRMath::ivec3 boundsMin,
    IREntity::EntityId canvas,
    std::optional<EntityAnchor> anchor,
    const std::vector<C_Voxel> &voxels
) {
    IRAsset::MemoryBinaryWriter w;
    w.writeI32(size.x);
    w.writeI32(size.y);
    w.writeI32(size.z);
    w.writeI32(boundsMin.x);
    w.writeI32(boundsMin.y);
    w.writeI32(boundsMin.z);
    w.writeU64(static_cast<std::uint64_t>(canvas));
    if (anchor.has_value()) {
        w.writeU8(static_cast<std::uint8_t>(*anchor));
    }
    w.writeVarUInt(voxels.size());
    for (const C_Voxel &voxel : voxels) {
        w.writeBytes(&voxel, sizeof(C_Voxel));
    }
    return w.buffer();
}

bool currentLayoutFaithfullyReads(
    const std::vector<std::uint8_t> &payload, const std::vector<C_Voxel> &voxels
) {
    IRAsset::MemoryBinaryReader reader(payload.data(), payload.size(), "retired-at-current");
    IRAsset::Result<C_VoxelSetNew> res = SaveSerialize<C_VoxelSetNew>::read(reader);

    if (!res.ok() || res.value_.pendingVoxels_.size() != voxels.size()) {
        return false;
    }
    for (std::size_t i = 0; i < voxels.size(); ++i) {
        if (std::memcmp(&res.value_.pendingVoxels_[i], &voxels[i], sizeof(C_Voxel)) != 0) {
            return false;
        }
    }
    return true;
}

void expectCurrentReadRejectsByteAtOffset(
    const C_VoxelSetNew &set, std::size_t offset, std::uint8_t expected
) {
    IRAsset::MemoryBinaryWriter writer;
    SaveSerialize<C_VoxelSetNew>::write(writer, set);
    std::vector<std::uint8_t> bytes = writer.buffer();

    ASSERT_GT(bytes.size(), offset);
    ASSERT_EQ(bytes[offset], expected);
    bytes[offset] = static_cast<std::uint8_t>(0xEE);

    IRAsset::MemoryBinaryReader reader(bytes.data(), bytes.size(), "bad-tag");
    IRAsset::Result<C_VoxelSetNew> res = SaveSerialize<C_VoxelSetNew>::read(reader);
    EXPECT_FALSE(res.ok());
}

} // namespace

// The anchor survives the round trip, and — the load-bearing half — the
// reconstructed set's local ORIGIN comes back exactly, which `boundsMin`
// alone cannot express: GROUND's z origin is half-integer for every size.
TEST(VoxelSetSerialize, GroundAnchorRoundTripsIncludingItsFractionalOrigin) {
    const IRMath::ivec3 size{2, 2, 3};
    const IRMath::ivec3 boundsMin{0, 0, 0};
    const std::vector<C_Voxel> voxels = makeVoxels(size.x * size.y * size.z);

    C_VoxelSetNew
        set{C_VoxelSetNew::StagedInit{}, size, boundsMin, voxels, 77, EntityAnchor::GROUND};
    ASSERT_EQ(set.anchor_, EntityAnchor::GROUND);

    IRAsset::Result<C_VoxelSetNew> res;
    const C_VoxelSetNew out = serializeThenRead(set, res);
    ASSERT_TRUE(res.ok());

    EXPECT_EQ(out.anchor_, EntityAnchor::GROUND);
    // The origin the post-load seed pass will place voxel (0,0,0) at.
    const IRMath::vec3 origin = out.stagedOrigin();
    const IRMath::vec3 expected = anchorOffset(EntityAnchor::GROUND, size);
    EXPECT_FLOAT_EQ(origin.x, expected.x);
    EXPECT_FLOAT_EQ(origin.y, expected.y);
    EXPECT_FLOAT_EQ(origin.z, expected.z);
    // Concretely: half-integer in z, which the ivec3 boundsMin cannot hold.
    EXPECT_FLOAT_EQ(origin.z, -2.5f);
}

// CORNER is the anchor whose origin IS boundsMin, so it must keep reading
// through boundsMin rather than through the mode — otherwise every
// dense-authored set would silently move to the origin on load.
TEST(VoxelSetSerialize, CornerAnchorStillSeedsFromBoundsMin) {
    const IRMath::ivec3 size{2, 1, 3};
    const IRMath::ivec3 boundsMin{-4, 7, 2};
    const std::vector<C_Voxel> voxels = makeVoxels(6);

    C_VoxelSetNew set{C_VoxelSetNew::StagedInit{}, size, boundsMin, voxels, 5};
    IRAsset::Result<C_VoxelSetNew> res;
    const C_VoxelSetNew out = serializeThenRead(set, res);
    ASSERT_TRUE(res.ok());

    EXPECT_EQ(out.anchor_, EntityAnchor::CORNER);
    const IRMath::vec3 origin = out.stagedOrigin();
    EXPECT_FLOAT_EQ(origin.x, static_cast<float>(boundsMin.x));
    EXPECT_FLOAT_EQ(origin.y, static_cast<float>(boundsMin.y));
    EXPECT_FLOAT_EQ(origin.z, static_cast<float>(boundsMin.z));
}

// A v1 record has no anchor byte. The migrator must read the shorter layout
// and default to CORNER — reading v1 bytes at the current layout would consume the
// first voxel record's leading byte as the anchor and shear every record.
TEST(VoxelSetSerialize, V1MigratorReadsPreAnchorLayoutAsCorner) {
    const IRMath::ivec3 size{2, 1, 3};
    const IRMath::ivec3 boundsMin{-4, 7, 2};
    const std::vector<C_Voxel> voxels = makeVoxels(6);
    const std::vector<std::uint8_t> payload =
        makeRetiredPayload(size, boundsMin, 12345, std::nullopt, voxels);

    const auto migrators = IRWorld::SaveMigration<C_VoxelSetNew>::migrators();
    ASSERT_EQ(migrators.size(), 2u);
    EXPECT_EQ(migrators[0].first, 1u);
    EXPECT_EQ(migrators[1].first, 2u);

    IRAsset::MemoryBinaryReader reader(payload.data(), payload.size(), "v1");
    const auto v1 = std::find_if(migrators.begin(), migrators.end(), [](const auto &entry) {
        return entry.first == 1u;
    });
    ASSERT_NE(v1, migrators.end());
    IRAsset::Result<C_VoxelSetNew> res = v1->second(reader);
    ASSERT_TRUE(res.ok());

    const C_VoxelSetNew &out = res.value_;
    EXPECT_EQ(out.anchor_, EntityAnchor::CORNER);
    EXPECT_EQ(out.lodMin_, IRRender::LodLevel::LOD_4);
    EXPECT_EQ(out.lodMax_, IRRender::LodLevel::LOD_0);
    EXPECT_EQ(out.pendingBoundsMin_.x, boundsMin.x);
    EXPECT_EQ(out.pendingBoundsMin_.z, boundsMin.z);
    ASSERT_EQ(out.pendingVoxels_.size(), voxels.size());
    // Byte-exact records prove the field stream did not shear by one byte.
    for (std::size_t i = 0; i < voxels.size(); ++i) {
        EXPECT_EQ(0, std::memcmp(&out.pendingVoxels_[i], &voxels[i], sizeof(C_Voxel)))
            << "voxel record " << i << " sheared";
    }
}

// Discrimination check for the test above: the SAME v1 bytes read at the
// CURRENT layout must NOT come back clean. Without this, the migrator test
// would pass even if v1 and v2 happened to be compatible.
TEST(VoxelSetSerialize, V1BytesReadAtCurrentLayoutDoNotRoundTrip) {
    const IRMath::ivec3 size{2, 1, 3};
    const std::vector<C_Voxel> voxels = makeVoxels(6);
    const std::vector<std::uint8_t> payload =
        makeRetiredPayload(size, IRMath::ivec3{-4, 7, 2}, 12345, std::nullopt, voxels);

    // Either the read fails outright, or it "succeeds" with sheared records.
    // Both are the wrong answer; what must NOT happen is a faithful decode.
    EXPECT_FALSE(currentLayoutFaithfullyReads(payload, voxels))
        << "v1 bytes decoded cleanly at the current layout — the migrator test proves nothing";
}

TEST(VoxelSetSerialize, V2MigratorDefaultsLodBandWithoutShearingRecords) {
    const IRMath::ivec3 size{2, 1, 3};
    const IRMath::ivec3 boundsMin{-4, 7, 2};
    const std::vector<C_Voxel> voxels = makeVoxels(6);
    const std::vector<std::uint8_t> payload =
        makeRetiredPayload(size, boundsMin, 12345, EntityAnchor::GROUND, voxels);

    const auto migrators = IRWorld::SaveMigration<C_VoxelSetNew>::migrators();
    IRAsset::MemoryBinaryReader reader(payload.data(), payload.size(), "v2");
    const auto v2 = std::find_if(migrators.begin(), migrators.end(), [](const auto &entry) {
        return entry.first == 2u;
    });
    ASSERT_NE(v2, migrators.end());
    IRAsset::Result<C_VoxelSetNew> res = v2->second(reader);
    ASSERT_TRUE(res.ok());

    const C_VoxelSetNew &out = res.value_;
    EXPECT_EQ(out.anchor_, EntityAnchor::GROUND);
    EXPECT_EQ(out.lodMin_, IRRender::LodLevel::LOD_4);
    EXPECT_EQ(out.lodMax_, IRRender::LodLevel::LOD_0);
    ASSERT_EQ(out.pendingVoxels_.size(), voxels.size());
    for (std::size_t i = 0; i < voxels.size(); ++i) {
        EXPECT_EQ(0, std::memcmp(&out.pendingVoxels_[i], &voxels[i], sizeof(C_Voxel)))
            << "voxel record " << i << " sheared";
    }
}

TEST(VoxelSetSerialize, V2BytesReadAtCurrentLayoutDoNotRoundTrip) {
    const IRMath::ivec3 size{2, 1, 3};
    const std::vector<C_Voxel> voxels = makeVoxels(6);
    const std::vector<std::uint8_t> payload =
        makeRetiredPayload(size, IRMath::ivec3{-4, 7, 2}, 12345, EntityAnchor::GROUND, voxels);

    EXPECT_FALSE(currentLayoutFaithfullyReads(payload, voxels))
        << "v2 bytes decoded cleanly at the current layout — the migrator test proves nothing";
}

// A corrupt / newer-writer anchor byte must fail the load rather than falling
// through anchorOffset's switch and silently placing the set at CORNER.
TEST(VoxelSetSerialize, OutOfRangeAnchorByteFailsTheRead) {
    const IRMath::ivec3 size{1, 1, 1};
    const std::vector<C_Voxel> voxels = makeVoxels(1);
    C_VoxelSetNew set{C_VoxelSetNew::StagedInit{}, size, IRMath::ivec3{0, 0, 0}, voxels, 1};

    // The anchor byte sits right after 6 x i32 + 1 x u64.
    constexpr std::size_t kAnchorOffset = 6u * sizeof(std::int32_t) + sizeof(std::uint64_t);
    expectCurrentReadRejectsByteAtOffset(
        set,
        kAnchorOffset,
        static_cast<std::uint8_t>(EntityAnchor::CORNER)
    );
}

TEST(VoxelSetSerialize, OutOfRangeLodByteFailsTheRead) {
    const IRMath::ivec3 size{1, 1, 1};
    const std::vector<C_Voxel> voxels = makeVoxels(1);
    C_VoxelSetNew set{C_VoxelSetNew::StagedInit{}, size, IRMath::ivec3(0), voxels, 1};

    constexpr std::size_t kAnchorOffset = 6u * sizeof(std::int32_t) + sizeof(std::uint64_t);
    constexpr std::size_t kLodMinOffset = kAnchorOffset + sizeof(std::uint8_t);
    expectCurrentReadRejectsByteAtOffset(
        set,
        kLodMinOffset,
        static_cast<std::uint8_t>(IRRender::LodLevel::LOD_4)
    );
    expectCurrentReadRejectsByteAtOffset(
        set,
        kLodMinOffset + sizeof(std::uint8_t),
        static_cast<std::uint8_t>(IRRender::LodLevel::LOD_0)
    );
}

TEST(VoxelSetSerialize, OutOfRangeLodBandFailsTheWrite) {
    const std::vector<C_Voxel> voxels = makeVoxels(1);
    C_VoxelSetNew set{C_VoxelSetNew::StagedInit{}, IRMath::ivec3(1), IRMath::ivec3(0), voxels, 1};

    set.lodMin_ = static_cast<IRRender::LodLevel>(5u);
    IRAsset::MemoryBinaryWriter invalidMinWriter;
    EXPECT_THROW(SaveSerialize<C_VoxelSetNew>::write(invalidMinWriter, set), std::runtime_error);

    set.lodMin_ = IRRender::LodLevel::LOD_4;
    set.lodMax_ = static_cast<IRRender::LodLevel>(5u);
    IRAsset::MemoryBinaryWriter invalidMaxWriter;
    EXPECT_THROW(SaveSerialize<C_VoxelSetNew>::write(invalidMaxWriter, set), std::runtime_error);
}

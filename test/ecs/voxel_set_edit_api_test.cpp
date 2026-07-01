#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

// Covers the encapsulating raw-edit API on C_VoxelSetNew (#2165, head of epic
// #2164): `editVoxels` / `carve` / `resyncAfterRawEdits`. Each routes through
// the private `resyncDerivedState()` so a single call restores BOTH derived
// invariants a carve must maintain — the pool's per-slot active-mask AND the
// per-voxel face-occlusion bits. The recurring footgun (#2018/#2117/#2146) is a
// carve that syncs the active-mask but forgets `recomputeFaceOccupancy`, leaving
// the carved surface's newly-exposed faces occluded (renders black under the
// lit/rotated path). These tests assert both halves land correctly for a mixed
// active/inactive result — the case the active-mask-only path silently breaks.
//
// `carve` is a template member not instantiated anywhere in engine/creation code
// yet, so this file is also its first real type-check.
//
// Headless: the explicit `targetCanvas` arg routes pool ops through a specific
// canvas entity, bypassing the RenderManager active-canvas lookup (same fixture
// as voxel_set_target_canvas_test.cpp).

namespace {

using IRComponents::C_Voxel;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::kVoxelActiveMaskBits;
using IRMath::Color;
using IRMath::ivec3;
using IRMath::vec3;
namespace VoxelFlags = IRComponents::VoxelFlags;

class VoxelSetEditApiTest : public ::testing::Test {
  protected:
    IREntity::EntityManager m_entityManager;

    // Pool active-mask bit for the voxel at set-local flat index `localIndex`.
    static bool maskBit(const C_VoxelSetNew &set, const C_VoxelPool &pool, int localIndex) {
        const std::size_t slot = set.voxelStartIdx_ + static_cast<std::size_t>(localIndex);
        return (pool.getActiveMask()[slot / kVoxelActiveMaskBits] >>
                (slot % kVoxelActiveMaskBits)) &
               1u;
    }

    // Flat index of local grid cell (x,y,z) in the set's span (x-major).
    static int flatIndex(const C_VoxelSetNew &set, ivec3 cell) {
        return IRMath::index3DtoIndex1D(cell, set.size_);
    }
};

// carve() deactivates the x==0 slice of a solid 3x3x3 cube. The result is
// mixed (9 inactive / 18 active), so both derived halves must be re-derived:
// the mask must clear the carved slice, and the survivors on the new surface
// must expose the face that pointed at a now-carved neighbor.
TEST_F(VoxelSetEditApiTest, CarveDeactivatesSliceAndUpdatesMaskAndFaces) {
    const IREntity::EntityId canvas = IREntity::createEntity(C_VoxelPool{ivec3(8, 8, 8)});
    const IREntity::EntityId object =
        IREntity::createEntity(C_VoxelSetNew{ivec3(3, 3, 3), Color{200, 100, 50, 255}, false, canvas});

    auto &set = IREntity::getComponent<C_VoxelSetNew>(object);
    ASSERT_EQ(set.numVoxels_, 27);

    // localPos is the voxel's grid coordinate (positions_[i].pos_); not
    // centered, so cell (x,y,z) sits at vec3(x,y,z). Deactivate the x==0 face.
    set.carve([](vec3 localPos) { return localPos.x < 0.5f; });

    const auto &pool = IREntity::getComponent<C_VoxelPool>(canvas);

    // Mask: the 9 x==0 cells are cleared; every x>=1 cell stays active.
    for (int y = 0; y < 3; ++y) {
        for (int z = 0; z < 3; ++z) {
            EXPECT_FALSE(maskBit(set, pool, flatIndex(set, ivec3(0, y, z))));
            EXPECT_TRUE(maskBit(set, pool, flatIndex(set, ivec3(1, y, z))));
            EXPECT_TRUE(maskBit(set, pool, flatIndex(set, ivec3(2, y, z))));
        }
    }

    // Face occupancy on the survivor at (1,1,1): its -X neighbor (0,1,1) was
    // carved, so that face is now EXPOSED (bit clear); +X (2,1,1) and all four
    // Y/Z neighbors remain active, so those faces stay occluded (bit set).
    const std::uint8_t survivor = set.voxels_[flatIndex(set, ivec3(1, 1, 1))].flags_;
    EXPECT_EQ(survivor & VoxelFlags::kFaceOccludedNegX, 0u);
    EXPECT_NE(survivor & VoxelFlags::kFaceOccludedPosX, 0u);
    EXPECT_NE(survivor & VoxelFlags::kFaceOccludedNegY, 0u);
    EXPECT_NE(survivor & VoxelFlags::kFaceOccludedPosY, 0u);
    EXPECT_NE(survivor & VoxelFlags::kFaceOccludedNegZ, 0u);
    EXPECT_NE(survivor & VoxelFlags::kFaceOccludedPosZ, 0u);

    // A carved voxel carries no face-occlusion bits (recompute clears inactive
    // voxels to all-zero on the face-bit mask).
    const std::uint8_t carved = set.voxels_[flatIndex(set, ivec3(0, 1, 1))].flags_;
    EXPECT_EQ(carved & VoxelFlags::kFaceOccludedMask, 0u);
}

// editVoxels() activates a slice of an initially all-inactive set (the
// activate direction, complementing carve's deactivate). Exercises editVoxels
// directly and confirms the mask picks up newly-active slots and face
// occupancy reflects the post-edit active set (not the empty pre-edit state).
TEST_F(VoxelSetEditApiTest, EditVoxelsActivatesSliceAndUpdatesMaskAndFaces) {
    const IREntity::EntityId canvas = IREntity::createEntity(C_VoxelPool{ivec3(8, 8, 8)});
    // Transparent color -> the ctor marks the whole span inactive.
    const IREntity::EntityId object =
        IREntity::createEntity(C_VoxelSetNew{ivec3(3, 3, 3), Color{0, 0, 0, 0}, false, canvas});

    auto &set = IREntity::getComponent<C_VoxelSetNew>(object);
    ASSERT_EQ(set.numVoxels_, 27);

    // Activate only the x==2 slice (give the voxels an opaque color).
    set.editVoxels([](int, C_Voxel &voxel, vec3 localPos) {
        if (localPos.x > 1.5f) {
            voxel.color_ = Color{40, 180, 90, 255};
        }
    });

    const auto &pool = IREntity::getComponent<C_VoxelPool>(canvas);

    for (int y = 0; y < 3; ++y) {
        for (int z = 0; z < 3; ++z) {
            EXPECT_TRUE(maskBit(set, pool, flatIndex(set, ivec3(2, y, z))));
            EXPECT_FALSE(maskBit(set, pool, flatIndex(set, ivec3(1, y, z))));
            EXPECT_FALSE(maskBit(set, pool, flatIndex(set, ivec3(0, y, z))));
        }
    }

    // (2,1,1) is interior to the active x==2 slab: its -X neighbor (1,1,1) is
    // inactive so -X is exposed; its four in-slice Y/Z neighbors are active so
    // those faces are occluded. +X is a grid boundary (no neighbor) -> exposed.
    const std::uint8_t face = set.voxels_[flatIndex(set, ivec3(2, 1, 1))].flags_;
    EXPECT_EQ(face & VoxelFlags::kFaceOccludedNegX, 0u);
    EXPECT_EQ(face & VoxelFlags::kFaceOccludedPosX, 0u);
    EXPECT_NE(face & VoxelFlags::kFaceOccludedNegY, 0u);
    EXPECT_NE(face & VoxelFlags::kFaceOccludedPosY, 0u);
    EXPECT_NE(face & VoxelFlags::kFaceOccludedNegZ, 0u);
    EXPECT_NE(face & VoxelFlags::kFaceOccludedPosZ, 0u);
}

// resyncAfterRawEdits() is the escape hatch for a multi-pass edit that writes
// the raw voxels_ span directly. Deactivate the z==0 slice via a raw loop, then
// resync once, and confirm both derived halves land the same as the sugar paths.
TEST_F(VoxelSetEditApiTest, ResyncAfterRawEditsMatchesEncapsulatedPaths) {
    const IREntity::EntityId canvas = IREntity::createEntity(C_VoxelPool{ivec3(8, 8, 8)});
    const IREntity::EntityId object =
        IREntity::createEntity(C_VoxelSetNew{ivec3(3, 3, 3), Color{120, 120, 200, 255}, false, canvas});

    auto &set = IREntity::getComponent<C_VoxelSetNew>(object);
    ASSERT_EQ(set.numVoxels_, 27);

    // Raw span writes (the SDF-carve pattern) bypass every mutator's resync.
    for (int i = 0; i < set.numVoxels_; ++i) {
        if (set.positions_[i].pos_.z < 0.5f) {
            set.voxels_[i].deactivate();
        }
    }
    set.resyncAfterRawEdits();

    const auto &pool = IREntity::getComponent<C_VoxelPool>(canvas);

    for (int x = 0; x < 3; ++x) {
        for (int y = 0; y < 3; ++y) {
            EXPECT_FALSE(maskBit(set, pool, flatIndex(set, ivec3(x, y, 0))));
            EXPECT_TRUE(maskBit(set, pool, flatIndex(set, ivec3(x, y, 1))));
            EXPECT_TRUE(maskBit(set, pool, flatIndex(set, ivec3(x, y, 2))));
        }
    }

    // Survivor at (1,1,1): -Z neighbor (1,1,0) was carved -> -Z exposed; the
    // other five neighbors are active -> occluded.
    const std::uint8_t survivor = set.voxels_[flatIndex(set, ivec3(1, 1, 1))].flags_;
    EXPECT_EQ(survivor & VoxelFlags::kFaceOccludedNegZ, 0u);
    EXPECT_NE(survivor & VoxelFlags::kFaceOccludedPosZ, 0u);
    EXPECT_NE(survivor & VoxelFlags::kFaceOccludedNegX, 0u);
    EXPECT_NE(survivor & VoxelFlags::kFaceOccludedPosX, 0u);
    EXPECT_NE(survivor & VoxelFlags::kFaceOccludedNegY, 0u);
    EXPECT_NE(survivor & VoxelFlags::kFaceOccludedPosY, 0u);
}

} // namespace

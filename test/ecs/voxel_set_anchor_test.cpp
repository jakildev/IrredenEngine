#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/entity_anchor.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

// Covers the EntityAnchor convention and GROUND mode on C_VoxelSetNew (#2563).
//
// The anchor decides the offset baked into a set's LOCAL voxel positions at
// construction. That bake is the whole mechanism — the rasterize / render /
// cull / occupancy / picking paths read `positions_` and never branch on the
// mode — so these tests assert the baked positions directly, which is the
// state every one of those consumers actually sees.
//
// Two properties matter and are tested separately:
//   1. GROUND places the body center-XY / bottom-Z, so the ground-contact face
//      lands exactly at the entity's translation.
//   2. CORNER and CENTER are BYTE-IDENTICAL to the pre-#2563 behavior. The
//      legacy `bool centerAroundOrigin` ctor now delegates to the enum one, so
//      the byte-identity arms recompute the historical expression inline and
//      compare — a regression there would silently move every existing voxel
//      set in the tree.
//
// Headless: the explicit `targetCanvas` argument routes pool ops through a
// specific canvas entity, bypassing the RenderManager active-canvas lookup
// (same fixture shape as voxel_set_edit_api_test.cpp).

namespace {

using IRComponents::anchorLocalCenter;
using IRComponents::anchorOffset;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::EntityAnchor;
using IRMath::Color;
using IRMath::ivec3;
using IRMath::vec3;

constexpr float kEps = 1e-5f;

// The historical offset expression, before #2563 routed it through
// `anchorOffset`. Kept spelled out so the byte-identity arms below compare
// against the old code rather than against the new helper (which would make
// them tautological).
vec3 legacyOffset(bool centerAroundOrigin, ivec3 size) {
    return centerAroundOrigin
               ? vec3(-(size.x - 1) * 0.5f, -(size.y - 1) * 0.5f, -(size.z - 1) * 0.5f)
               : vec3(0.0f);
}

class VoxelSetAnchorTest : public ::testing::Test {
  protected:
    IREntity::EntityManager m_entityManager;

    static IREntity::EntityId makeCanvas() {
        return IREntity::createEntity(C_VoxelPool{ivec3(16, 16, 16)});
    }

    static void expectVec3Near(vec3 actual, vec3 expected, const char *what) {
        EXPECT_NEAR(actual.x, expected.x, kEps) << what << " .x";
        EXPECT_NEAR(actual.y, expected.y, kEps) << what << " .y";
        EXPECT_NEAR(actual.z, expected.z, kEps) << what << " .z";
    }

    static vec3 localPos(const C_VoxelSetNew &set, ivec3 cell) {
        return set.positions_[IRMath::index3DtoIndex1D(cell, set.size_)].pos_;
    }
};

// ---------------------------------------------------------------------------
// Pure-math arm: the helpers, with no pool involved.
// ---------------------------------------------------------------------------

TEST_F(VoxelSetAnchorTest, AnchorOffsetMatchesTheConventionForAllThreeModes) {
    const ivec3 size(2, 2, 2);

    expectVec3Near(anchorOffset(EntityAnchor::CORNER, size), vec3(0.0f), "CORNER offset");
    expectVec3Near(
        anchorOffset(EntityAnchor::CENTER, size),
        vec3(-0.5f, -0.5f, -0.5f),
        "CENTER offset"
    );
    // GROUND: center XY (same as CENTER on x/y), bottom Z at -(size.z - 0.5).
    expectVec3Near(
        anchorOffset(EntityAnchor::GROUND, size),
        vec3(-0.5f, -0.5f, -1.5f),
        "GROUND offset"
    );
}

TEST_F(VoxelSetAnchorTest, AnchorLocalCenterIsTheOffsetPlusHalfTheCellExtent) {
    // anchorLocalCenter must equal anchorOffset + (size - 1) * 0.5 per axis for
    // every mode — that identity is what lets a consumer swap a hard-coded
    // `(size - 1) * 0.5` for the helper without changing CORNER behavior.
    const ivec3 sizes[] = {ivec3(1, 1, 1), ivec3(2, 3, 4), ivec3(6, 6, 12), ivec3(5, 5, 5)};
    const EntityAnchor anchors[] =
        {EntityAnchor::CORNER, EntityAnchor::CENTER, EntityAnchor::GROUND};

    for (ivec3 size : sizes) {
        const vec3 halfCells((size.x - 1) * 0.5f, (size.y - 1) * 0.5f, (size.z - 1) * 0.5f);
        for (EntityAnchor anchor : anchors) {
            const vec3 expected = anchorOffset(anchor, size) + halfCells;
            expectVec3Near(anchorLocalCenter(anchor, size), expected, "localCenter identity");
        }
    }

    // The values the migrated consumers rely on, spelled out.
    const ivec3 size(6, 6, 12);
    expectVec3Near(
        anchorLocalCenter(EntityAnchor::CORNER, size),
        vec3(2.5f, 2.5f, 5.5f),
        "CORNER center"
    );
    expectVec3Near(anchorLocalCenter(EntityAnchor::CENTER, size), vec3(0.0f), "CENTER center");
    expectVec3Near(
        anchorLocalCenter(EntityAnchor::GROUND, size),
        vec3(0.0f, 0.0f, -6.0f),
        "GROUND center"
    );
}

// ---------------------------------------------------------------------------
// GROUND placement, as baked into the pool span.
// ---------------------------------------------------------------------------

TEST_F(VoxelSetAnchorTest, GroundBakesCenterXyBottomZIntoLocalPositions) {
    const IREntity::EntityId canvas = makeCanvas();
    const IREntity::EntityId object = IREntity::createEntity(
        C_VoxelSetNew{ivec3(2, 2, 2), Color{200, 100, 50, 255}, EntityAnchor::GROUND, canvas}
    );

    const auto &set = IREntity::getComponent<C_VoxelSetNew>(object);
    ASSERT_EQ(set.numVoxels_, 8);
    EXPECT_EQ(set.anchor_, EntityAnchor::GROUND);

    expectVec3Near(localPos(set, ivec3(0, 0, 0)), vec3(-0.5f, -0.5f, -1.5f), "GROUND cell(0,0,0)");
    expectVec3Near(localPos(set, ivec3(1, 1, 1)), vec3(0.5f, 0.5f, -0.5f), "GROUND cell(1,1,1)");
}

TEST_F(VoxelSetAnchorTest, GroundContactFaceSitsExactlyAtTheTranslation) {
    // The load-bearing property for the whole convention: for ANY size, the far
    // face of the last cell in z is at local z == 0, so an entity placed at
    // translation.z == floorSurfaceZ stands flush on that floor with no
    // per-caller half-height offset. Cell centers sit half a unit inside their
    // faces, hence the +0.5f.
    const ivec3 sizes[] = {ivec3(1, 1, 1), ivec3(2, 2, 2), ivec3(6, 6, 12), ivec3(3, 3, 7)};
    for (ivec3 size : sizes) {
        const vec3 offset = anchorOffset(EntityAnchor::GROUND, size);
        const float lastCellCenterZ = offset.z + static_cast<float>(size.z - 1);
        EXPECT_NEAR(lastCellCenterZ + 0.5f, 0.0f, kEps)
            << "ground-contact face for size.z=" << size.z;
        // ...and the top of the body is exactly `size.z` above it.
        EXPECT_NEAR(offset.z - 0.5f, -static_cast<float>(size.z), kEps)
            << "body top for size.z=" << size.z;
    }
}

// ---------------------------------------------------------------------------
// Byte-identity: the two legacy modes must not move.
// ---------------------------------------------------------------------------

TEST_F(VoxelSetAnchorTest, LegacyBoolCtorArmsMapToCornerAndCenter) {
    const IREntity::EntityId canvas = makeCanvas();
    const IREntity::EntityId cornerObj = IREntity::createEntity(
        C_VoxelSetNew{ivec3(2, 2, 2), Color{10, 20, 30, 255}, false, canvas}
    );
    const IREntity::EntityId centerObj =
        IREntity::createEntity(C_VoxelSetNew{ivec3(2, 2, 2), Color{10, 20, 30, 255}, true, canvas});

    EXPECT_EQ(IREntity::getComponent<C_VoxelSetNew>(cornerObj).anchor_, EntityAnchor::CORNER);
    EXPECT_EQ(IREntity::getComponent<C_VoxelSetNew>(centerObj).anchor_, EntityAnchor::CENTER);
}

TEST_F(VoxelSetAnchorTest, DefaultConstructedAnchorIsCorner) {
    // The staged / dense / default ctors don't take an anchor; their origin is
    // carried by boundsMin, and CORNER is the offset-free reading of that.
    const C_VoxelSetNew empty;
    EXPECT_EQ(empty.anchor_, EntityAnchor::CORNER);
}

TEST_F(VoxelSetAnchorTest, CornerAndCenterPositionsAreByteIdenticalToTheLegacyFormula) {
    // Odd AND even sizes: CENTER's origin is half-integer only on even axes, so
    // an odd/even pair is what distinguishes "still correct" from "correct by
    // parity coincidence".
    const ivec3 sizes[] = {ivec3(3, 3, 3), ivec3(2, 4, 2)};
    const IREntity::EntityId canvas = makeCanvas();

    for (ivec3 size : sizes) {
        for (bool centered : {false, true}) {
            const IREntity::EntityId object = IREntity::createEntity(
                C_VoxelSetNew{size, Color{99, 99, 99, 255}, centered, canvas}
            );
            const auto &set = IREntity::getComponent<C_VoxelSetNew>(object);
            ASSERT_EQ(set.numVoxels_, size.x * size.y * size.z);

            const vec3 expectedOffset = legacyOffset(centered, size);
            for (int x = 0; x < size.x; ++x) {
                for (int y = 0; y < size.y; ++y) {
                    for (int z = 0; z < size.z; ++z) {
                        const vec3 expected = vec3(x, y, z) + expectedOffset;
                        const vec3 actual = localPos(set, ivec3(x, y, z));
                        // Exact equality, not near: the new code must produce
                        // the identical float expression, not merely a close
                        // one — this is the byte-identity criterion.
                        EXPECT_FLOAT_EQ(actual.x, expected.x);
                        EXPECT_FLOAT_EQ(actual.y, expected.y);
                        EXPECT_FLOAT_EQ(actual.z, expected.z);
                    }
                }
            }
        }
    }
}

} // namespace

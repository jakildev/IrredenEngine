#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/entity_anchor.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/grid_rotation.hpp>
#include <irreden/voxel/systems/system_rebuild_detached_voxels.hpp>

#include <stdexcept>

// Covers the origin-centered guard on a DETACHED_REVOXELIZE pool (#2911).
//
// The defect: `SYSTEM_REBUILD_DETACHED_VOXELS`' conservative cull bound and the
// GPU inverse-resample (`DetachedRevoxelize::seedResidentLocals`) both rotate
// about the POOL ORIGIN and assume it is the body's center. A GROUND- or
// CORNER-anchored `C_VoxelSetNew` bakes an asymmetric offset into its composed
// locals, so the solid orbits its anchor instead of spinning in place — and
// nothing detected it, because the only assert on that path measures
// `halfCellAnchor` UNIFORMITY, which a GROUND set satisfies (its z offset is
// half-integer, so the residual is a uniform -0.5).
//
// Why the guard lives in this system and nowhere else: `RotationMode::setMode`
// and `Prefab::spawnPrefab` — the two sites the issue proposed — never bind a
// voxel set to a pool. Both allocate the canvas through `EntityCanvas::create`,
// which creates textures + size + name only; the DENSE `C_VoxelSetNew` is
// attached afterwards and allocates from the ACTIVE canvas, not the detached
// one. A guard there would compare `anchor_` against a canvas the set never
// renders through — a gate that passes by construction. This system, by
// contrast, ticks the canvas that owns the pool and gates on `reVoxelize_`, so
// every authoring path (the C++ `targetCanvas` argument, the Lua 4-arg ctor,
// and the post-load `attachToCanvas` seed) converges here on the pool's first
// re-voxelize frame. Do not re-open `setMode`.
//
// The guard deliberately fires on any NON-CENTERED pool rather than on the
// GROUND enum alone: CORNER orbits its min corner by the identical mechanism
// and the bound is equally wrong for it.
//
// Headless: the explicit `targetCanvas` argument routes pool ops through a
// specific canvas entity, bypassing the RenderManager active-canvas lookup, and
// the rebuild tick is pure GridRotation math plus the face-occupancy recompute
// — no render manager. A singleton pipeline group executes on the calling
// thread, and nothing in `engine/system/` catches, so the debug `IR_ASSERT`
// surfaces as a `std::runtime_error` out of `executePipeline`.

namespace {

using IRComponents::C_CanvasLocalRotation;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::EntityAnchor;
using IRMath::Color;
using IRMath::ivec3;
using IRMath::vec3;

constexpr Color kTestColor{255, 0, 0, 255};

// Identity quaternion. The C_CanvasLocalRotation default (0,0,0,0) is the
// "not a detached canvas" sentinel, not a rotation — PROPAGATE_CANVAS_ROTATION
// always overwrites it with a unit quat, so the fixture must too.
constexpr IRMath::vec4 kIdentityRotation{0.0f, 0.0f, 0.0f, 1.0f};

class RebuildDetachedVoxelsGuardTest : public testing::Test {
  protected:
    // EntityManager before SystemManager: the system manager's registration
    // reaches the entity manager, so it must outlive nothing and be built last.
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;

    RebuildDetachedVoxelsGuardTest()
        : m_entity_manager{}
        , m_system_manager{} {
        m_system_manager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::REBUILD_DETACHED_VOXELS>()}
        );
    }

    // A canvas entity owning a private voxel pool. `reVoxelize` picks the
    // DETACHED_REVOXELIZE path (true) or the forward-scatter path (false) the
    // tick early-returns on.
    static IREntity::EntityId makeCanvas(bool reVoxelize) {
        const IREntity::EntityId canvas = IREntity::createEntity(
            C_VoxelPool{ivec3(16, 16, 16)},
            C_CanvasLocalRotation{}
        );
        auto &rotation = IREntity::getComponent<C_CanvasLocalRotation>(canvas);
        rotation.rotation_ = kIdentityRotation;
        rotation.reVoxelize_ = reVoxelize;
        return canvas;
    }

    void tick() { m_system_manager.executePipeline(IRTime::Events::UPDATE); }

    static bool hasBound(IREntity::EntityId canvas) {
        return IREntity::getComponent<C_VoxelPool>(canvas).hasStaticReVoxelizeBound();
    }

    // Per-axis (min + max) of a set's own composed locals — the same quantity
    // the guard measures, read straight off the component.
    static vec3 asymmetryOf(const C_VoxelSetNew &set) {
        return IRPrefab::GridRotation::poolOriginAsymmetry(
            set.numVoxels_,
            [&](int i) { return set.positions_[i].pos_ + set.positionOffsets_[i]; }
        );
    }

    static void expectVec3Eq(vec3 actual, vec3 expected, const char *what) {
        EXPECT_FLOAT_EQ(actual.x, expected.x) << what << " .x";
        EXPECT_FLOAT_EQ(actual.y, expected.y) << what << " .y";
        EXPECT_FLOAT_EQ(actual.z, expected.z) << what << " .z";
    }
};

// ---------------------------------------------------------------------------
// Positive fire: the defect the issue describes.
// ---------------------------------------------------------------------------

TEST_F(RebuildDetachedVoxelsGuardTest, GroundSetInReVoxelizePoolFiresTheGuard) {
    const IREntity::EntityId canvas = makeCanvas(/*reVoxelize=*/true);
    IREntity::createEntity(C_VoxelSetNew{ivec3(2, 2, 2), kTestColor, EntityAnchor::GROUND, canvas});

    EXPECT_THROW(tick(), std::runtime_error);

    // The assert runs BEFORE setStaticReVoxelizeBound, so a rejected pool is
    // never left holding a bound derived from an off-center solid.
    EXPECT_FALSE(hasBound(canvas)) << "guard must fire before the bound is seeded";
}

// ---------------------------------------------------------------------------
// Positive control (issue AC 2): the identical path, silent.
// ---------------------------------------------------------------------------

TEST_F(RebuildDetachedVoxelsGuardTest, CenterSetInReVoxelizePoolIsSilent) {
    const IREntity::EntityId canvas = makeCanvas(/*reVoxelize=*/true);
    IREntity::createEntity(C_VoxelSetNew{ivec3(2, 2, 2), kTestColor, EntityAnchor::CENTER, canvas});

    EXPECT_NO_THROW(tick());

    // Not a vacuous pass: the bound is seeded in the same block the guard
    // stands in, so its presence proves the tick reached the guarded code.
    EXPECT_TRUE(hasBound(canvas)) << "tick must have reached the guarded block";
}

TEST_F(RebuildDetachedVoxelsGuardTest, LegacyCenterAroundOriginSpellingIsSilent) {
    const IREntity::EntityId canvas = makeCanvas(/*reVoxelize=*/true);
    // The spelling all eight canvas_stress spawns, detached_probe and fog_demo
    // use. It delegates to EntityAnchor::CENTER, and must stay silent.
    IREntity::createEntity(
        C_VoxelSetNew{ivec3(4, 4, 4), kTestColor, /*centerAroundOrigin=*/true, canvas}
    );

    EXPECT_NO_THROW(tick());
    EXPECT_TRUE(hasBound(canvas));
}

// ---------------------------------------------------------------------------
// The widened scope: CORNER orbits its min corner by the same mechanism.
// ---------------------------------------------------------------------------

TEST_F(RebuildDetachedVoxelsGuardTest, CornerSetInReVoxelizePoolFiresTheGuard) {
    const IREntity::EntityId canvas = makeCanvas(/*reVoxelize=*/true);
    IREntity::createEntity(C_VoxelSetNew{ivec3(2, 2, 2), kTestColor, EntityAnchor::CORNER, canvas});

    EXPECT_THROW(tick(), std::runtime_error);
    EXPECT_FALSE(hasBound(canvas));
}

TEST_F(RebuildDetachedVoxelsGuardTest, SingleCellCornerSetIsCentered) {
    const IREntity::EntityId canvas = makeCanvas(/*reVoxelize=*/true);
    // A 1-cell CORNER set IS origin-centered (offset 0, one voxel at 0), so it
    // must pass — this pins the 0.5 tolerance edge from below.
    IREntity::createEntity(C_VoxelSetNew{ivec3(1, 1, 1), kTestColor, EntityAnchor::CORNER, canvas});

    EXPECT_NO_THROW(tick());
    EXPECT_TRUE(hasBound(canvas));
}

// ---------------------------------------------------------------------------
// Allocated-slot semantics: a carve must not read as off-center.
// ---------------------------------------------------------------------------

TEST_F(RebuildDetachedVoxelsGuardTest, CarvedCenterSetStaysCentered) {
    const IREntity::EntityId canvas = makeCanvas(/*reVoxelize=*/true);
    const IREntity::EntityId object = IREntity::createEntity(
        C_VoxelSetNew{ivec3(4, 4, 4), kTestColor, EntityAnchor::CENTER, canvas}
    );

    // The canvas_stress L-prism carve. The guard scans ALLOCATED slots, not the
    // active mask, so deactivating a quadrant must not make the pool look
    // asymmetric — the bound-seed loop iterates the same span.
    IREntity::getComponent<C_VoxelSetNew>(object).carve([](vec3 localPos) {
        return localPos.x > 0.0f && localPos.y > 0.0f;
    });

    EXPECT_NO_THROW(tick());
    EXPECT_TRUE(hasBound(canvas));
}

// ---------------------------------------------------------------------------
// Scope boundary: plain DETACHED forward-scatter is not this guard's business.
// ---------------------------------------------------------------------------

TEST_F(RebuildDetachedVoxelsGuardTest, GroundSetOnANonReVoxelizeCanvasIsOutOfScope) {
    const IREntity::EntityId canvas = makeCanvas(/*reVoxelize=*/false);
    IREntity::createEntity(C_VoxelSetNew{ivec3(2, 2, 2), kTestColor, EntityAnchor::GROUND, canvas});

    // The tick early-returns on !reVoxelize_, so the guard never runs and no
    // bound is seeded. A forward-scatter canvas does not inverse-resample.
    EXPECT_NO_THROW(tick());
    EXPECT_FALSE(hasBound(canvas));
}

// ---------------------------------------------------------------------------
// The pure helper, per anchor and size — exactness is the point.
// ---------------------------------------------------------------------------

TEST_F(RebuildDetachedVoxelsGuardTest, PoolOriginAsymmetryMatchesEachAnchor) {
    const IREntity::EntityId canvas = makeCanvas(/*reVoxelize=*/false);

    // Odd and even extents so half-integer parity is covered both ways.
    for (const ivec3 size :
         {ivec3(1, 1, 1), ivec3(2, 2, 2), ivec3(3, 3, 3), ivec3(2, 4, 2), ivec3(6, 6, 12)}) {
        const IREntity::EntityId centerObj = IREntity::createEntity(
            C_VoxelSetNew{size, kTestColor, EntityAnchor::CENTER, canvas}
        );
        const IREntity::EntityId groundObj = IREntity::createEntity(
            C_VoxelSetNew{size, kTestColor, EntityAnchor::GROUND, canvas}
        );
        const IREntity::EntityId cornerObj = IREntity::createEntity(
            C_VoxelSetNew{size, kTestColor, EntityAnchor::CORNER, canvas}
        );

        // CENTER is exactly symmetric on every axis — every value is a multiple
        // of 0.5, so this is float-exact, not near.
        expectVec3Eq(
            asymmetryOf(IREntity::getComponent<C_VoxelSetNew>(centerObj)),
            vec3(0.0f),
            "CENTER"
        );
        // GROUND shifts z by -(size.z - 0.5) relative to the centered bake, so
        // the z sum is -size.z; x and y stay centered.
        expectVec3Eq(
            asymmetryOf(IREntity::getComponent<C_VoxelSetNew>(groundObj)),
            vec3(0.0f, 0.0f, -static_cast<float>(size.z)),
            "GROUND"
        );
        // CORNER puts the min voxel at the origin, so the sum is the max index.
        expectVec3Eq(
            asymmetryOf(IREntity::getComponent<C_VoxelSetNew>(cornerObj)),
            vec3(size - ivec3(1)),
            "CORNER"
        );
    }
}

TEST_F(RebuildDetachedVoxelsGuardTest, PoolIsOriginCenteredHandlesTheEmptyPool) {
    // Nothing to be off-center by, and the rebuild tick returns before the
    // guard on an empty pool anyway. Guard the helper's own edge.
    const auto never = [](int) { return vec3(0.0f); };
    EXPECT_TRUE(IRPrefab::GridRotation::poolIsOriginCentered(0, never));
    expectVec3Eq(IRPrefab::GridRotation::poolOriginAsymmetry(0, never), vec3(0.0f), "empty");
}

} // namespace

// Canvas-teardown contract for the per-canvas voxel pool (#2913): destroying a
// canvas re-stages every `C_VoxelSetNew` that allocated out of its pool, so no
// set is left naming a destroyed entity or indexing a dead pool.
//
// Every case runs headless. `C_VoxelPool` is constructible on a bare entity and
// `C_VoxelSetNew`'s `targetCanvas` argument routes allocation straight at it, so
// no RenderManager is needed — the same shape `voxel_set_target_canvas_test.cpp`
// uses.
//
// `IREntity::destroyEntity` only MARKS (`markEntityForDeletion`); the record
// teardown — and with it the pre-destroy hooks — runs in the
// `destroyMarkedEntities` drain, which `World` calls once per frame. Headless
// there is no World, so every case below drains explicitly via `destroyNow`.
// Marking without draining is what a frame looks like mid-tick: the canvas is
// still alive and its pool still valid, so nothing is stranded until the drain.
//
// The two production arming sites (`Prefab<kVoxelPoolCanvas>::create`,
// `IRPrefab::EntityCanvas::addVoxelPool`) both spawn GPU textures and cannot run
// here, so the fixture calls `ensureCanvasTeardownHook()` — the arming function
// those sites call — directly. The assertions below then destroy the canvas and
// nothing else: no test hand-calls `restageFromPool`, so a hook that is not
// actually wired fails them.

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/rotation_mode.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/voxel_pool_teardown.hpp>

namespace {

using IRComponents::C_EntityCanvas;
using IRComponents::C_RotationMode;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::RotationMode;
using IRMath::Color;
using IRMath::ivec3;

const ivec3 kPoolSize{8, 8, 8};
const ivec3 kSetSize{2, 3, 4};
const int kSetVoxels = kSetSize.x * kSetSize.y * kSetSize.z;

class VoxelPoolTeardown : public ::testing::Test {
  protected:
    void SetUp() override {
        IRPrefab::VoxelPool::ensureCanvasTeardownHook();
    }

    // A canvas entity owning a pool, plus a separate entity whose voxel set
    // allocated out of it. Deliberately two entities: the sweep keys on the
    // set's `canvasEntity_`, not on the canvas's parent, and a set that renders
    // into a canvas need not live on the entity holding the wrapper.
    // Mark + drain, i.e. what one frame's `destroyEntity` actually amounts to.
    static void destroyNow(IREntity::EntityId entity) {
        IREntity::destroyEntity(entity);
        IREntity::getEntityManager().destroyMarkedEntities();
    }

    static IREntity::EntityId makeCanvas() {
        return IREntity::createEntity(C_VoxelPool{kPoolSize});
    }

    static IREntity::EntityId makeResidentSet(IREntity::EntityId canvas) {
        return IREntity::createEntity(
            C_VoxelSetNew{kSetSize, Color{200, 100, 50, 255}, false, canvas}
        );
    }

    static const C_VoxelSetNew &setOf(IREntity::EntityId entity) {
        return IREntity::getComponent<C_VoxelSetNew>(entity);
    }

    IREntity::EntityManager m_entityManager;
};

// The whole point: after the canvas is gone the set holds no reference to it and
// no span into its pool, and its voxel records survive in staged form.
TEST_F(VoxelPoolTeardown, DestroyingTheCanvasRestagesItsResidentSets) {
    const IREntity::EntityId canvas = makeCanvas();
    const IREntity::EntityId object = makeResidentSet(canvas);
    ASSERT_EQ(setOf(object).numVoxels_, kSetVoxels);
    ASSERT_EQ(setOf(object).canvasEntity_, canvas);

    destroyNow(canvas);

    const C_VoxelSetNew &set = setOf(object);
    EXPECT_EQ(set.numVoxels_, 0);
    EXPECT_EQ(set.voxelStartIdx_, 0u);
    EXPECT_EQ(set.canvasEntity_, IREntity::kNullEntity);
    EXPECT_EQ(set.pendingVoxels_.size(), static_cast<std::size_t>(kSetVoxels));
    // Staged mode is the state `attachToCanvas` seeds from, so the set stays
    // re-homeable rather than merely being made safe.
    EXPECT_EQ(set.recordCount(), static_cast<std::size_t>(kSetVoxels));
    EXPECT_EQ(set.size_.x, kSetSize.x);
    EXPECT_EQ(set.size_.y, kSetSize.y);
    EXPECT_EQ(set.size_.z, kSetSize.z);
}

// The colors are the point of a voxel set; a re-stage that dropped or zeroed
// them would satisfy every id/index assertion above.
TEST_F(VoxelPoolTeardown, RestagedRecordsKeepTheAuthoredColors) {
    const IREntity::EntityId canvas = makeCanvas();
    const IREntity::EntityId object = makeResidentSet(canvas);
    IREntity::getComponent<C_VoxelSetNew>(object).changeVoxelColor(
        ivec3(1, 2, 3),
        Color{11, 22, 33, 255}
    );

    destroyNow(canvas);

    const C_VoxelSetNew &set = setOf(object);
    ASSERT_EQ(set.pendingVoxels_.size(), static_cast<std::size_t>(kSetVoxels));
    const int edited = IRMath::index3DtoIndex1D(ivec3(1, 2, 3), kSetSize);
    EXPECT_EQ(
        set.pendingVoxels_[edited].color_.toPackedRGBA(),
        (Color{11, 22, 33, 255}).toPackedRGBA()
    );
    EXPECT_EQ(
        set.pendingVoxels_[0].color_.toPackedRGBA(),
        (Color{200, 100, 50, 255}).toPackedRGBA()
    );
}

// The span goes back to the pool rather than leaking. `getLiveVoxelCount()` is
// the bump high-water mark, which a deallocate deliberately does not rewind, so
// the observable is reuse: the next same-size request must come back out of the
// freed span rather than off the end of the pool.
TEST_F(VoxelPoolTeardown, RestageReleasesTheSpanBackToThePool) {
    const IREntity::EntityId canvas = makeCanvas();
    makeResidentSet(canvas);
    ASSERT_EQ(IREntity::getComponent<C_VoxelPool>(canvas).getLiveVoxelCount(), kSetVoxels);

    IRPrefab::VoxelPool::restageSetsOnCanvas(canvas);
    const auto reused = IRPrefab::VoxelPool::allocate(kSetVoxels, canvas);

    EXPECT_EQ(reused.startIndex_, 0u);
    EXPECT_EQ(IREntity::getComponent<C_VoxelPool>(canvas).getLiveVoxelCount(), kSetVoxels);
}

// The paired control for the case above: without the re-stage the same request
// lands past the resident span, so the assertion there is about the release and
// not about the allocator handing out index 0 by default.
TEST_F(VoxelPoolTeardown, WithoutRestageTheSameRequestLandsPastTheResidentSpan) {
    const IREntity::EntityId canvas = makeCanvas();
    makeResidentSet(canvas);

    const auto fresh = IRPrefab::VoxelPool::allocate(kSetVoxels, canvas);

    EXPECT_EQ(fresh.startIndex_, static_cast<std::size_t>(kSetVoxels));
}

// A set allocated out of a DIFFERENT canvas must not be touched — the sweep is
// keyed, not a blanket "re-stage everything on any destroy".
TEST_F(VoxelPoolTeardown, SetsOnOtherCanvasesAreUntouched) {
    const IREntity::EntityId canvasA = makeCanvas();
    const IREntity::EntityId canvasB = makeCanvas();
    const IREntity::EntityId objectB = makeResidentSet(canvasB);

    destroyNow(canvasA);

    const C_VoxelSetNew &set = setOf(objectB);
    EXPECT_EQ(set.numVoxels_, kSetVoxels);
    EXPECT_EQ(set.canvasEntity_, canvasB);
    EXPECT_TRUE(set.pendingVoxels_.empty());
}

// The hook fires on every destroy, so the non-canvas case is the one that must
// stay cheap and inert. Destroying the set's own entity leaves the pool empty
// via `onDestroy()` — the ordinary release path — not via the sweep.
TEST_F(VoxelPoolTeardown, DestroyingANonCanvasEntityIsInert) {
    const IREntity::EntityId canvas = makeCanvas();
    const IREntity::EntityId object = makeResidentSet(canvas);
    const IREntity::EntityId bystander = IREntity::createEntity(C_RotationMode{RotationMode::GRID});

    destroyNow(bystander);

    const C_VoxelSetNew &set = setOf(object);
    EXPECT_EQ(set.numVoxels_, kSetVoxels);
    EXPECT_EQ(set.canvasEntity_, canvas);
}

// Arming is idempotent: a world creates several canvases, and a second
// registration would run the sweep twice per destroy (the second pass seeing
// already-staged sets).
TEST_F(VoxelPoolTeardown, ArmingTwiceRegistersOneHook) {
    const IREntity::PreDestroyHookId first =
        IREntity::singleton<IRComponents::C_VoxelPoolTeardownHook>().hookId_;
    ASSERT_NE(first, IREntity::kInvalidPreDestroyHookId);

    IRPrefab::VoxelPool::ensureCanvasTeardownHook();

    EXPECT_EQ(IREntity::singleton<IRComponents::C_VoxelPoolTeardownHook>().hookId_, first);
}

// The reachable production caller (#2913's title case): `setMode` leaving the
// canvas-owning family destroys the canvas, which must take its pool's sets
// with it rather than stranding them.
TEST_F(VoxelPoolTeardown, SetModeToGridRestagesTheCanvasResidentSet) {
    const IREntity::EntityId canvas = makeCanvas();
    const IREntity::EntityId entity = IREntity::createEntity(
        C_RotationMode{RotationMode::DETACHED_REVOXELIZE},
        C_EntityCanvas{canvas, IRMath::ivec2(16, 16)}
    );
    const IREntity::EntityId object = makeResidentSet(canvas);

    IRPrefab::RotationMode::setMode(entity, RotationMode::GRID);
    IREntity::getEntityManager().destroyMarkedEntities();

    ASSERT_FALSE(IREntity::entityExists(canvas));
    const C_VoxelSetNew &set = setOf(object);
    EXPECT_EQ(set.numVoxels_, 0);
    EXPECT_EQ(set.canvasEntity_, IREntity::kNullEntity);
    EXPECT_EQ(set.pendingVoxels_.size(), static_cast<std::size_t>(kSetVoxels));
}

} // namespace

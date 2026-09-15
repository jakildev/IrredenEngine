// Canvas-teardown contract for the per-canvas voxel pool: destroying a canvas
// re-stages every `C_VoxelSetNew` that allocated out of its pool, so no
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
// nothing else: no test hand-calls `restageSet`, so a hook that is not
// actually wired fails them.
//
// The re-home cases (`Rehomed*`) continue past the teardown into
// `attachToCanvas` on a second canvas — the production attach route with its
// explicit target, since the active-canvas fallback needs a RenderManager —
// and assert the state the span derived on the first canvas comes back on the
// second: the per-trixel-priority count and pool aggregate, the GPU transform
// slot's per-voxel stamps, and a rigged set's bone stamps. The last of those
// is the seed pass's job, so that case drives `SEED_STAGED_VOXELS` through a
// `SystemManager` pipeline rather than calling the re-stamp by hand.

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/rotation_mode.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/systems/system_update_joint_matrices.hpp>
#include <irreden/voxel/components/component_joint.hpp>
#include <irreden/voxel/components/component_skeleton.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/systems/system_seed_staged_voxels.hpp>
#include <irreden/voxel/voxel_pool_teardown.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using IRComponents::C_EntityCanvas;
using IRComponents::C_Joint;
using IRComponents::C_RotationMode;
using IRComponents::C_Skeleton;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::RotationMode;
using IRComponents::VoxelReserved::kPriorityMask;
using IRMath::Color;
using IRMath::ivec3;

const ivec3 kPoolSize{8, 8, 8};
const ivec3 kSetSize{2, 3, 4};
const int kSetVoxels = kSetSize.x * kSetSize.y * kSetSize.z;
constexpr std::uint32_t kEntitySlot = 3;

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

    static C_VoxelSetNew &mutableSetOf(IREntity::EntityId entity) {
        return IREntity::getComponent<C_VoxelSetNew>(entity);
    }

    static const C_VoxelPool &poolOf(IREntity::EntityId canvas) {
        return IREntity::getComponent<C_VoxelPool>(canvas);
    }

    // Opt a resident set into the GPU transform prepass the way a creation
    // does (`shape_debug --gpu-voxel-smoke`): the slot on the set, the
    // per-voxel stamps on its pool span.
    static void routeThroughGpuSlot(IREntity::EntityId object, std::uint32_t slot) {
        C_VoxelSetNew &set = mutableSetOf(object);
        set.gpuTransformSlot_ = slot;
        IREntity::getComponent<C_VoxelPool>(set.canvasEntity_)
            .setTransformIndexForRange(
                set.voxelStartIdx_,
                static_cast<std::size_t>(set.numVoxels_),
                slot
            );
    }

    static void expectSpanStampedWith(IREntity::EntityId object, std::uint32_t slot) {
        const C_VoxelSetNew &set = setOf(object);
        const auto &indices = poolOf(set.canvasEntity_).getTransformIndices();
        for (int i = 0; i < set.numVoxels_; ++i) {
            EXPECT_EQ(indices[set.voxelStartIdx_ + static_cast<std::size_t>(i)], slot)
                << "voxel " << i;
        }
    }

    // EntityManager first: system registration reaches the entity manager.
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
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

// The reachable production caller, and the release-arm regression case the
// issue named: `setMode` leaving the canvas-owning family destroys the canvas,
// which must take its pool's sets with it rather than stranding them. This is
// the case to run ALONE against a disarmed hook when re-proving the fix.
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

// Priority tiers ride the voxel records, so they survive the re-stage as data;
// what the set and the pool COUNT has to follow them. The first canvas is
// re-staged rather than destroyed so its aggregate is still readable: it must
// drop to none, and the second canvas's must come up, with the set's own count
// matching — otherwise the re-homed voxels carry tier bits the finalization
// decode never looks at.
TEST_F(VoxelPoolTeardown, RehomedSetKeepsItsPriorityTiersCounted) {
    const IREntity::EntityId canvasA = makeCanvas();
    const IREntity::EntityId canvasB = makeCanvas();
    const IREntity::EntityId object = makeResidentSet(canvasA);
    mutableSetOf(object).changeVoxelPriorityAll(2);
    ASSERT_TRUE(poolOf(canvasA).hasPerTrixelPriority());
    ASSERT_EQ(setOf(object).perTrixelPriorityVoxelCount_, static_cast<std::uint32_t>(kSetVoxels));

    IRPrefab::VoxelPool::restageSetsOnCanvas(canvasA);
    EXPECT_FALSE(poolOf(canvasA).hasPerTrixelPriority());
    EXPECT_EQ(setOf(object).perTrixelPriorityVoxelCount_, 0u);

    ASSERT_TRUE(mutableSetOf(object).attachToCanvas(canvasB));

    const C_VoxelSetNew &set = setOf(object);
    EXPECT_EQ(set.canvasEntity_, canvasB);
    EXPECT_EQ(set.perTrixelPriorityVoxelCount_, static_cast<std::uint32_t>(kSetVoxels));
    EXPECT_TRUE(poolOf(canvasB).hasPerTrixelPriority());
    EXPECT_FALSE(poolOf(canvasA).hasPerTrixelPriority());
    for (int i = 0; i < set.numVoxels_; ++i) {
        EXPECT_EQ(set.voxels_[i].reserved_ & kPriorityMask, 2u) << "voxel " << i;
    }
}

// A set routed through the GPU transform prepass owns its slot; what it loses
// with the span is the per-voxel stamps in the pool. After the re-home the new
// span must point at the same slot and be queued for the binding-17 re-seed —
// with the slot retained but the span unstamped, UPDATE_VOXEL_SET_CHILDREN
// withholds the CPU upload and the prepass never writes the set either.
TEST_F(VoxelPoolTeardown, RehomedSetCarriesItsGpuTransformSlotOntoTheNewSpan) {
    const IREntity::EntityId canvasA = makeCanvas();
    const IREntity::EntityId canvasB = makeCanvas();
    const IREntity::EntityId object = makeResidentSet(canvasA);
    routeThroughGpuSlot(object, kEntitySlot);

    destroyNow(canvasA);
    ASSERT_EQ(setOf(object).gpuTransformSlot_, kEntitySlot);
    ASSERT_TRUE(mutableSetOf(object).attachToCanvas(canvasB));

    const C_VoxelSetNew &set = setOf(object);
    EXPECT_EQ(set.gpuTransformSlot_, kEntitySlot);
    expectSpanStampedWith(object, kEntitySlot);
    const auto &pending = poolOf(canvasB).getPendingTransformIndexRanges();
    ASSERT_EQ(pending.size(), 1u);
    EXPECT_EQ(
        pending[0],
        std::make_pair(set.voxelStartIdx_, static_cast<std::size_t>(set.numVoxels_))
    );
}

// The other half of the same hazard: a CPU-direct set re-homed into a span a
// GPU-routed set vacated must not inherit that set's stamps. The pool resets a
// freed range to static on release (and queues it, so binding 17 follows),
// which is what makes the reused span come back clean.
TEST_F(VoxelPoolTeardown, RehomedStaticSetDoesNotInheritTheVacatedSpansSlot) {
    const IREntity::EntityId canvasA = makeCanvas();
    const IREntity::EntityId canvasB = makeCanvas();
    const IREntity::EntityId vacating = makeResidentSet(canvasB);
    routeThroughGpuSlot(vacating, kEntitySlot);
    const std::size_t vacatedStart = setOf(vacating).voxelStartIdx_;
    const IREntity::EntityId object = makeResidentSet(canvasA);

    destroyNow(vacating);
    destroyNow(canvasA);
    ASSERT_TRUE(mutableSetOf(object).attachToCanvas(canvasB));

    const C_VoxelSetNew &set = setOf(object);
    ASSERT_EQ(set.voxelStartIdx_, vacatedStart)
        << "fixture: the re-home must reuse the vacated span";
    EXPECT_EQ(set.gpuTransformSlot_, IRRender::kVoxelTransformStatic);
    expectSpanStampedWith(object, IRRender::kVoxelTransformStatic);
    EXPECT_FALSE(poolOf(canvasB).getPendingTransformIndexRanges().empty());
}

// A rigged set's bone→slot stamps are UPDATE_JOINT_MATRICES' to write, and it
// writes them when the skeleton's block is allocated — which does not recur on
// a re-home. The seed pass owns the re-stamp: it lands the set, then re-runs
// `seedVoxelBoneSlots` for it in endTick. Driven through a SystemManager
// pipeline so the case fails if that wiring is missing, not just the stamp.
// The set names the second canvas as its target the way a saved canvas id
// does; the active-canvas fallback needs a RenderManager.
TEST_F(VoxelPoolTeardown, RehomedRiggedSetIsRestampedByTheSeedPass) {
    IRSystem::createSystem<IRSystem::UPDATE_JOINT_MATRICES>();
    m_systemManager.registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::SEED_STAGED_VOXELS>()}
    );
    const IREntity::EntityId canvasA = makeCanvas();
    const IREntity::EntityId canvasB = makeCanvas();
    const IREntity::EntityId rigRoot = IREntity::createEntity(
        C_VoxelSetNew{ivec3(3, 1, 1), Color{200, 100, 50, 255}, false, canvasA}
    );
    {
        C_VoxelSetNew &set = mutableSetOf(rigRoot);
        set.gpuTransformSlot_ = kEntitySlot;
        set.voxels_[0].bone_id_ = 0;
        set.voxels_[1].bone_id_ = 1;
        set.voxels_[2].bone_id_ = 7;
        C_Skeleton skeleton;
        for (int i = 0; i < 2; ++i) {
            skeleton.joints_.push_back(IREntity::createEntity(C_Joint{}));
            skeleton.bindPose_.push_back(IRMath::SQT{});
        }
        IREntity::setComponent(rigRoot, skeleton);
    }
    auto *joints = IRPrefab::JointTransform::system();
    ASSERT_NE(joints, nullptr);
    joints->beginTick();
    const std::uint32_t base = joints->skeletonBlocks_.at(rigRoot).base_;
    ASSERT_NE(base, IRRender::kVoxelTransformStatic);
    ASSERT_EQ(poolOf(canvasA).getTransformIndices()[setOf(rigRoot).voxelStartIdx_], base);

    destroyNow(canvasA);
    ASSERT_EQ(setOf(rigRoot).numVoxels_, 0);
    mutableSetOf(rigRoot).canvasEntity_ = canvasB;
    m_systemManager.executePipeline(IRTime::Events::UPDATE);

    const C_VoxelSetNew &set = setOf(rigRoot);
    ASSERT_EQ(set.numVoxels_, 3);
    ASSERT_EQ(set.canvasEntity_, canvasB);
    EXPECT_EQ(set.gpuTransformSlot_, kEntitySlot);
    const auto &indices = poolOf(canvasB).getTransformIndices();
    EXPECT_EQ(indices[set.voxelStartIdx_ + 0], base + 0);
    EXPECT_EQ(indices[set.voxelStartIdx_ + 1], base + 1);
    EXPECT_EQ(indices[set.voxelStartIdx_ + 2], kEntitySlot);
}

} // namespace

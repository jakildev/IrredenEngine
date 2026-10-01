#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/components/entity_anchor.hpp>
#include <irreden/common/rotation_mode.hpp>
#include <irreden/render/canvas_part.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_canvas_part.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/systems/system_propagate_canvas_parts.hpp>
#include <irreden/render/systems/system_propagate_canvas_rotation.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <stdexcept>

// Composite entities: several voxel sets hosted in one detached re-voxelize
// canvas, each posted to the canvas's pool as a cell group with its own pose.
//
// Headless: a canvas is a plain entity carrying the pool and pose components
// the systems read, and every voxel set names its target canvas explicitly, so
// nothing reaches the render manager. With no active canvas a set that leaves
// its pool stays staged — the observable stand-in for "re-homed to the main
// canvas" here.

namespace {

using IRComponents::C_CanvasLocalRotation;
using IRComponents::C_CanvasPart;
using IRComponents::C_EntityCanvas;
using IRComponents::C_LocalTransform;
using IRComponents::C_RotationMode;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::EntityAnchor;
using IRComponents::RotationMode;
using IRComponents::VoxelCellGroup;
using IRMath::Color;
using IRMath::ivec2;
using IRMath::ivec3;
using IRMath::vec3;
using IRMath::vec4;

constexpr float kEps = 1e-4f;
constexpr Color kColor{200, 80, 40, 255};

class CanvasPart : public testing::Test {
  protected:
    CanvasPart() {
        m_system_manager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
             IRSystem::createSystem<IRSystem::PROPAGATE_CANVAS_ROTATION>(),
             IRSystem::createSystem<IRSystem::PROPAGATE_CANVAS_PARTS>()}
        );
        IREntity::setName(IREntity::createEntity(), "camera");
    }

    void tick() {
        m_system_manager.executePipeline(IRTime::Events::UPDATE);
    }

    static IREntity::EntityId makeCanvas(ivec3 poolSize = ivec3(16, 16, 16)) {
        return IREntity::createEntity(C_VoxelPool{poolSize}, C_CanvasLocalRotation{});
    }

    // A host entity owning a canvas, in the given canvas-owning mode.
    struct Host {
        IREntity::EntityId entity_;
        IREntity::EntityId canvas_;
    };

    static Host makeHost(
        const C_LocalTransform &local,
        RotationMode mode = RotationMode::DETACHED_REVOXELIZE,
        ivec3 poolSize = ivec3(16, 16, 16)
    ) {
        const IREntity::EntityId canvas = makeCanvas(poolSize);
        const IREntity::EntityId entity =
            IREntity::createEntity(local, C_RotationMode{mode}, C_EntityCanvas{canvas, ivec2(64)});
        return {entity, canvas};
    }

    static const C_VoxelPool &poolOf(IREntity::EntityId canvas) {
        return IREntity::getComponent<C_VoxelPool>(canvas);
    }

    static const C_VoxelSetNew &setOf(IREntity::EntityId entity) {
        return IREntity::getComponent<C_VoxelSetNew>(entity);
    }

    static RotationMode modeOf(IREntity::EntityId entity) {
        return IREntity::getComponent<C_RotationMode>(entity).mode_;
    }

    static bool isPart(IREntity::EntityId entity) {
        return IREntity::getComponentOptional<C_CanvasPart>(entity).has_value();
    }

    static void expectVec3Near(vec3 actual, vec3 expected) {
        EXPECT_NEAR(actual.x, expected.x, kEps);
        EXPECT_NEAR(actual.y, expected.y, kEps);
        EXPECT_NEAR(actual.z, expected.z, kEps);
    }

    static void expectQuatNear(vec4 actual, vec4 expected) {
        EXPECT_NEAR(actual.x, expected.x, kEps);
        EXPECT_NEAR(actual.y, expected.y, kEps);
        EXPECT_NEAR(actual.z, expected.z, kEps);
        EXPECT_NEAR(actual.w, expected.w, kEps);
    }

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(CanvasPart, EachHostedPartPostsOneGroupPosedByItsOwnTransform) {
    const Host host = makeHost(C_LocalTransform{vec3(10.0f, 0.0f, 0.0f)});
    const vec4 rotationA = IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), 0.6f);
    const vec4 rotationB = IRMath::quatAxisAngle(vec3(1.0f, 0.0f, 0.0f), 1.2f);
    const IREntity::EntityId partA = IRPrefab::CanvasPart::create(
        host.entity_,
        C_LocalTransform{vec3(13.0f, 0.0f, 0.0f), rotationA},
        ivec3(2, 2, 2),
        kColor
    );
    const IREntity::EntityId partB = IRPrefab::CanvasPart::create(
        host.entity_,
        C_LocalTransform{vec3(10.0f, -4.5f, 1.0f), rotationB},
        ivec3(3, 3, 3),
        kColor
    );

    tick();

    const std::vector<VoxelCellGroup> &groups = poolOf(host.canvas_).getCellGroups();
    ASSERT_EQ(groups.size(), 2u);
    EXPECT_TRUE(poolOf(host.canvas_).hostsCellGroups());
    EXPECT_EQ(groups[0].start_, setOf(partA).voxelStartIdx_);
    EXPECT_EQ(groups[0].count_, 8u);
    expectQuatNear(groups[0].rotation_, rotationA);
    expectVec3Near(groups[0].translation_, vec3(3.0f, 0.0f, 0.0f));
    EXPECT_EQ(groups[1].start_, setOf(partB).voxelStartIdx_);
    EXPECT_EQ(groups[1].count_, 27u);
    expectQuatNear(groups[1].rotation_, rotationB);
    expectVec3Near(groups[1].translation_, vec3(0.0f, -4.5f, 1.0f));
    EXPECT_TRUE(IRPrefab::CanvasPart::isHosted(partA));
    EXPECT_EQ(modeOf(partA), RotationMode::DETACHED_REVOXELIZE);
}

TEST_F(CanvasPart, ParentedPartRidesTheHostsRotation) {
    const float angle = 0.8f;
    const vec4 hostRotation = IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), angle);
    const Host host = makeHost(C_LocalTransform{vec3(5.0f, 5.0f, 0.0f), hostRotation});
    const IREntity::EntityId part = IRPrefab::CanvasPart::create(
        host.entity_,
        C_LocalTransform{vec3(3.0f, 0.0f, 0.0f)},
        ivec3(2, 2, 2),
        kColor
    );
    IREntity::setParent(part, host.entity_);

    tick();

    const std::vector<VoxelCellGroup> &groups = poolOf(host.canvas_).getCellGroups();
    ASSERT_EQ(groups.size(), 1u);
    expectQuatNear(groups[0].rotation_, hostRotation);
    expectVec3Near(
        groups[0].translation_,
        vec3(3.0f * IRMath::cos(angle), 3.0f * IRMath::sin(angle), 0.0f)
    );
}

TEST_F(CanvasPart, GroupListIsRebuiltEveryFrame) {
    const Host host = makeHost(C_LocalTransform{vec3(0.0f)});
    const IREntity::EntityId part = IRPrefab::CanvasPart::create(
        host.entity_,
        C_LocalTransform{vec3(1.0f, 0.0f, 0.0f)},
        ivec3(2, 2, 2),
        kColor
    );

    tick();
    IREntity::getComponent<C_LocalTransform>(part).translation_ = vec3(4.0f, 2.0f, 0.0f);
    tick();

    const std::vector<VoxelCellGroup> &groups = poolOf(host.canvas_).getCellGroups();
    ASSERT_EQ(groups.size(), 1u) << "a re-posted span must replace, not accumulate";
    expectVec3Near(groups[0].translation_, vec3(4.0f, 2.0f, 0.0f));
}

TEST_F(CanvasPart, HiddenPartIsNotPosted) {
    const Host host = makeHost(C_LocalTransform{vec3(0.0f)});
    const IREntity::EntityId part =
        IRPrefab::CanvasPart::create(host.entity_, C_LocalTransform{}, ivec3(2, 2, 2), kColor);
    IREntity::getComponent<C_VoxelSetNew>(part).visible_ = false;

    tick();

    EXPECT_TRUE(poolOf(host.canvas_).getCellGroups().empty());
}

TEST_F(CanvasPart, SetOnAForwardScatterCanvasIsNotPosted) {
    const Host host = makeHost(C_LocalTransform{vec3(0.0f)}, RotationMode::DETACHED);
    IREntity::createEntity(
        C_VoxelSetNew{ivec3(2, 2, 2), kColor, EntityAnchor::CENTER, host.canvas_},
        C_RotationMode{RotationMode::DETACHED_REVOXELIZE},
        C_CanvasPart{host.entity_}
    );

    tick();

    EXPECT_TRUE(poolOf(host.canvas_).getCellGroups().empty());
    EXPECT_FALSE(poolOf(host.canvas_).hostsCellGroups());
}

TEST_F(CanvasPart, FreedSpanStopsBeingAGroupImmediately) {
    const Host host = makeHost(C_LocalTransform{vec3(0.0f)});
    const IREntity::EntityId partA =
        IRPrefab::CanvasPart::create(host.entity_, C_LocalTransform{}, ivec3(2, 2, 2), kColor);
    const IREntity::EntityId partB =
        IRPrefab::CanvasPart::create(host.entity_, C_LocalTransform{}, ivec3(3, 3, 3), kColor);
    tick();
    ASSERT_EQ(poolOf(host.canvas_).getCellGroups().size(), 2u);
    const std::size_t startB = setOf(partB).voxelStartIdx_;

    // No tick in between: the group must be gone before the next pose pass,
    // or the frame the span was freed on would still resample it.
    IREntity::getEntityManager().destroyEntity(partA);

    const std::vector<VoxelCellGroup> &groups = poolOf(host.canvas_).getCellGroups();
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].start_, startB);
    EXPECT_TRUE(poolOf(host.canvas_).hostsCellGroups())
        << "a pool that hosted groups keeps resampling only posted spans";
}

TEST_F(CanvasPart, SetModeDetachesAPartFromItsHost) {
    const Host host = makeHost(C_LocalTransform{vec3(0.0f)});
    const IREntity::EntityId staying =
        IRPrefab::CanvasPart::create(host.entity_, C_LocalTransform{}, ivec3(3, 3, 3), kColor);
    const IREntity::EntityId leaving = IRPrefab::CanvasPart::create(
        host.entity_,
        C_LocalTransform{vec3(6.0f, 0.0f, 0.0f)},
        ivec3(2, 2, 2),
        kColor
    );
    tick();
    ASSERT_EQ(poolOf(host.canvas_).getCellGroups().size(), 2u);

    IRPrefab::RotationMode::setMode(leaving, RotationMode::GRID);

    EXPECT_FALSE(isPart(leaving));
    EXPECT_EQ(modeOf(leaving), RotationMode::GRID);
    // The same call frees the span and drops its group: no frame draws the
    // part from both the host and its new home.
    const std::vector<VoxelCellGroup> &groups = poolOf(host.canvas_).getCellGroups();
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].start_, setOf(staying).voxelStartIdx_);
    // Records survive the move; with no active canvas they wait staged.
    EXPECT_EQ(setOf(leaving).numVoxels_, 0);
    EXPECT_EQ(setOf(leaving).recordCount(), 8u);
    EXPECT_EQ(setOf(leaving).size_, ivec3(2, 2, 2));
    // The transform is untouched: the part kept its own pose all along.
    expectVec3Near(
        IREntity::getComponent<C_LocalTransform>(leaving).translation_,
        vec3(6.0f, 0.0f, 0.0f)
    );
    EXPECT_TRUE(isPart(staying));
}

TEST_F(CanvasPart, HostLeavingReVoxelizeReleasesItsPartsToGrid) {
    const Host host = makeHost(C_LocalTransform{vec3(0.0f)});
    const IREntity::EntityId part =
        IRPrefab::CanvasPart::create(host.entity_, C_LocalTransform{}, ivec3(2, 2, 2), kColor);
    tick();

    IRPrefab::RotationMode::setMode(host.entity_, RotationMode::GRID);

    EXPECT_EQ(modeOf(part), RotationMode::GRID);
    EXPECT_TRUE(isPart(part)) << "membership outlives residency";
    EXPECT_FALSE(IRPrefab::CanvasPart::isHosted(part));
    EXPECT_EQ(setOf(part).numVoxels_, 0);
    EXPECT_EQ(setOf(part).recordCount(), 8u);
    EXPECT_FALSE(IREntity::getComponentOptional<C_EntityCanvas>(host.entity_).has_value());
}

TEST_F(CanvasPart, HostSwitchingToForwardScatterReleasesOnlyItsParts) {
    const Host host = makeHost(C_LocalTransform{vec3(0.0f)});
    IREntity::setComponent(
        host.entity_,
        C_VoxelSetNew{ivec3(3, 3, 3), kColor, EntityAnchor::CENTER, host.canvas_}
    );
    const IREntity::EntityId part =
        IRPrefab::CanvasPart::create(host.entity_, C_LocalTransform{}, ivec3(2, 2, 2), kColor);

    IRPrefab::RotationMode::setMode(host.entity_, RotationMode::DETACHED);

    EXPECT_EQ(modeOf(part), RotationMode::GRID);
    EXPECT_EQ(setOf(part).numVoxels_, 0);
    EXPECT_EQ(setOf(host.entity_).numVoxels_, 27) << "the host's own set keeps its canvas";
    EXPECT_EQ(setOf(host.entity_).canvasEntity_, host.canvas_);
}

TEST_F(CanvasPart, AdoptPartsMovesReleasedPartsIntoTheNewCanvas) {
    const Host host = makeHost(C_LocalTransform{vec3(0.0f)});
    const IREntity::EntityId part =
        IRPrefab::CanvasPart::create(host.entity_, C_LocalTransform{}, ivec3(2, 2, 2), kColor);
    IRPrefab::RotationMode::setMode(host.entity_, RotationMode::GRID);
    ASSERT_EQ(setOf(part).numVoxels_, 0);

    const IREntity::EntityId newCanvas = makeCanvas();
    IREntity::setComponent(host.entity_, C_EntityCanvas{newCanvas, ivec2(64)});
    IREntity::setComponent(host.entity_, C_RotationMode{RotationMode::DETACHED_REVOXELIZE});
    IRPrefab::CanvasPart::adoptParts(host.entity_, newCanvas);

    EXPECT_EQ(setOf(part).numVoxels_, 8);
    EXPECT_EQ(setOf(part).canvasEntity_, newCanvas);
    EXPECT_EQ(modeOf(part), RotationMode::DETACHED_REVOXELIZE);
    EXPECT_TRUE(IRPrefab::CanvasPart::isHosted(part));
}

TEST_F(CanvasPart, AttachMovesAnExistingSetIntoTheHostCanvas) {
    const Host host = makeHost(C_LocalTransform{vec3(0.0f)});
    const IREntity::EntityId elsewhere = makeCanvas();
    const IREntity::EntityId entity = IREntity::createEntity(
        C_VoxelSetNew{ivec3(2, 2, 2), kColor, EntityAnchor::CENTER, elsewhere},
        C_RotationMode{RotationMode::GRID}
    );

    EXPECT_TRUE(IRPrefab::CanvasPart::attach(entity, host.entity_));

    EXPECT_TRUE(IRPrefab::CanvasPart::isHosted(entity));
    EXPECT_EQ(setOf(entity).canvasEntity_, host.canvas_);
    EXPECT_EQ(setOf(entity).numVoxels_, 8);
    EXPECT_EQ(modeOf(entity), RotationMode::DETACHED_REVOXELIZE);
    EXPECT_EQ(poolOf(elsewhere).getLiveVoxelCount(), 8) << "the old span is freed, not shrunk";
}

TEST_F(CanvasPart, AttachLeavesThePartInPlaceWhenTheHostPoolIsFull) {
    const Host host =
        makeHost(C_LocalTransform{vec3(0.0f)}, RotationMode::DETACHED_REVOXELIZE, ivec3(2, 2, 2));
    const IREntity::EntityId elsewhere = makeCanvas();
    const IREntity::EntityId entity = IREntity::createEntity(
        C_VoxelSetNew{ivec3(3, 3, 3), kColor, EntityAnchor::CENTER, elsewhere},
        C_RotationMode{RotationMode::GRID}
    );

    EXPECT_FALSE(IRPrefab::CanvasPart::attach(entity, host.entity_));

    EXPECT_EQ(setOf(entity).canvasEntity_, elsewhere);
    EXPECT_EQ(setOf(entity).numVoxels_, 27) << "a refused move must not drop the records";
    EXPECT_EQ(modeOf(entity), RotationMode::GRID);
    EXPECT_FALSE(IRPrefab::CanvasPart::isHosted(entity));
}

TEST_F(CanvasPart, AttachToAHostWithoutACanvasOnlyRecordsMembership) {
    const IREntity::EntityId host = IREntity::createEntity(C_RotationMode{RotationMode::GRID});
    const IREntity::EntityId elsewhere = makeCanvas();
    const IREntity::EntityId entity = IREntity::createEntity(
        C_VoxelSetNew{ivec3(2, 2, 2), kColor, EntityAnchor::CENTER, elsewhere},
        C_RotationMode{RotationMode::GRID}
    );

    EXPECT_FALSE(IRPrefab::CanvasPart::attach(entity, host));

    EXPECT_TRUE(isPart(entity));
    EXPECT_EQ(setOf(entity).canvasEntity_, elsewhere);
    EXPECT_EQ(modeOf(entity), RotationMode::GRID);
}

TEST_F(CanvasPart, AttachingAnEntityToItselfFiresTheGuard) {
    const Host host = makeHost(C_LocalTransform{vec3(0.0f)});

    EXPECT_THROW(IRPrefab::CanvasPart::attach(host.entity_, host.entity_), std::runtime_error);
    EXPECT_FALSE(isPart(host.entity_));
}

// ---- the pool's cell-group registry -------------------------------------

TEST(VoxelPoolCellGroups, PostedGroupsAreKeptInSpanOrder) {
    C_VoxelPool pool{ivec3(8, 8, 8)};
    pool.postCellGroup(VoxelCellGroup{64, 8});
    pool.postCellGroup(VoxelCellGroup{0, 27});
    pool.postCellGroup(VoxelCellGroup{27, 8});

    const std::vector<VoxelCellGroup> &groups = pool.getCellGroups();
    ASSERT_EQ(groups.size(), 3u);
    EXPECT_EQ(groups[0].start_, 0u);
    EXPECT_EQ(groups[1].start_, 27u);
    EXPECT_EQ(groups[2].start_, 64u);
}

TEST(VoxelPoolCellGroups, HostingLatchSurvivesTheFrameClear) {
    C_VoxelPool pool{ivec3(4, 4, 4)};
    EXPECT_FALSE(pool.hostsCellGroups());
    pool.postCellGroup(VoxelCellGroup{0, 8});
    pool.clearCellGroups();

    EXPECT_TRUE(pool.getCellGroups().empty());
    EXPECT_TRUE(pool.hostsCellGroups());
}

TEST(VoxelPoolCellGroups, PostingOneSpanTwiceFiresTheGuard) {
    C_VoxelPool pool{ivec3(4, 4, 4)};
    pool.postCellGroup(VoxelCellGroup{0, 8});

    EXPECT_THROW(pool.postCellGroup(VoxelCellGroup{0, 8}), std::runtime_error);
    EXPECT_EQ(pool.getCellGroups().size(), 1u);
}

TEST(VoxelPoolCellGroups, GroupPastThePoolFiresTheGuard) {
    C_VoxelPool pool{ivec3(2, 2, 2)};

    EXPECT_THROW(pool.postCellGroup(VoxelCellGroup{4, 8}), std::runtime_error);
    EXPECT_TRUE(pool.getCellGroups().empty());
}

} // namespace

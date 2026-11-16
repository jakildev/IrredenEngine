#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_static_settings.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/common/static_residents.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/system/components/component_static.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>

#include <cstddef>
#include <cstring>
#include <vector>

// The motion-control half of docs/design/static-residents.md, gate 4: a
// static entity is compared with an always-dynamic twin after every step of
// its life. The twin gets the same input writes, so at each step the world
// transform and every pool global position must be bit-equal.

namespace {

using IRComponents::C_LocalTransform;
using IRComponents::C_Settled;
using IRComponents::C_Static;
using IRComponents::C_StaticSettings;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::C_WorldTransform;
using IREntity::EntityId;
using IRMath::Color;
using IRMath::ivec3;
using IRMath::vec3;
namespace Static = IRPrefab::Static;

constexpr ivec3 kSetSize{2, 1, 2};
constexpr vec3 kStart{3.0f, -4.0f, 1.0f};
constexpr vec3 kMove{2.0f, 5.0f, -1.0f};

class StaticResident : public testing::Test {
  protected:
    StaticResident()
        : m_entityManager{}
        , m_systemManager{} {
        m_canvas = IREntity::createEntity(C_VoxelPool{ivec3(32, 32, 8)});
        IRRender::setHeadlessActiveCanvasEntity(m_canvas);
        m_propagate = IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>();
        m_children = IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>();
        m_systemManager.registerPipeline(IRTime::Events::UPDATE, {m_propagate});
        m_systemManager.registerPipeline(IRTime::Events::RENDER, {m_children});
    }

    ~StaticResident() override {
        IRRender::setHeadlessActiveCanvasEntity(IREntity::kNullEntity);
    }

    EntityId makeSet(vec3 translation, bool isStatic) {
        const C_VoxelSetNew set{kSetSize, Color{200, 120, 60, 255}, false, m_canvas};
        return isStatic ? IREntity::createEntity(C_LocalTransform{translation}, set, C_Static{})
                        : IREntity::createEntity(C_LocalTransform{translation}, set);
    }

    // One rendered frame with one fixed update, with the drain where the
    // World runs it.
    void frame() {
        m_systemManager.executePipeline(IRTime::Events::UPDATE);
        Static::drain();
        m_systemManager.executePipeline(IRTime::Events::RENDER);
        Static::drain();
    }

    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            frame();
        }
    }

    C_VoxelPool &pool() {
        return IREntity::getComponent<C_VoxelPool>(m_canvas);
    }

    std::vector<vec3> globalPositions(EntityId entity) {
        const C_VoxelSetNew &set = IREntity::getComponent<C_VoxelSetNew>(entity);
        std::vector<vec3> positions;
        for (int v = 0; v < set.numVoxels_; ++v) {
            positions.push_back(
                pool().getPositionGlobals()[set.voxelStartIdx_ + static_cast<std::size_t>(v)].pos_
            );
        }
        return positions;
    }

    // World transform and pool global positions, bit for bit.
    void expectEqualsTwin(EntityId entity, EntityId twin, const char *step) {
        const C_WorldTransform &world = IREntity::getComponent<C_WorldTransform>(entity);
        const C_WorldTransform &twinWorld = IREntity::getComponent<C_WorldTransform>(twin);
        EXPECT_EQ(std::memcmp(&world, &twinWorld, sizeof(C_WorldTransform)), 0) << step;
        const std::vector<vec3> positions = globalPositions(entity);
        const std::vector<vec3> twinPositions = globalPositions(twin);
        ASSERT_EQ(positions.size(), twinPositions.size()) << step;
        ASSERT_FALSE(positions.empty()) << step;
        EXPECT_EQ(
            std::memcmp(positions.data(), twinPositions.data(), positions.size() * sizeof(vec3)),
            0
        ) << step;
    }

    void moveBoth(EntityId entity, EntityId twin, vec3 delta) {
        IREntity::getComponent<C_LocalTransform>(entity).translation_ += delta;
        IREntity::getComponent<C_LocalTransform>(twin).translation_ += delta;
    }

    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    EntityId m_canvas = IREntity::kNullEntity;
    IRSystem::SystemId m_propagate = IRSystem::kNullSystemId;
    IRSystem::SystemId m_children = IRSystem::kNullSystemId;
};

TEST_F(StaticResident, CreatedStaticSettlesAndMatchesTwinThroughItsLife) {
    const EntityId entity = makeSet(kStart, true);
    const EntityId twin = makeSet(kStart, false);

    EXPECT_TRUE(Static::isStatic(entity));
    EXPECT_FALSE(Static::isSettled(entity));
    frames(2);
    EXPECT_TRUE(Static::isSettled(entity)) << "settled within two frames of creation";
    expectEqualsTwin(entity, twin, "settled");

    frames(3);
    expectEqualsTwin(entity, twin, "settled, later frames");

    Static::thaw(entity);
    frame();
    EXPECT_FALSE(Static::isStatic(entity));
    expectEqualsTwin(entity, twin, "thawed");

    moveBoth(entity, twin, kMove);
    frame();
    expectEqualsTwin(entity, twin, "moved");

    EXPECT_EQ(Static::freeze(entity), Static::FreezeResult::FROZEN);
    frame();
    EXPECT_TRUE(Static::isStatic(entity));
    expectEqualsTwin(entity, twin, "frozen");

    frames(2);
    EXPECT_TRUE(Static::isSettled(entity));
    expectEqualsTwin(entity, twin, "settled again");

    moveBoth(entity, twin, kMove);
    Static::resettle(entity);
    frames(3);
    EXPECT_TRUE(Static::isSettled(entity));
    expectEqualsTwin(entity, twin, "resettled after a raw write");
}

TEST_F(StaticResident, SettledEntityIsNotMatchedByTheDerivingSystems) {
    const EntityId entity = makeSet(kStart, true);
    frames(2);
    ASSERT_TRUE(Static::isSettled(entity));

    m_systemManager.setTimingEnabled(true);
    m_systemManager.resetTimingStats();
    frames(4);
    // The canvas entity carries the transform pair and is dynamic.
    EXPECT_EQ(m_systemManager.getTimingAccum(m_propagate).totalEntityCount_, 4u);
    EXPECT_EQ(m_systemManager.getTimingAccum(m_children).totalEntityCount_, 0u);
    EXPECT_EQ(m_systemManager.getTimingAccum(m_children).callCount_, 4u);
}

TEST_F(StaticResident, SettledEntityQueuesNoPositionsAndInvalidatesNoCullChunk) {
    makeSet(kStart, true);
    frames(2);
    pool().clearPendingPositionRanges();
    frames(4);
    EXPECT_TRUE(pool().getPendingPositionRanges().empty());
}

TEST_F(StaticResident, RawWriteIsRetainedAndNotDerivedUntilResettle) {
    const EntityId entity = makeSet(kStart, true);
    const EntityId twin = makeSet(kStart, false);
    frames(2);
    ASSERT_TRUE(Static::isSettled(entity));
    const C_WorldTransform before = IREntity::getComponent<C_WorldTransform>(entity);
    const std::vector<vec3> positionsBefore = globalPositions(entity);

    moveBoth(entity, twin, kMove);
    frames(3);
    EXPECT_EQ(IREntity::getComponent<C_LocalTransform>(entity).translation_, kStart + kMove)
        << "the raw write is stored";
    const C_WorldTransform &after = IREntity::getComponent<C_WorldTransform>(entity);
    EXPECT_EQ(std::memcmp(&before, &after, sizeof(C_WorldTransform)), 0)
        << "derived pose does not follow a raw write";
    EXPECT_EQ(globalPositions(entity), positionsBefore);

    Static::resettle(entity);
    frames(3);
    expectEqualsTwin(entity, twin, "resettle applies the raw write");
}

TEST_F(StaticResident, PromotionIsCappedPerRoundLowestStampFirst) {
    IREntity::singleton<C_StaticSettings>().promotionsPerRound_ = 2;
    std::vector<EntityId> entities;
    for (int i = 0; i < 5; ++i) {
        entities.push_back(makeSet(kStart + vec3(static_cast<float>(i) * 4.0f, 0.0f, 0.0f), true));
    }
    frames(2);
    EXPECT_EQ(Static::stats().settled_, 2u);
    frame();
    EXPECT_EQ(Static::stats().settled_, 4u);
    frame();
    EXPECT_EQ(Static::stats().settled_, 5u);
    EXPECT_EQ(Static::stats().settling_, 0u);
    EXPECT_EQ(Static::stats().counters_.promoted_, 5u);
}

TEST_F(StaticResident, AnyOtherArchetypeMoveReturnsASettledEntityToSettling) {
    struct C_Unrelated {
        int value_ = 0;
    };
    const EntityId entity = makeSet(kStart, true);
    frames(2);
    ASSERT_TRUE(Static::isSettled(entity));

    IREntity::setComponent(entity, C_Unrelated{7});
    EXPECT_TRUE(Static::isStatic(entity));
    EXPECT_FALSE(Static::isSettled(entity));
    frames(2);
    EXPECT_TRUE(Static::isSettled(entity));

    IREntity::removeComponent<C_Unrelated>(entity);
    EXPECT_FALSE(Static::isSettled(entity));
    frames(2);
    EXPECT_TRUE(Static::isSettled(entity));
}

TEST_F(StaticResident, ThawRemovesBothTagsInOneMove) {
    const EntityId entity = makeSet(kStart, true);
    frames(2);
    ASSERT_TRUE(Static::isSettled(entity));
    Static::thaw(entity);
    EXPECT_TRUE(Static::isSettled(entity)) << "thaw is deferred to the next flush";
    IREntity::flushStructuralChanges();
    EXPECT_FALSE(Static::isStatic(entity));
    EXPECT_FALSE(Static::isSettled(entity));
    Static::drain();
    EXPECT_EQ(Static::stats().counters_.thawed_, 1u);
}

} // namespace

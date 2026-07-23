#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/entity/entity_manager.hpp>

namespace {

struct C_ValueA {
    int n_ = 0;
};
struct C_ValueB {
    int m_ = 0;
};

} // namespace

namespace IRSystem {

// Specialization with all four hooks (tick, beginTick, endTick) plus a
// member field (`scaleFromBegin_`) that beginTick writes and tick reads.
// The member-field shape is the helper's whole point — verify it
// persists across ticks within a frame and is reset frame-to-frame as
// beginTick re-seeds it.
template <> struct System<TEST_REGISTER_SYSTEM_A> {
    int scaleFromBegin_ = 0;
    int beginCount_ = 0;
    int endCount_ = 0;
    int tickCount_ = 0;

    void beginTick() {
        beginCount_++;
        scaleFromBegin_ = beginCount_ * 10;
    }

    void tick(C_ValueA &v) {
        tickCount_++;
        v.n_ += scaleFromBegin_;
    }

    void endTick() {
        endCount_++;
    }

    static SystemId create() {
        return registerSystem<TEST_REGISTER_SYSTEM_A, C_ValueA>("RegisterSystemTestA");
    }
};

// Second specialization to exercise per-instance state separation —
// two distinct System<N> specializations own independent params instances
// with no cross-talk between different SystemName types.
template <> struct System<TEST_REGISTER_SYSTEM_B> {
    int hits_ = 0;

    void tick(C_ValueB &v) {
        hits_++;
        v.m_ += 1;
    }

    static SystemId create() {
        return registerSystem<TEST_REGISTER_SYSTEM_B, C_ValueB>("RegisterSystemTestB");
    }
};

} // namespace IRSystem

namespace {

class RegisterSystemTest : public testing::Test {
  protected:
    RegisterSystemTest()
        : m_entity_manager{}
        , m_system_manager{} {}

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(RegisterSystemTest, MemberTickFiresPerEntity) {
    auto idA = IREntity::createEntity(C_ValueA{0});
    auto idB = IREntity::createEntity(C_ValueA{0});

    auto sysId = IRSystem::createSystem<IRSystem::TEST_REGISTER_SYSTEM_A>();
    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysId});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    // beginTick set scaleFromBegin_ = 10; tick added 10 to each C_ValueA.
    EXPECT_EQ(IREntity::getComponent<C_ValueA>(idA).n_, 10);
    EXPECT_EQ(IREntity::getComponent<C_ValueA>(idB).n_, 10);
}

TEST_F(RegisterSystemTest, MemberFieldsPersistAcrossTicks) {
    auto idA = IREntity::createEntity(C_ValueA{0});

    auto sysId = IRSystem::createSystem<IRSystem::TEST_REGISTER_SYSTEM_A>();
    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysId});

    m_system_manager.executePipeline(IRTime::Events::UPDATE); // begin=1, scale=10, +10
    m_system_manager.executePipeline(IRTime::Events::UPDATE); // begin=2, scale=20, +20
    m_system_manager.executePipeline(IRTime::Events::UPDATE); // begin=3, scale=30, +30

    auto *params =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::TEST_REGISTER_SYSTEM_A>>(sysId);
    ASSERT_NE(params, nullptr);
    EXPECT_EQ(params->beginCount_, 3) << "beginTick fired once per pipeline execution";
    EXPECT_EQ(params->endCount_, 3) << "endTick fired once per pipeline execution";
    EXPECT_EQ(params->tickCount_, 3) << "per-entity tick fired once per execution per entity";

    // scaleFromBegin_ was 10, 20, 30 on executions 1-3; C_ValueA.n_ accumulated 10+20+30=60.
    EXPECT_EQ(IREntity::getComponent<C_ValueA>(idA).n_, 60);
}

TEST_F(RegisterSystemTest, BeginAndEndFireWithZeroEntities) {
    auto sysId = IRSystem::createSystem<IRSystem::TEST_REGISTER_SYSTEM_A>();
    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysId});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    auto *params =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::TEST_REGISTER_SYSTEM_A>>(sysId);
    ASSERT_NE(params, nullptr);
    EXPECT_EQ(params->beginCount_, 1);
    EXPECT_EQ(params->endCount_, 1);
    EXPECT_EQ(params->tickCount_, 0);
}

TEST_F(RegisterSystemTest, DistinctSystemsHaveIndependentParams) {
    IREntity::createEntity(C_ValueA{0});
    IREntity::createEntity(C_ValueB{0});

    auto sysA = IRSystem::createSystem<IRSystem::TEST_REGISTER_SYSTEM_A>();
    auto sysB = IRSystem::createSystem<IRSystem::TEST_REGISTER_SYSTEM_B>();
    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysA, sysB});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    auto *paramsA =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::TEST_REGISTER_SYSTEM_A>>(sysA);
    auto *paramsB =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::TEST_REGISTER_SYSTEM_B>>(sysB);
    ASSERT_NE(paramsA, nullptr);
    ASSERT_NE(paramsB, nullptr);
    EXPECT_EQ(paramsA->beginCount_, 2);
    EXPECT_EQ(paramsB->hits_, 2);
}

TEST_F(RegisterSystemTest, ParamsAccessibleAfterCreate) {
    auto sysId = IRSystem::createSystem<IRSystem::TEST_REGISTER_SYSTEM_A>();
    auto *params =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::TEST_REGISTER_SYSTEM_A>>(sysId);
    ASSERT_NE(params, nullptr) << "registerSystem must populate the params slot";
    EXPECT_EQ(params->beginCount_, 0);
    EXPECT_EQ(params->scaleFromBegin_, 0);
}

// #2526 — the SystemName registry. Registration self-wires, which is what lets
// the prefab handles drop their manual wire-once setters.
TEST_F(RegisterSystemTest, FindSystemResolvesEachRegisteredName) {
    auto sysA = IRSystem::createSystem<IRSystem::TEST_REGISTER_SYSTEM_A>();
    auto sysB = IRSystem::createSystem<IRSystem::TEST_REGISTER_SYSTEM_B>();
    ASSERT_NE(sysA, sysB);

    EXPECT_EQ(IRSystem::findSystem(IRSystem::TEST_REGISTER_SYSTEM_A), sysA);
    EXPECT_EQ(IRSystem::findSystem(IRSystem::TEST_REGISTER_SYSTEM_B), sysB);
}

TEST_F(RegisterSystemTest, FindSystemReportsANameThatWasNeverCreated) {
    // Only A is created, so B has no entry — the miss value is the same
    // `kNullEntity` the wire-once globals used as their "unwired" sentinel, so
    // `system()` / `allocator()` null-return semantics are unchanged.
    IRSystem::createSystem<IRSystem::TEST_REGISTER_SYSTEM_A>();

    EXPECT_EQ(IRSystem::findSystem(IRSystem::TEST_REGISTER_SYSTEM_B), IREntity::kNullEntity);
}

// Deliberately NOT a RegisterSystemTest fixture case: the guard under test is
// that `findSystem` tolerates a null `g_systemManager`. The prefab handles are
// reached from headless contexts that tick a `System<N>` against a bare
// `EntityManager` with no `World` (see test/ecs/voxel_bone_slot_seed_test.cpp),
// and the wire-once globals this registry replaces checked their sentinel
// BEFORE touching the manager. Without the guard this segfaults.
TEST(FindSystemNoManagerTest, ReportsNullWhenNoSystemManagerExists) {
    ASSERT_EQ(IRSystem::g_systemManager, nullptr)
        << "precondition: no SystemManager is alive between fixtures";

    EXPECT_EQ(IRSystem::findSystem(IRSystem::TEST_REGISTER_SYSTEM_A), IREntity::kNullEntity);
}

} // namespace

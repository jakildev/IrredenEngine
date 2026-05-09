#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/entity/entity_manager.hpp>

namespace {

struct ValueA {
    int n_ = 0;
};
struct ValueB {
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

    void tick(ValueA &v) {
        tickCount_++;
        v.n_ += scaleFromBegin_;
    }

    void endTick() {
        endCount_++;
    }

    static SystemId create() {
        return registerSystem<TEST_REGISTER_SYSTEM_A, ValueA>("RegisterSystemTestA");
    }
};

// Second specialization to exercise per-instance state separation —
// the same SystemName cannot be registered twice (the params slot is
// per SystemId), but two distinct System<NAME>s should each own their
// own params instance with no cross-talk.
template <> struct System<TEST_REGISTER_SYSTEM_B> {
    int hits_ = 0;

    void tick(ValueB &v) {
        hits_++;
        v.m_ += 1;
    }

    static SystemId create() {
        return registerSystem<TEST_REGISTER_SYSTEM_B, ValueB>("RegisterSystemTestB");
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
    auto idA = IREntity::createEntity(ValueA{0});
    auto idB = IREntity::createEntity(ValueA{0});

    auto sysId = IRSystem::createSystem<IRSystem::TEST_REGISTER_SYSTEM_A>();
    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysId});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    // beginTick set scaleFromBegin_ = 10; tick added 10 to each ValueA.
    EXPECT_EQ(IREntity::getComponent<ValueA>(idA).n_, 10);
    EXPECT_EQ(IREntity::getComponent<ValueA>(idB).n_, 10);
}

TEST_F(RegisterSystemTest, MemberFieldsPersistAcrossTicks) {
    auto idA = IREntity::createEntity(ValueA{0});

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

    // beginCount_ accumulated 1+2+3=6 in the field; scaleFromBegin_ = 10*beginCount_ each tick.
    // ValueA.n_ accumulated 10 + 20 + 30 = 60.
    EXPECT_EQ(IREntity::getComponent<ValueA>(idA).n_, 60);
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
    IREntity::createEntity(ValueA{0});
    IREntity::createEntity(ValueB{0});

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

} // namespace

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_job.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/job/job_manager.hpp>

#include <atomic>
#include <memory>
#include <stdexcept>

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

struct CountingTickObserver : IRSystem::TickObserver {
    explicit CountingTickObserver(int &destructions)
        : destructions_{destructions} {}

    ~CountingTickObserver() override {
        ++destructions_;
    }
    void onBeforeTick(IRSystem::SystemId) override {}
    void onAfterTick(IRSystem::SystemId) override {}

    int &destructions_;
};

struct DerivedCountingTickObserver : CountingTickObserver {
    using CountingTickObserver::CountingTickObserver;
};

struct OtherTickObserver : IRSystem::TickObserver {
    void onBeforeTick(IRSystem::SystemId) override {}
    void onAfterTick(IRSystem::SystemId) override {}
};

TEST(TickObserverLookupTest, FindsFirstCastableObserverAndReportsTypeMisses) {
    int destructions = 0;
    IRSystem::SystemManager manager;
    EXPECT_EQ(manager.findTickObserver<CountingTickObserver>(), nullptr);
    auto unrelated = std::make_unique<OtherTickObserver>();
    auto *unrelatedPointer = unrelated.get();
    manager.registerTickObserver(std::move(unrelated));
    EXPECT_EQ(manager.findTickObserver<CountingTickObserver>(), nullptr);
    auto first = std::make_unique<DerivedCountingTickObserver>(destructions);
    auto *firstPointer = first.get();
    manager.registerTickObserver(std::move(first));
    manager.registerTickObserver(std::make_unique<CountingTickObserver>(destructions));

    EXPECT_EQ(manager.findTickObserver<IRSystem::TickObserver>(), unrelatedPointer);
    EXPECT_EQ(manager.findTickObserver<OtherTickObserver>(), unrelatedPointer);
    EXPECT_EQ(manager.findTickObserver<CountingTickObserver>(), firstPointer);
    EXPECT_EQ(manager.findTickObserver<DerivedCountingTickObserver>(), firstPointer);
}

TEST(TickObserverLookupTest, SimultaneouslyLiveManagersKeepSeparateObservers) {
    int destructions = 0;
    IRSystem::SystemManager firstManager;
    IRSystem::SystemManager secondManager;
    auto first = std::make_unique<CountingTickObserver>(destructions);
    auto second = std::make_unique<CountingTickObserver>(destructions);
    auto *firstPointer = first.get();
    auto *secondPointer = second.get();
    firstManager.registerTickObserver(std::move(first));
    EXPECT_EQ(secondManager.findTickObserver<CountingTickObserver>(), nullptr);
    secondManager.registerTickObserver(std::move(second));

    EXPECT_EQ(firstManager.findTickObserver<CountingTickObserver>(), firstPointer);
    EXPECT_EQ(secondManager.findTickObserver<CountingTickObserver>(), secondPointer);
    firstManager.clearTickObservers();
    EXPECT_EQ(firstManager.findTickObserver<CountingTickObserver>(), nullptr);
    EXPECT_EQ(secondManager.findTickObserver<CountingTickObserver>(), secondPointer);
    EXPECT_EQ(destructions, 1);
}

TEST(TickObserverLookupTest, UnregisterAndClearRemoveOwnedMatchesAndAllowRegistration) {
    int destructions = 0;
    IRSystem::SystemManager manager;
    const auto firstId =
        manager.registerTickObserver(std::make_unique<CountingTickObserver>(destructions));
    auto second = std::make_unique<CountingTickObserver>(destructions);
    auto *secondPointer = second.get();
    manager.registerTickObserver(std::move(second));
    manager.unregisterTickObserver(firstId);
    EXPECT_EQ(destructions, 1);
    EXPECT_EQ(manager.findTickObserver<CountingTickObserver>(), secondPointer);
    manager.clearTickObservers();
    EXPECT_EQ(destructions, 2);
    EXPECT_EQ(manager.findTickObserver<CountingTickObserver>(), nullptr);
    manager.clearTickObservers();
    EXPECT_EQ(destructions, 2);
    auto replacement = std::make_unique<CountingTickObserver>(destructions);
    auto *replacementPointer = replacement.get();
    manager.registerTickObserver(std::move(replacement));
    EXPECT_EQ(manager.findTickObserver<CountingTickObserver>(), replacementPointer);
}

#ifndef IR_RELEASE
TEST(TickObserverLookupTest, RejectsWorkerLookup) {
    IRJob::JobManager jobs(2);
    IRSystem::SystemManager manager;
    std::atomic<bool> rejected{false};
    std::atomic<bool> unexpectedException{false};
    IRJob::pinTo(1, [&] {
        // Exceptions must not escape an enkiTS worker task.
        try {
            manager.findTickObserver<OtherTickObserver>();
        } catch (const std::runtime_error &) {
            rejected.store(true);
        } catch (...) {
            unexpectedException.store(true);
        }
    });
    EXPECT_TRUE(rejected.load());
    EXPECT_FALSE(unexpectedException.load());
    EXPECT_EQ(manager.findTickObserver<OtherTickObserver>(), nullptr);
}
#endif

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

// Registration populates the SystemName registry automatically, allowing
// prefab handles to omit manual wire-once setters.
TEST_F(RegisterSystemTest, FindSystemResolvesEachRegisteredName) {
    auto sysA = IRSystem::createSystem<IRSystem::TEST_REGISTER_SYSTEM_A>();
    auto sysB = IRSystem::createSystem<IRSystem::TEST_REGISTER_SYSTEM_B>();
    ASSERT_NE(sysA, sysB);

    EXPECT_EQ(IRSystem::findSystem(IRSystem::TEST_REGISTER_SYSTEM_A), sysA);
    EXPECT_EQ(IRSystem::findSystem(IRSystem::TEST_REGISTER_SYSTEM_B), sysB);
}

TEST_F(RegisterSystemTest, FindSystemReportsANameThatWasNeverCreated) {
    // Only A is created, so B has no entry — the miss value is `kNullSystemId`,
    // which the prefab handles compare against for their "unwired -> nullptr"
    // branch, so `system()` / `allocator()` null-return semantics are unchanged.
    IRSystem::createSystem<IRSystem::TEST_REGISTER_SYSTEM_A>();

    EXPECT_EQ(IRSystem::findSystem(IRSystem::TEST_REGISTER_SYSTEM_B), IRSystem::kNullSystemId);
}

// The miss sentinel has to be unreachable as a real id, and id 0 — handed to
// whichever system registers first — is the one value a `kNullEntity`-based
// sentinel could not express.
TEST_F(RegisterSystemTest, FirstRegisteredSystemIsDistinguishableFromAMiss) {
    // A is the first system this fixture creates, so it holds id 0; B is never
    // created. The ASSERT pins that premise: if SystemManager ever
    // pre-registers a system or stops counting from 0, it fails here rather
    // than letting the EXPECT_NE below pass vacuously.
    const auto sysA = IRSystem::createSystem<IRSystem::TEST_REGISTER_SYSTEM_A>();
    ASSERT_EQ(sysA, 0u) << "premise: the first system registered holds id 0";

    EXPECT_EQ(IRSystem::findSystem(IRSystem::TEST_REGISTER_SYSTEM_A), sysA);
    EXPECT_NE(
        IRSystem::findSystem(IRSystem::TEST_REGISTER_SYSTEM_A),
        IRSystem::findSystem(IRSystem::TEST_REGISTER_SYSTEM_B)
    ) << "registered-first must not read back as never-registered";
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

    EXPECT_EQ(IRSystem::findSystem(IRSystem::TEST_REGISTER_SYSTEM_A), IRSystem::kNullSystemId);
}

} // namespace

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_job.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/job/job_manager.hpp>
#include <irreden/system/components/component_static.hpp>

#include <cstdint>
#include <stdexcept>
#include <vector>

// The static-resident matching policy and the round counters
// (docs/design/static-residents.md, "The matching policy" and "Settle").
// Every registration and query lane is driven against one dynamic, one
// settling and one settled entity; the tags are attached directly, so no
// transition step is involved.

namespace IRSystem {

template <> struct System<TEST_STATIC_POSE_WRITER> {
    int visits_ = 0;
    void tick(IRComponents::C_LocalTransform &) {
        ++visits_;
    }
    static SystemId create() {
        return registerSystem<TEST_STATIC_POSE_WRITER, IRComponents::C_LocalTransform>(
            "TestStaticPoseWriter"
        );
    }
};

template <> struct System<TEST_STATIC_POSE_READER> {
    int visits_ = 0;
    void tick(const IRComponents::C_LocalTransform &) {
        ++visits_;
    }
    static SystemId create() {
        return registerSystem<TEST_STATIC_POSE_READER, const IRComponents::C_LocalTransform>(
            "TestStaticPoseReader"
        );
    }
};

template <> struct System<TEST_STATIC_DERIVING> {
    int visits_ = 0;
    void tick(const IRComponents::C_LocalTransform &, IRComponents::C_WorldTransform &) {
        ++visits_;
    }
    static SystemId create() {
        return registerSystem<
            TEST_STATIC_DERIVING,
            const IRComponents::C_LocalTransform,
            IRComponents::C_WorldTransform,
            Exclude<IRComponents::C_Settled>>("TestStaticDeriving");
    }
};

} // namespace IRSystem

namespace {

using IRComponents::C_LocalTransform;
using IRComponents::C_Settled;
using IRComponents::C_Static;
using IRComponents::C_WorldTransform;
using IREntity::EntityId;
using IRSystem::Exclude;
using IRSystem::SystemId;

struct C_PolicyA {
    int value_ = 0;
};
struct C_PolicyB {
    int value_ = 0;
};

class StaticPolicy : public testing::Test {
  protected:
    void makePopulation() {
        m_dynamic = IREntity::createEntity(C_PolicyA{}, C_PolicyB{});
        m_settling = IREntity::createEntity(C_PolicyA{}, C_PolicyB{}, C_Static{});
        m_settled = IREntity::createEntity(C_PolicyA{}, C_PolicyB{}, C_Static{}, C_Settled{});
    }

    int visits(SystemId system) {
        m_systemManager.setTimingEnabled(true);
        m_systemManager.resetTimingStats();
        m_systemManager.executeSystem(system);
        return static_cast<int>(m_systemManager.getTimingAccum(system).totalWalkedCount_);
    }

    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    EntityId m_dynamic = IREntity::kNullEntity;
    EntityId m_settling = IREntity::kNullEntity;
    EntityId m_settled = IREntity::kNullEntity;
};

TEST_F(StaticPolicy, CreateSystemLaneFollowsTheDeclaredWriteSet) {
    makePopulation();
    int writerVisits = 0;
    const SystemId writer = IRSystem::createSystem<C_LocalTransform>(
        "Writer",
        [&writerVisits](C_LocalTransform &) { ++writerVisits; }
    );
    const SystemId reader = IRSystem::createSystem<const C_LocalTransform>(
        "Reader",
        [](const C_LocalTransform &) {}
    );
    const SystemId derivedWriter =
        IRSystem::createSystem<C_WorldTransform>("DerivedWriter", [](C_WorldTransform &) {});
    const SystemId deriving = IRSystem::createSystem<C_WorldTransform, Exclude<C_Settled>>(
        "Deriving",
        [](C_WorldTransform &) {}
    );

    EXPECT_EQ(visits(writer), 1) << "a pose writer never visits a settling or settled entity";
    EXPECT_EQ(writerVisits, 1);
    EXPECT_EQ(visits(reader), 3) << "a const reader visits all three";
    EXPECT_EQ(visits(derivedWriter), 1) << "a derived-pose writer that is not deriving";
    EXPECT_EQ(visits(deriving), 2) << "a deriving system visits settling rows, not settled";
    EXPECT_FALSE(m_systemManager.isDerivingSystem(writer));
    EXPECT_FALSE(m_systemManager.isDerivingSystem(derivedWriter));
    EXPECT_TRUE(m_systemManager.isDerivingSystem(deriving));
}

TEST_F(StaticPolicy, AlsoWritesCountsAsADeclaredWrite) {
    makePopulation();
    const SystemId foreignWriter =
        IRSystem::createSystem<C_PolicyA, IRSystem::AlsoWrites<C_LocalTransform>>(
            "ForeignWriter",
            [](C_PolicyA &) {}
        );
    const SystemId unrelated = IRSystem::createSystem<C_PolicyA>("Unrelated", [](C_PolicyA &) {});
    EXPECT_EQ(visits(foreignWriter), 1);
    EXPECT_EQ(visits(unrelated), 3) << "a system that names no pose component is unchanged";
}

TEST_F(StaticPolicy, RegisterSystemLaneMatchesTheSameRows) {
    makePopulation();
    const SystemId writer = IRSystem::createSystem<IRSystem::TEST_STATIC_POSE_WRITER>();
    const SystemId reader = IRSystem::createSystem<IRSystem::TEST_STATIC_POSE_READER>();
    const SystemId deriving = IRSystem::createSystem<IRSystem::TEST_STATIC_DERIVING>();
    EXPECT_EQ(visits(writer), 1);
    EXPECT_EQ(visits(reader), 3);
    EXPECT_EQ(visits(deriving), 2);
    using Writer = IRSystem::System<IRSystem::TEST_STATIC_POSE_WRITER>;
    EXPECT_EQ(IRSystem::getSystemParams<Writer>(writer)->visits_, 1);
    EXPECT_TRUE(m_systemManager.isDerivingSystem(deriving));
}

TEST_F(StaticPolicy, DynamicLaneTreatsEveryColumnAsWrittenUnlessReadOnly) {
    makePopulation();
    const IREntity::Archetype local = IREntity::getArchetype<C_LocalTransform>();
    const IREntity::Archetype world = IREntity::getArchetype<C_WorldTransform>();
    const auto body = [](IREntity::ArchetypeNode *) {};
    const SystemId writer = IRSystem::createSystemDynamic("DynWriter", local, {}, body);
    const SystemId reader = IRSystem::createSystemDynamic(
        "DynReader",
        local,
        {},
        body,
        IRSystem::Concurrency::SERIAL,
        local
    );
    const SystemId deriving = IRSystem::createSystemDynamic(
        "DynDeriving",
        world,
        IREntity::getArchetype<C_Settled>(),
        body
    );
    EXPECT_EQ(visits(writer), 1);
    EXPECT_EQ(visits(reader), 3);
    EXPECT_EQ(visits(deriving), 2);
}

TEST_F(StaticPolicy, QueryLanesFollowTheSameRule) {
    makePopulation();
    int writes = 0;
    IRSystem::executeQuery<C_LocalTransform>([&writes](C_LocalTransform &) { ++writes; });
    int reads = 0;
    IRSystem::executeQuery<const C_LocalTransform>([&reads](const C_LocalTransform &) {
        ++reads;
    });
    int derived = 0;
    IRSystem::executeQuery<C_WorldTransform, Exclude<C_Settled>>([&derived](C_WorldTransform &) {
        ++derived;
    });
    int dynamicRows = 0;
    IRSystem::executeQueryDynamic(
        IREntity::getArchetype<C_LocalTransform>(),
        {},
        [&dynamicRows](IREntity::ArchetypeNode *node) { dynamicRows += node->length_; }
    );
    EXPECT_EQ(writes, 1);
    EXPECT_EQ(reads, 3);
    EXPECT_EQ(derived, 2);
    EXPECT_EQ(dynamicRows, 1) << "a runtime query counts every included component as written";
}

TEST_F(StaticPolicy, ASystemCreatedBeforeAnyEntityMatchesLikeOneCreatedAfter) {
    const SystemId early =
        IRSystem::createSystem<C_LocalTransform>("Early", [](C_LocalTransform &) {});
    makePopulation();
    const SystemId late =
        IRSystem::createSystem<C_LocalTransform>("Late", [](C_LocalTransform &) {});
    EXPECT_EQ(visits(early), 1);
    EXPECT_EQ(visits(late), 1);
}

TEST_F(StaticPolicy, ExcludingSettledAfterCreationMakesADerivedWriterDeriving) {
    makePopulation();
    const SystemId system =
        IRSystem::createSystem<C_WorldTransform>("LateDeriving", [](C_WorldTransform &) {});
    EXPECT_EQ(visits(system), 1);
    IRSystem::addSystemExcludeTag<C_Settled>(system);
    EXPECT_TRUE(m_systemManager.isDerivingSystem(system));
    EXPECT_EQ(visits(system), 2);
}

#ifndef IR_RELEASE
TEST_F(StaticPolicy, ADerivingSystemThatWritesAPoseInputIsARegistrationError) {
    const auto registerBadDeriving = [] {
        IRSystem::createSystem<C_LocalTransform, Exclude<C_Settled>>(
            "BadDeriving",
            [](C_LocalTransform &) {}
        );
    };
    EXPECT_THROW(registerBadDeriving(), std::runtime_error);
}
#endif

TEST_F(StaticPolicy, ConstPackDispatchesEveryTickForm) {
    makePopulation();
    int rows = 0;
    int ids = 0;
    int batchRows = 0;
    const SystemId row = IRSystem::createSystem<const C_PolicyA, C_PolicyB>(
        "ConstRow",
        [&rows](const C_PolicyA &, C_PolicyB &b) {
            ++rows;
            ++b.value_;
        }
    );
    const SystemId withId = IRSystem::createSystem<const C_PolicyA>(
        "ConstId",
        [&ids](EntityId, const C_PolicyA &) { ++ids; }
    );
    const SystemId batch = IRSystem::createSystem<const C_PolicyA>(
        "ConstBatch",
        [&batchRows](
            const IREntity::Archetype &,
            std::vector<EntityId> &entities,
            const std::vector<C_PolicyA> &
        ) { batchRows += static_cast<int>(entities.size()); }
    );
    m_systemManager.executeSystem(row);
    m_systemManager.executeSystem(withId);
    m_systemManager.executeSystem(batch);
    EXPECT_EQ(rows, 3);
    EXPECT_EQ(ids, 3);
    EXPECT_EQ(batchRows, 3);
    EXPECT_EQ(IREntity::getComponent<C_PolicyB>(m_dynamic).value_, 1);
    EXPECT_TRUE(m_systemManager.getSystemAccess(row).readsType<C_PolicyA>());
    EXPECT_TRUE(m_systemManager.getSystemAccess(row).writesType<C_PolicyB>());
}

TEST_F(StaticPolicy, AWholeNodeTickReportsTheRowsItWalked) {
    makePopulation();
    const SystemId silent = IRSystem::createSystem<C_PolicyA>(
        "SilentBatch",
        [](const IREntity::Archetype &, std::vector<EntityId> &, std::vector<C_PolicyA> &) {}
    );
    const SystemId reporting = IRSystem::createSystem<C_PolicyA>(
        "ReportingBatch",
        [](const IREntity::Archetype &, std::vector<EntityId> &, std::vector<C_PolicyA> &) {
            IRSystem::reportRowsWalked(1);
        }
    );
    m_systemManager.setTimingEnabled(true);
    m_systemManager.executeSystem(silent);
    m_systemManager.executeSystem(reporting);
    EXPECT_EQ(m_systemManager.getTimingAccum(silent).totalEntityCount_, 3u);
    EXPECT_EQ(m_systemManager.getTimingAccum(silent).totalWalkedCount_, 3u)
        << "a whole-node tick that reports nothing is counted at its matched length";
    EXPECT_EQ(m_systemManager.getTimingAccum(reporting).totalEntityCount_, 3u);
    EXPECT_EQ(m_systemManager.getTimingAccum(reporting).totalWalkedCount_, 3u)
        << "one report per matched archetype node";
}

// ---- Rounds --------------------------------------------------------------

class StaticRounds : public testing::Test {
  protected:
    SystemId makeDeriving(const char *name) {
        return IRSystem::createSystem<C_PolicyA, Exclude<C_Settled>>(name, [](C_PolicyA &) {});
    }

    void update() {
        m_systemManager.executePipeline(IRTime::Events::UPDATE);
    }
    void render() {
        m_systemManager.executePipeline(IRTime::Events::RENDER);
    }

    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
};

TEST_F(StaticRounds, WithNoDerivingSystemARoundOpensAndClosesAtEachPipelineStart) {
    m_systemManager.registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<C_PolicyA>("Plain", [](C_PolicyA &) {})}
    );
    update();
    EXPECT_EQ(m_systemManager.roundsOpened(), 1u);
    EXPECT_EQ(m_systemManager.roundsCompleted(), 1u);
    update();
    EXPECT_EQ(m_systemManager.roundsCompleted(), 2u);
}

TEST_F(StaticRounds, ARoundCoversEveryDerivingGroupInPipelineOrder) {
    m_systemManager.registerPipeline(IRTime::Events::UPDATE, {makeDeriving("U")});
    m_systemManager.registerPipeline(IRTime::Events::RENDER, {makeDeriving("R")});

    // The round opens at RENDER with its cursor on the UPDATE group, so
    // this RENDER dispatch is not under the cursor and changes nothing.
    render();
    EXPECT_EQ(m_systemManager.roundsOpened(), 1u);
    EXPECT_EQ(m_systemManager.roundsCompleted(), 0u);
    update();
    EXPECT_EQ(m_systemManager.roundsCompleted(), 0u);
    update();
    EXPECT_EQ(m_systemManager.roundsOpened(), 1u) << "a second UPDATE pass opens no new round";
    render();
    EXPECT_EQ(m_systemManager.roundsCompleted(), 1u);
    update();
    EXPECT_EQ(m_systemManager.roundsOpened(), 2u);
}

void expectGroupedRoundWaitsForTheSlowerMember(IRSystem::SystemManager &systems) {
    const SystemId fast = IRSystem::createSystem<C_PolicyA, Exclude<C_Settled>>(
        "Fast",
        [](C_PolicyA &) {}
    );
    const SystemId slow = IRSystem::createSystem<C_PolicyB, Exclude<C_Settled>>(
        "Slow",
        [](C_PolicyB &) {}
    );
    systems.registerPipelineGroups(IRTime::Events::UPDATE, {{fast, slow}});
    systems.validateAllPipelineGroups();
    systems.setSystemCadence(slow, 2);

    systems.executePipeline(IRTime::Events::UPDATE);
    EXPECT_EQ(systems.roundsOpened(), 1u);
    EXPECT_EQ(systems.roundsCompleted(), 0u)
        << "the cadence-2 member has not dispatched under the cursor";
    systems.executePipeline(IRTime::Events::UPDATE);
    EXPECT_EQ(systems.roundsCompleted(), 1u);
}

TEST_F(StaticRounds, GroupedDerivingSystemsCloseTheRoundTogetherInTheSerialFallback) {
    expectGroupedRoundWaitsForTheSlowerMember(m_systemManager);
}

TEST(StaticRoundsWithWorkers, GroupedDerivingSystemsCloseTheRoundTogetherOnAWorkerPool) {
    IREntity::EntityManager entities;
    IRSystem::SystemManager systems;
    IRJob::JobManager jobs(2);
    entities.resizeWorkerStaging(static_cast<std::size_t>(IRJob::workerCount()) + 1u);
    IREntity::createEntity(C_PolicyA{}, C_PolicyB{});
    expectGroupedRoundWaitsForTheSlowerMember(systems);
}

TEST_F(StaticRounds, AMembershipChangeAbandonsTheOpenRound) {
    m_systemManager.registerPipeline(IRTime::Events::UPDATE, {makeDeriving("U")});
    m_systemManager.registerPipeline(IRTime::Events::RENDER, {makeDeriving("R")});
    update();
    ASSERT_EQ(m_systemManager.roundsOpened(), 1u);

    IRSystem::appendToPipeline(IRTime::Events::RENDER, makeDeriving("Late"));
    render();
    EXPECT_EQ(m_systemManager.roundsCompleted(), 0u) << "the abandoned round never completes";
    EXPECT_EQ(m_systemManager.roundsOpened(), 2u) << "the next pipeline start opens a new one";
    update();
    render();
    EXPECT_EQ(m_systemManager.roundsCompleted(), 2u)
        << "the next complete round includes the new system";
}

TEST_F(StaticRounds, RoundCountersDoNotWrapAtThirtyTwoBits) {
    m_systemManager.registerPipeline(IRTime::Events::UPDATE, {makeDeriving("U")});
    const std::uint64_t start = (std::uint64_t{1} << 32) - 2;
    m_systemManager.seedRoundCountersForTest(start);
    for (int i = 0; i < 5; ++i) {
        update();
    }
    EXPECT_EQ(m_systemManager.roundsOpened(), start + 5);
    EXPECT_EQ(m_systemManager.roundsCompleted(), start + 5);
    EXPECT_GT(m_systemManager.roundsCompleted(), std::uint64_t{1} << 32);
}

} // namespace

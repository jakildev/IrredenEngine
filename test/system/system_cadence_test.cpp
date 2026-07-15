#include <gtest/gtest.h>

#include <irreden/ir_constants.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/entity/entity_manager.hpp>

#include <cstdint>
#include <stdexcept>
#include <vector>

// #2404 — per-system update cadence. Exercises the SystemManager cadence
// gate end-to-end through IRSystem::createSystem + registerPipeline(Groups),
// using the g_jobManager == nullptr serial fallback (no worker pool in a
// unit test) for deterministic dispatch. The five acceptance criteria from
// the issue map onto the tests below; execution counting rides beginTick
// (fires once per due execution, even with zero matched entities), and
// per-entity iteration counting rides the per-entity tick body.

namespace {

struct C_CadA {
    int n_ = 0;
};
struct C_CadB {
    int m_ = 0;
};

class SystemCadenceTest : public testing::Test {
  protected:
    SystemCadenceTest()
        : m_entity_manager{}
        , m_system_manager{} {}

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

// Acceptance 1: a cadence-N system fires on exactly 1-in-N phase ticks.
TEST_F(SystemCadenceTest, RunsOneInNTicks) {
    int exec = 0;
    auto sys = IRSystem::createSystem<C_CadA>(
        "Cad3",
        [](C_CadA &) {},
        [&exec]() { ++exec; },
        nullptr,
        {},
        nullptr,
        IRSystem::Concurrency::SERIAL,
        IRSystem::kDefaultGrainSize,
        /*cadence=*/3,
        /*offset=*/0
    );
    m_system_manager.registerPipeline(IRTime::UPDATE, {sys});

    for (int i = 0; i < 30; ++i) {
        m_system_manager.executePipeline(IRTime::UPDATE);
    }
    // Fires on ticks 3, 6, ..., 30 = 10 executions = floor(30/3).
    EXPECT_EQ(exec, 10);
    EXPECT_EQ(m_system_manager.getSystemCadence(sys), 3u);
}

// Acceptance 4: the default (unset) cadence runs every tick, unchanged.
TEST_F(SystemCadenceTest, DefaultCadenceRunsEveryTick) {
    int exec = 0;
    auto sys = IRSystem::createSystem<C_CadA>("Every", [](C_CadA &) {}, [&exec]() { ++exec; });
    m_system_manager.registerPipeline(IRTime::UPDATE, {sys});

    for (int i = 0; i < 12; ++i) {
        m_system_manager.executePipeline(IRTime::UPDATE);
    }
    EXPECT_EQ(exec, 12);
    EXPECT_EQ(m_system_manager.getSystemCadence(sys), 1u);
    // A cadence-1 system covers exactly one phase tick per execution.
    EXPECT_EQ(m_system_manager.getAccumulatedTicks(sys), 1u);
}

// Acceptance 3: off-cadence ticks incur no per-entity iteration — the
// visit count scales with 1/N, not with the tick count.
TEST_F(SystemCadenceTest, OffCadenceTicksSkipIteration) {
    int exec = 0;
    int visits = 0;
    auto sys = IRSystem::createSystem<C_CadA>(
        "Skip",
        [&visits](C_CadA &) { ++visits; },
        [&exec]() { ++exec; },
        nullptr,
        {},
        nullptr,
        IRSystem::Concurrency::SERIAL,
        IRSystem::kDefaultGrainSize,
        /*cadence=*/4,
        /*offset=*/0
    );
    m_system_manager.registerPipeline(IRTime::UPDATE, {sys});
    IREntity::createEntity(C_CadA{});
    IREntity::createEntity(C_CadA{});

    for (int i = 0; i < 20; ++i) {
        m_system_manager.executePipeline(IRTime::UPDATE);
    }
    EXPECT_EQ(exec, 5);       // fires on 4, 8, 12, 16, 20
    EXPECT_EQ(visits, 5 * 2); // two entities iterated only on due ticks
}

// Acceptance 2: getAccumulatedTicks reports the phase-tick gap each
// execution covers, re-phasing correctly across a mid-run cadence change,
// and the per-execution values sum to the total elapsed phase ticks.
TEST_F(SystemCadenceTest, AccumulatedTicksRephaseOnCadenceChange) {
    int exec = 0;
    int prevExec = 0;
    std::vector<std::uint64_t> accs;
    auto sys = IRSystem::createSystem<C_CadA>(
        "Acc",
        [](C_CadA &) {},
        [&exec]() { ++exec; },
        nullptr,
        {},
        nullptr,
        IRSystem::Concurrency::SERIAL,
        IRSystem::kDefaultGrainSize,
        /*cadence=*/3,
        /*offset=*/0
    );
    m_system_manager.registerPipeline(IRTime::UPDATE, {sys});

    auto tickAndRecord = [&]() {
        m_system_manager.executePipeline(IRTime::UPDATE);
        if (exec != prevExec) {
            accs.push_back(m_system_manager.getAccumulatedTicks(sys));
            prevExec = exec;
        }
    };

    for (int i = 0; i < 9; ++i) { // fires at 3, 6, 9 — each covering 3 ticks
        tickAndRecord();
    }
    m_system_manager.setSystemCadence(sys, 5); // re-phase from last run (tick 9)
    for (int i = 0; i < 10; ++i) {             // ticks 10..19 — fires at 14, 19 — 5 ticks each
        tickAndRecord();
    }

    ASSERT_EQ(accs.size(), 5u);
    EXPECT_EQ(accs[0], 3u);
    EXPECT_EQ(accs[1], 3u);
    EXPECT_EQ(accs[2], 3u);
    EXPECT_EQ(accs[3], 5u);
    EXPECT_EQ(accs[4], 5u);

    std::uint64_t sum = 0;
    for (std::uint64_t a : accs) {
        sum += a;
    }
    EXPECT_EQ(sum, 19u); // last fire landed on phase tick 19
}

// Acceptance 5: in a mixed-cadence multi-system group, only the due
// members dispatch on a given tick (serial fallback, no worker pool).
TEST_F(SystemCadenceTest, MultiSystemGroupFiltersDueMembers) {
    int execA = 0;
    int execB = 0;
    auto a = IRSystem::createSystem<C_CadA>(
        "GroupA",
        [](C_CadA &) {},
        [&execA]() { ++execA; },
        nullptr,
        {},
        nullptr,
        IRSystem::Concurrency::SERIAL,
        IRSystem::kDefaultGrainSize,
        /*cadence=*/2,
        /*offset=*/0
    );
    auto b = IRSystem::createSystem<C_CadB>(
        "GroupB",
        [](C_CadB &) {},
        [&execB]() { ++execB; },
        nullptr,
        {},
        nullptr,
        IRSystem::Concurrency::SERIAL,
        IRSystem::kDefaultGrainSize,
        /*cadence=*/3,
        /*offset=*/0
    );
    m_system_manager.registerPipelineGroups(IRTime::UPDATE, {{a, b}});

    for (int i = 0; i < 6; ++i) {
        m_system_manager.executePipeline(IRTime::UPDATE);
    }
    EXPECT_EQ(execA, 3); // 2, 4, 6
    EXPECT_EQ(execB, 2); // 3, 6
}

// Amendment 1: an initial-phase offset staggers sibling systems registered
// together so they fire on distinct ticks instead of spiking together.
TEST_F(SystemCadenceTest, OffsetStaggersSiblings) {
    int exec0 = 0;
    int exec1 = 0;
    auto s0 = IRSystem::createSystem<C_CadA>(
        "Off0",
        [](C_CadA &) {},
        [&exec0]() { ++exec0; },
        nullptr,
        {},
        nullptr,
        IRSystem::Concurrency::SERIAL,
        IRSystem::kDefaultGrainSize,
        /*cadence=*/2,
        /*offset=*/0
    );
    auto s1 = IRSystem::createSystem<C_CadB>(
        "Off1",
        [](C_CadB &) {},
        [&exec1]() { ++exec1; },
        nullptr,
        {},
        nullptr,
        IRSystem::Concurrency::SERIAL,
        IRSystem::kDefaultGrainSize,
        /*cadence=*/2,
        /*offset=*/1
    );
    m_system_manager.registerPipeline(IRTime::UPDATE, {s0, s1});

    std::vector<int> fires0;
    std::vector<int> fires1;
    for (int t = 1; t <= 6; ++t) {
        const int p0 = exec0;
        const int p1 = exec1;
        m_system_manager.executePipeline(IRTime::UPDATE);
        if (exec0 != p0) {
            fires0.push_back(t);
        }
        if (exec1 != p1) {
            fires1.push_back(t);
        }
        // Staggered: the two never fire on the same tick.
        EXPECT_FALSE(exec0 != p0 && exec1 != p1);
    }
    EXPECT_EQ(fires0, (std::vector<int>{2, 4, 6}));
    EXPECT_EQ(fires1, (std::vector<int>{3, 5}));
    EXPECT_EQ(m_system_manager.getSystemCadenceOffset(s1), 1u);
}

// Runtime setters normalize their inputs; cadenceFromRate maps a target
// sub-rate onto the integer divisor primitive.
TEST_F(SystemCadenceTest, RuntimeSettersNormalizeAndCadenceFromRate) {
    auto sys = IRSystem::createSystem<C_CadA>("Setters", [](C_CadA &) {});
    EXPECT_EQ(m_system_manager.getSystemCadence(sys), 1u);
    EXPECT_EQ(m_system_manager.getSystemCadenceOffset(sys), 0u);

    m_system_manager.setSystemCadence(sys, 0); // 0 normalizes to 1
    EXPECT_EQ(m_system_manager.getSystemCadence(sys), 1u);

    m_system_manager.setSystemCadence(sys, 5);
    EXPECT_EQ(m_system_manager.getSystemCadence(sys), 5u);

    m_system_manager.setSystemCadenceOffset(sys, 7); // clamps into [0, 5) → 2
    EXPECT_EQ(m_system_manager.getSystemCadenceOffset(sys), 2u);

    EXPECT_EQ(IRSystem::cadenceFromRate(0.0), 1u); // non-positive → every tick
    EXPECT_EQ(
        IRSystem::cadenceFromRate(static_cast<double>(IRConstants::kFPS)),
        1u
    ); // full rate → 1
    EXPECT_EQ(
        IRSystem::cadenceFromRate(static_cast<double>(IRConstants::kFPS) / 2.0),
        2u
    ); // half rate → 2
}

// A runtime drop to cadence 1 while `lastRunTick` is still ahead of `now`
// (from a nonzero offset seed a prior, larger cadence hadn't yet caught up
// to) must keep waiting for `now` to catch up, not fire immediately — firing
// early would compute `now - lastRunTick` as an underflowed uint64_t. See
// #2425.
TEST_F(SystemCadenceTest, CadenceDropToOneDoesNotUnderflow) {
    int exec = 0;
    auto sys = IRSystem::createSystem<C_CadA>(
        "DropToOne",
        [](C_CadA &) {},
        [&exec]() { ++exec; },
        nullptr,
        {},
        nullptr,
        IRSystem::Concurrency::SERIAL,
        IRSystem::kDefaultGrainSize,
        /*cadence=*/4,
        /*offset=*/3
    );
    m_system_manager.registerPipeline(IRTime::UPDATE, {sys}); // stamp: lastRun = 0 + 3 = 3

    m_system_manager.executePipeline(IRTime::UPDATE); // now=1: off-cadence, no fire.
    EXPECT_EQ(exec, 0);

    m_system_manager.setSystemCadence(sys, 1); // cadence=1, offset clamps to 0; lastRun stays 3.

    // now=2, now=3: still off-cadence — due at now >= lastRun(3) + cadence(1) = 4.
    m_system_manager.executePipeline(IRTime::UPDATE);
    m_system_manager.executePipeline(IRTime::UPDATE);
    EXPECT_EQ(exec, 0);

    m_system_manager.executePipeline(IRTime::UPDATE); // now=4: due.
    EXPECT_EQ(exec, 1);
    EXPECT_EQ(m_system_manager.getAccumulatedTicks(sys), 1u); // not ~1.8e19 (UINT64_MAX underflow).
}

// setSystemCadenceOffset must re-phase against the system's own pipeline
// event clock, not another event's counter — events advance at different
// rates (RENDER is uncapped, UPDATE is fixed-step), so seeding from a
// foreign clock can put `lastRunTick` far ahead of this system's `now` and
// silently stall it. See #2425.
TEST_F(SystemCadenceTest, OffsetRephaseUsesOwnEventClock) {
    int exec = 0;
    auto sys = IRSystem::createSystem<C_CadA>(
        "OwnClock",
        [](C_CadA &) {},
        [&exec]() { ++exec; },
        nullptr,
        {},
        nullptr,
        IRSystem::Concurrency::SERIAL,
        IRSystem::kDefaultGrainSize,
        /*cadence=*/4,
        /*offset=*/0
    );
    m_system_manager.registerPipeline(IRTime::UPDATE, {sys});
    m_system_manager.registerPipelineGroups(IRTime::RENDER, {}); // establish RENDER's counter.

    // Advance RENDER far past UPDATE — representative of a real World,
    // where RENDER is uncapped and UPDATE is pinned to the fixed step.
    for (int i = 0; i < 50; ++i) {
        m_system_manager.executePipeline(IRTime::RENDER);
    }

    m_system_manager.setSystemCadenceOffset(sys, 1); // must re-phase against UPDATE's own clock.

    // Fires within offset + cadence UPDATE ticks (5) — not stalled for
    // ~50 ticks waiting for UPDATE to catch up to RENDER's counter.
    for (int i = 0; i < 5; ++i) {
        m_system_manager.executePipeline(IRTime::UPDATE);
    }
    EXPECT_EQ(exec, 1);
}

// A system's cadence clock is bound to one event (stampCadenceJoin). Joining
// it to a second event while it is STILL listed in the first one would leave
// it live in two pipelines, thrashing m_lastRunTick between two counters that
// advance at different rates. stampCadenceJoin IR_ASSERTs on that; the assert
// throws std::runtime_error via engAssert, as in pipeline_groups_test.cpp.
TEST_F(SystemCadenceTest, JoinToSecondEventWhileLiveInFirstAsserts) {
    auto sys = IRSystem::createSystem<C_CadA>(
        "TwoPipelinesLive",
        [](C_CadA &) {},
        nullptr,
        nullptr,
        {},
        nullptr,
        IRSystem::Concurrency::SERIAL,
        IRSystem::kDefaultGrainSize,
        /*cadence=*/2,
        /*offset=*/0
    );
    m_system_manager.registerPipeline(IRTime::UPDATE, {sys});

    // UPDATE's pipeline still lists sys — joining RENDER must be rejected.
    EXPECT_THROW(m_system_manager.appendToPipeline(IRTime::RENDER, sys), std::runtime_error);
}

// The counterpart success path: clearing the old event's pipeline first makes
// the re-join legitimate (a scene transition re-purposing the SystemId), so
// the guard must stay silent rather than false-assert on any re-join.
TEST_F(SystemCadenceTest, RejoinAfterClearingPriorPipelineIsSilent) {
    auto sys = IRSystem::createSystem<C_CadA>(
        "RejoinAfterClear",
        [](C_CadA &) {},
        nullptr,
        nullptr,
        {},
        nullptr,
        IRSystem::Concurrency::SERIAL,
        IRSystem::kDefaultGrainSize,
        /*cadence=*/2,
        /*offset=*/0
    );
    m_system_manager.registerPipeline(IRTime::UPDATE, {sys});
    m_system_manager.clearPipeline(IRTime::UPDATE); // no longer live in UPDATE.

    EXPECT_NO_THROW(m_system_manager.appendToPipeline(IRTime::RENDER, sys));
}

} // namespace

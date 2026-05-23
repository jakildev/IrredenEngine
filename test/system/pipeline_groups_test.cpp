#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/system/system_access.hpp>

#include <stdexcept>

// T-224 Phase 3 — multithreading epic (#226). Two surfaces under test:
//
//   1. The pure cross-system conflict-detection function
//      `findPipelineGroupConflict` over hand-built `SystemAccess`
//      values — no fixture needed.
//   2. The end-to-end `SystemManager::validateAllPipelineGroups`
//      driven through `IRSystem::createSystem` + `registerPipelineGroups`.
//      The validator IR_ASSERTs on conflicts (throws std::runtime_error
//      via engAssert), so each rejection case asserts EXPECT_THROW.

namespace {

struct C_VelA {
    int n_ = 0;
};
struct C_VelB {
    int m_ = 0;
};

using IRSystem::findPipelineGroupConflict;
using IRSystem::GroupConflictKind;
using IRSystem::SystemAccess;
using IRSystem::typeKey;

// ----------------------------------------------------------------------
// Pure validator — predicate-level coverage
// ----------------------------------------------------------------------

TEST(PipelineGroupsValidator, EmptyAndSingletonGroupsAreClean) {
    EXPECT_EQ(findPipelineGroupConflict(nullptr, 0).kind_, GroupConflictKind::NONE);
    SystemAccess one{};
    EXPECT_EQ(findPipelineGroupConflict(&one, 1).kind_, GroupConflictKind::NONE);
}

TEST(PipelineGroupsValidator, NonConflictingPairIsClean) {
    SystemAccess accesses[2]{};
    // A writes C_VelA, reads nothing.
    accesses[0].writes_[0] = typeKey<C_VelA>;
    accesses[0].writeCount_ = 1;
    // B writes C_VelB, reads nothing — disjoint columns.
    accesses[1].writes_[0] = typeKey<C_VelB>;
    accesses[1].writeCount_ = 1;
    EXPECT_EQ(findPipelineGroupConflict(accesses, 2).kind_, GroupConflictKind::NONE);
}

TEST(PipelineGroupsValidator, WriteWriteOnSameKeyConflicts) {
    SystemAccess accesses[2]{};
    accesses[0].writes_[0] = typeKey<C_VelA>;
    accesses[0].writeCount_ = 1;
    accesses[1].writes_[0] = typeKey<C_VelA>;
    accesses[1].writeCount_ = 1;
    auto c = findPipelineGroupConflict(accesses, 2);
    EXPECT_EQ(c.kind_, GroupConflictKind::WRITE_WRITE);
    EXPECT_EQ(c.indexA_, 0u);
    EXPECT_EQ(c.indexB_, 1u);
    EXPECT_EQ(c.componentKey_, typeKey<C_VelA>);
}

TEST(PipelineGroupsValidator, WriteReadConflictsInBothDirections) {
    SystemAccess accesses[2]{};
    accesses[0].writes_[0] = typeKey<C_VelA>;
    accesses[0].writeCount_ = 1;
    accesses[1].reads_[0] = typeKey<C_VelA>;
    accesses[1].readCount_ = 1;
    auto c = findPipelineGroupConflict(accesses, 2);
    EXPECT_EQ(c.kind_, GroupConflictKind::WRITE_READ);
    EXPECT_EQ(c.componentKey_, typeKey<C_VelA>);

    // Flip: A reads, B writes — still a conflict, opposite kind.
    SystemAccess flipped[2]{};
    flipped[0].reads_[0] = typeKey<C_VelA>;
    flipped[0].readCount_ = 1;
    flipped[1].writes_[0] = typeKey<C_VelA>;
    flipped[1].writeCount_ = 1;
    auto c2 = findPipelineGroupConflict(flipped, 2);
    EXPECT_EQ(c2.kind_, GroupConflictKind::READ_WRITE);
    EXPECT_EQ(c2.componentKey_, typeKey<C_VelA>);
}

TEST(PipelineGroupsValidator, MainThreadInGroupRejected) {
    SystemAccess accesses[2]{};
    accesses[0].mainThreadOnly_ = true;
    accesses[1].writes_[0] = typeKey<C_VelB>;
    accesses[1].writeCount_ = 1;
    auto c = findPipelineGroupConflict(accesses, 2);
    EXPECT_EQ(c.kind_, GroupConflictKind::MAIN_THREAD_IN_GROUP);
    EXPECT_EQ(c.indexA_, 0u);
}

TEST(PipelineGroupsValidator, TwoSpawnersInGroupAcceptedAfterT225) {
    // T-225 lifted the two-`mutatesArchetypeGraph_`-in-a-group rule.
    // Per-worker deferred-mutation buffers route every structural
    // change into a worker-private slot, and the main thread drains
    // them serially at flush, so two spawners can coexist in the
    // same parallel group as long as no other write/read conflict
    // applies. Validator returns NONE in this case.
    SystemAccess accesses[2]{};
    accesses[0].writes_[0] = typeKey<C_VelA>;
    accesses[0].writeCount_ = 1;
    accesses[0].mutatesArchetypeGraph_ = true;
    accesses[1].writes_[0] = typeKey<C_VelB>;
    accesses[1].writeCount_ = 1;
    accesses[1].mutatesArchetypeGraph_ = true;
    auto c = findPipelineGroupConflict(accesses, 2);
    EXPECT_EQ(c.kind_, GroupConflictKind::NONE);
}

TEST(PipelineGroupsValidator, SingleMutatorWithSiblingAcceptedAfterT225) {
    // T-225: a single mutator + non-mutator sibling is also accepted now.
    // MUTATOR_IN_PARALLEL_GROUP is lifted alongside TWO_SPAWNERS.
    SystemAccess accesses[2]{};
    accesses[0].writes_[0] = typeKey<C_VelA>;
    accesses[0].writeCount_ = 1;
    accesses[0].mutatesArchetypeGraph_ = true;
    accesses[1].writes_[0] = typeKey<C_VelB>;
    accesses[1].writeCount_ = 1;
    auto c = findPipelineGroupConflict(accesses, 2);
    EXPECT_EQ(c.kind_, GroupConflictKind::NONE);
}

TEST(PipelineGroupsValidator, MutatorAloneInSingletonGroupAccepted) {
    // A mutator in a singleton group is fine (trivially no conflicts).
    SystemAccess one{};
    one.writes_[0] = typeKey<C_VelA>;
    one.writeCount_ = 1;
    one.mutatesArchetypeGraph_ = true;
    EXPECT_EQ(findPipelineGroupConflict(&one, 1).kind_, GroupConflictKind::NONE);
}

TEST(PipelineGroupsValidator, MainThreadPrioritizedOverOtherConflicts) {
    // When multiple conflict kinds are present, the MAIN_THREAD check
    // fires first so the diagnostic surfaces the strongest claim
    // ("MAIN_THREAD never co-executes" > "writes overlap").
    SystemAccess accesses[2]{};
    accesses[0].mainThreadOnly_ = true;
    accesses[0].writes_[0] = typeKey<C_VelA>;
    accesses[0].writeCount_ = 1;
    accesses[1].writes_[0] = typeKey<C_VelA>;
    accesses[1].writeCount_ = 1;
    auto c = findPipelineGroupConflict(accesses, 2);
    EXPECT_EQ(c.kind_, GroupConflictKind::MAIN_THREAD_IN_GROUP);
}

// ----------------------------------------------------------------------
// End-to-end — SystemManager::validateAllPipelineGroups
// ----------------------------------------------------------------------

class PipelineGroupsValidatorTest : public testing::Test {
  protected:
    PipelineGroupsValidatorTest()
        : m_entity_manager{}
        , m_system_manager{} {}

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(PipelineGroupsValidatorTest, SingletonGroupsAlwaysPass) {
    auto sysA = IRSystem::createSystem<C_VelA>("A", [](C_VelA &) {});
    auto sysB = IRSystem::createSystem<C_VelB>("B", [](C_VelB &) {});
    m_system_manager.registerPipelineGroups(IRTime::Events::UPDATE, {{sysA}, {sysB}});
    EXPECT_NO_THROW(m_system_manager.validateAllPipelineGroups());
}

TEST_F(PipelineGroupsValidatorTest, ConflictingWriteGroupRejected) {
    // Two systems both writing C_VelA — write/write conflict on the
    // same component column. validateAllPipelineGroups must throw
    // with a diagnostic naming both systems.
    auto sysA = IRSystem::createSystem<C_VelA>("WriteA1", [](C_VelA &) {});
    auto sysB = IRSystem::createSystem<C_VelA>("WriteA2", [](C_VelA &) {});
    m_system_manager.registerPipelineGroups(IRTime::Events::UPDATE, {{sysA, sysB}});
    EXPECT_THROW(m_system_manager.validateAllPipelineGroups(), std::runtime_error);
}

// `MainThread` / `Spawns` rejections live in the pure-function tests
// above. They can't ride through `IRSystem::createSystem`'s Components
// pack today — `InvocableWithComponents` probes the tick lambda with
// `(MainThread&)` etc. and falls into the static_assert branch. The
// SystemManager path validates the same conflict rules via
// `findPipelineGroupConflict`, so the rule is covered; expressing it
// end-to-end here would need a `setSystemAccess` test setter that the
// production API otherwise has no use for.

TEST_F(PipelineGroupsValidatorTest, DisjointWritesInGroupAccepted) {
    // Two systems writing distinct component columns can share a
    // group — the validator clears it as the canonical "this group
    // is safe to parallelize" case.
    auto sysA = IRSystem::createSystem<C_VelA>("WriteAOnly", [](C_VelA &) {});
    auto sysB = IRSystem::createSystem<C_VelB>("WriteBOnly", [](C_VelB &) {});
    m_system_manager.registerPipelineGroups(IRTime::Events::UPDATE, {{sysA, sysB}});
    EXPECT_NO_THROW(m_system_manager.validateAllPipelineGroups());
}

TEST_F(PipelineGroupsValidatorTest, ParallelForInMultiGroupRejected) {
    // A PARALLEL_FOR system cannot share a parallel group with any sibling:
    // its inner IRJobs::parallelFor would be reached from a worker thread,
    // violating the main-thread assert. It must run in its own singleton group.
    auto sysA = IRSystem::createSystem<C_VelA>(
        "ParForA",
        [](C_VelA &) {},
        nullptr,
        nullptr,
        {},
        nullptr,
        IRSystem::Concurrency::PARALLEL_FOR
    );
    auto sysB = IRSystem::createSystem<C_VelB>("SerialB", [](C_VelB &) {});
    m_system_manager.registerPipelineGroups(IRTime::Events::UPDATE, {{sysA, sysB}});
    EXPECT_THROW(m_system_manager.validateAllPipelineGroups(), std::runtime_error);
}

TEST_F(PipelineGroupsValidatorTest, ParallelForAloneInSingletonGroupAccepted) {
    auto sysA = IRSystem::createSystem<C_VelA>(
        "ParForAlone",
        [](C_VelA &) {},
        nullptr,
        nullptr,
        {},
        nullptr,
        IRSystem::Concurrency::PARALLEL_FOR
    );
    m_system_manager.registerPipelineGroups(IRTime::Events::UPDATE, {{sysA}});
    EXPECT_NO_THROW(m_system_manager.validateAllPipelineGroups());
}

TEST_F(PipelineGroupsValidatorTest, RegisterPipelineProducesSingletonGroups) {
    // The legacy `registerPipeline(list<SystemId>)` API must translate
    // to single-system groups so existing call sites get the
    // bit-for-bit-equivalent dispatch behavior.
    auto sysA = IRSystem::createSystem<C_VelA>("A", [](C_VelA &) {});
    auto sysB = IRSystem::createSystem<C_VelA>("B", [](C_VelA &) {});
    // Two writers to C_VelA in a flat list would be a conflict if
    // they landed in the same group; the legacy API must spread them
    // across singleton groups instead. validateAllPipelineGroups
    // must NOT throw.
    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysA, sysB});
    EXPECT_NO_THROW(m_system_manager.validateAllPipelineGroups());

    const auto &groups = m_system_manager.getPipelineGroups(IRTime::Events::UPDATE);
    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(groups[0].size(), 1u);
    EXPECT_EQ(groups[1].size(), 1u);
    EXPECT_EQ(groups[0][0], sysA);
    EXPECT_EQ(groups[1][0], sysB);
}

} // namespace

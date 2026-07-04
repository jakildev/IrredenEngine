#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <algorithm>
#include <vector>

// Covers the one-shot query primitive `IRSystem::executeQuery<Cs...>` /
// `executeQueryDynamic` (#17) — the run-now counterpart to
// `createSystem` / `createSystemDynamic`. The contract: same archetype-node
// traversal as a system tick (include + Exclude<> filtering), but nothing is
// registered — no SystemId, no pipeline slot, no persistent state. These tests
// assert both the dispatch shapes (per-component, per-entity-id, per-node) and
// the "leaves no trace on the SystemManager" invariant that separates this from
// `createSystem`.

namespace {

// Plain data + tag types — TU-local so they don't pollute the engine's
// component registry across test files.
struct Counter {
    int n_ = 0;
};
struct ExcludeTag {};

class ExecuteQueryTest : public testing::Test {
  protected:
    ExecuteQueryTest()
        : m_entityManager{}
        , m_systemManager{} {}

    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
};

// (a) executeQuery<Counter> visits every entity carrying Counter, across the
// two distinct archetypes (Counter-only vs Counter+ExcludeTag).
TEST_F(ExecuteQueryTest, PerComponentVisitsAcrossArchetypes) {
    auto idA = IREntity::createEntity(Counter{0});               // archetype 1
    auto idB = IREntity::createEntity(Counter{0}, ExcludeTag{}); // archetype 2

    IRSystem::executeQuery<Counter>([](Counter &c) { c.n_++; });

    EXPECT_EQ(IREntity::getComponent<Counter>(idA).n_, 1);
    EXPECT_EQ(IREntity::getComponent<Counter>(idB).n_, 1);
}

// (b) Exclude<> in the query params skips tagged entities via archetype
// rejection — no per-entity branching in the body.
TEST_F(ExecuteQueryTest, ExcludeSkipsTaggedEntities) {
    auto idA = IREntity::createEntity(Counter{0});
    auto idB = IREntity::createEntity(Counter{0}, ExcludeTag{});

    IRSystem::executeQuery<Counter, IRSystem::Exclude<ExcludeTag>>([](Counter &c) { c.n_++; });

    EXPECT_EQ(IREntity::getComponent<Counter>(idA).n_, 1) << "untagged runs";
    EXPECT_EQ(IREntity::getComponent<Counter>(idB).n_, 0) << "tagged excluded";
}

// (c) The EntityId tick form receives the correct ids.
TEST_F(ExecuteQueryTest, EntityIdFormReceivesCorrectIds) {
    auto idA = IREntity::createEntity(Counter{0});
    auto idB = IREntity::createEntity(Counter{0}, ExcludeTag{});

    std::vector<IREntity::EntityId> visited;
    IRSystem::executeQuery<Counter>([&visited](IREntity::EntityId id, Counter &c) {
        visited.push_back(id);
        c.n_++;
    });

    EXPECT_EQ(visited.size(), 2u);
    EXPECT_NE(std::find(visited.begin(), visited.end(), idA), visited.end());
    EXPECT_NE(std::find(visited.begin(), visited.end(), idB), visited.end());
    EXPECT_EQ(IREntity::getComponent<Counter>(idA).n_, 1);
    EXPECT_EQ(IREntity::getComponent<Counter>(idB).n_, 1);
}

// (d) executeQueryDynamic fires exactly once per matched archetype node.
TEST_F(ExecuteQueryTest, DynamicFiresOncePerMatchedNode) {
    IREntity::createEntity(Counter{0});               // node 1
    IREntity::createEntity(Counter{0});               // same node 1
    IREntity::createEntity(Counter{0}, ExcludeTag{}); // node 2

    int nodeVisits = 0;
    IRSystem::executeQueryDynamic(
        IREntity::getArchetype<Counter>(),
        IREntity::Archetype{},
        [&nodeVisits](IREntity::ArchetypeNode *) { ++nodeVisits; }
    );

    EXPECT_EQ(nodeVisits, 2) << "one fire per archetype containing Counter";
}

// Zero matches is a silent no-op (correct for a command with nothing to act on).
TEST_F(ExecuteQueryTest, ZeroMatchesIsNoOp) {
    int bodyRuns = 0;
    IRSystem::executeQuery<Counter>([&bodyRuns](Counter &) { ++bodyRuns; });
    EXPECT_EQ(bodyRuns, 0);
}

// (e) No registration: the query leaves the SystemManager's system count
// unchanged, and a subsequent executePipeline does NOT re-run the body (proof
// it didn't secretly register a persistent system).
TEST_F(ExecuteQueryTest, LeavesNoRegisteredSystem) {
    auto id = IREntity::createEntity(Counter{0});

    const IRSystem::SystemId countBefore = m_systemManager.getSystemCount();
    IRSystem::executeQuery<Counter>([](Counter &c) { c.n_++; });
    const IRSystem::SystemId countAfter = m_systemManager.getSystemCount();

    EXPECT_EQ(countBefore, countAfter) << "executeQuery registers no system";
    EXPECT_EQ(IREntity::getComponent<Counter>(id).n_, 1);

    // An empty pipeline tick must not re-run the query body.
    m_systemManager.executePipeline(IRTime::Events::UPDATE);
    EXPECT_EQ(IREntity::getComponent<Counter>(id).n_, 1)
        << "the one-shot query does not persist into the pipeline";
}

} // namespace

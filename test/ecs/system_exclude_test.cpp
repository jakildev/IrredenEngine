#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/entity/archetype_graph.hpp>
#include <irreden/entity/entity_manager.hpp>

namespace {

// Plain data + tag types — kept local to this TU so they don't pollute
// the engine's component registry across test files.
struct Counter {
    int n_ = 0;
};
struct ExcludeTag {};

// ---- Low-level: ArchetypeGraph honors excludeComponents ------------------

class ArchetypeGraphExcludeTest : public testing::Test {
  protected:
    IREntity::EntityManager m_entity_manager;
};

TEST_F(ArchetypeGraphExcludeTest, EmptyExcludeMatchesAllIncluded) {
    IREntity::createEntity(Counter{1});
    IREntity::createEntity(Counter{2}, ExcludeTag{});

    auto include = IREntity::getArchetype<Counter>();
    auto nodes = IREntity::queryArchetypeNodesSimple(include);

    int total = 0;
    for (auto *node : nodes) total += node->length_;
    EXPECT_EQ(total, 2);
}

TEST_F(ArchetypeGraphExcludeTest, NonEmptyExcludeRejectsTaggedNodes) {
    IREntity::createEntity(Counter{1});
    IREntity::createEntity(Counter{2}, ExcludeTag{});

    auto include = IREntity::getArchetype<Counter>();
    auto exclude = IREntity::getArchetype<ExcludeTag>();
    auto nodes = IREntity::queryArchetypeNodesSimple(include, exclude);

    int total = 0;
    for (auto *node : nodes) total += node->length_;
    EXPECT_EQ(total, 1);
}

TEST_F(ArchetypeGraphExcludeTest, ExcludeOnNonTaggedNodesIsNoOp) {
    IREntity::createEntity(Counter{1});
    IREntity::createEntity(Counter{2});

    auto include = IREntity::getArchetype<Counter>();
    auto exclude = IREntity::getArchetype<ExcludeTag>();
    auto nodes = IREntity::queryArchetypeNodesSimple(include, exclude);

    int total = 0;
    for (auto *node : nodes) total += node->length_;
    EXPECT_EQ(total, 2);
}

// ---- High-level: Exclude<> in createSystem<> dispatches via archetype ----

class SystemExcludeTest : public testing::Test {
  protected:
    SystemExcludeTest()
        : m_entity_manager{}
        , m_system_manager{} {}

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(SystemExcludeTest, ExcludeTemplateParamSkipsTaggedEntities) {
    auto idA = IREntity::createEntity(Counter{0});             // not tagged
    auto idB = IREntity::createEntity(Counter{0}, ExcludeTag{}); // tagged

    auto sysId = IRSystem::createSystem<Counter, IRSystem::Exclude<ExcludeTag>>(
        "IncrementUntagged",
        [](Counter &c) { c.n_++; }
    );
    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysId});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    EXPECT_EQ(IREntity::getComponent<Counter>(idA).n_, 1) << "untagged entity should run";
    EXPECT_EQ(IREntity::getComponent<Counter>(idB).n_, 0) << "tagged entity should be excluded";
}

TEST_F(SystemExcludeTest, NoExcludeTemplateParamMatchesAll) {
    auto idA = IREntity::createEntity(Counter{0});
    auto idB = IREntity::createEntity(Counter{0}, ExcludeTag{});

    auto sysId = IRSystem::createSystem<Counter>(
        "IncrementAll",
        [](Counter &c) { c.n_++; }
    );
    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysId});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    EXPECT_EQ(IREntity::getComponent<Counter>(idA).n_, 1);
    EXPECT_EQ(IREntity::getComponent<Counter>(idB).n_, 1);
}

TEST_F(SystemExcludeTest, ExcludeAndIncludePartitionTaggedPopulation) {
    auto idA = IREntity::createEntity(Counter{0});
    auto idB = IREntity::createEntity(Counter{0}, ExcludeTag{});

    auto sysExempt = IRSystem::createSystem<Counter, IRSystem::Exclude<ExcludeTag>>(
        "IncrementUntagged",
        [](Counter &c) { c.n_ += 1; }
    );
    auto sysTagged = IRSystem::createSystem<Counter, ExcludeTag>(
        "IncrementTaggedTenfold",
        [](Counter &c, ExcludeTag &) { c.n_ += 10; }
    );
    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysExempt, sysTagged});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    EXPECT_EQ(IREntity::getComponent<Counter>(idA).n_, 1)  << "untagged path only";
    EXPECT_EQ(IREntity::getComponent<Counter>(idB).n_, 10) << "tagged path only";
}

TEST_F(SystemExcludeTest, AddSystemExcludeTagMatchesExcludeTemplateParam) {
    auto idA = IREntity::createEntity(Counter{0});
    auto idB = IREntity::createEntity(Counter{0}, ExcludeTag{});

    auto sysId = IRSystem::createSystem<Counter>(
        "IncrementUntaggedRuntime",
        [](Counter &c) { c.n_++; }
    );
    IRSystem::addSystemExcludeTag<ExcludeTag>(sysId);
    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysId});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    EXPECT_EQ(IREntity::getComponent<Counter>(idA).n_, 1);
    EXPECT_EQ(IREntity::getComponent<Counter>(idB).n_, 0);
}

} // namespace

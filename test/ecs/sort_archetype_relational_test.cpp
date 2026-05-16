#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>

// Tests for sortArchetypeNodesByRelationChildOf exercised via
// queryArchetypeNodesRelational.  The bug (issue #750) was that the BFS
// seeded from leaf archetypes (pointed-to by CHILD_OF edges) and walked
// toward children — but leaves have no children, so parent archetypes
// were silently absent from the output.  The fix seeds from true roots
// (archetypes with no CHILD_OF relation of their own) and walks outward.

namespace {

struct C_BfsTestTag {
    int visited_ = 0;
};

class SortArchetypeRelationalTest : public testing::Test {
  protected:
    IREntity::EntityManager m_entity_manager;
};

// With the bug, only the child archetype node was returned; the parent
// archetype node was silently dropped.
TEST_F(SortArchetypeRelationalTest, ParentNodeIncludedInRelationalQuery) {
    auto parent = IREntity::createEntity(C_BfsTestTag{});
    auto child  = IREntity::createEntity(C_BfsTestTag{});
    IREntity::setParent(child, parent);

    auto include = IREntity::getArchetype<C_BfsTestTag>();
    auto nodes   = IREntity::queryArchetypeNodesRelational(IREntity::CHILD_OF, include);

    int total = 0;
    for (auto *node : nodes) total += node->length_;
    EXPECT_EQ(total, 2) << "both parent and child entities must be returned";
}

// Parent archetype node must appear before the child so a system that
// propagates values down the chain reads the correct parent state.
TEST_F(SortArchetypeRelationalTest, ParentNodeAppearsBeforeChildNode) {
    auto parent = IREntity::createEntity(C_BfsTestTag{});
    auto child  = IREntity::createEntity(C_BfsTestTag{});
    IREntity::setParent(child, parent);

    auto include = IREntity::getArchetype<C_BfsTestTag>();
    auto nodes   = IREntity::queryArchetypeNodesRelational(IREntity::CHILD_OF, include);

    ASSERT_EQ(nodes.size(), 2u);

    bool parent_first = false, child_second = false;
    for (auto id : nodes[0]->entities_) {
        if (id == parent) {
            parent_first = true;
            break;
        }
    }
    for (auto id : nodes[1]->entities_) {
        if (id == child) {
            child_second = true;
            break;
        }
    }
    EXPECT_TRUE(parent_first)  << "parent entity should be in the first returned node";
    EXPECT_TRUE(child_second)  << "child entity should be in the second returned node";
}

// Three-level chain: grandparent → parent → grandchild.  With the bug,
// only the grandchild (the leaf) was returned.  After the fix all three
// archetype nodes must appear.
TEST_F(SortArchetypeRelationalTest, ThreeLevelChainAllNodesReturned) {
    auto gp     = IREntity::createEntity(C_BfsTestTag{});
    auto parent = IREntity::createEntity(C_BfsTestTag{});
    auto child  = IREntity::createEntity(C_BfsTestTag{});
    IREntity::setParent(parent, gp);
    IREntity::setParent(child, parent);

    auto include = IREntity::getArchetype<C_BfsTestTag>();
    auto nodes   = IREntity::queryArchetypeNodesRelational(IREntity::CHILD_OF, include);

    int total = 0;
    for (auto *node : nodes) total += node->length_;
    EXPECT_EQ(total, 3) << "all three entities across three levels must be visited";
}

// A flat set of entities with no CHILD_OF relations must be returned
// unchanged (all nodes are roots).
TEST_F(SortArchetypeRelationalTest, FlatSetWithNoRelationsReturnsAllNodes) {
    IREntity::createEntity(C_BfsTestTag{});
    IREntity::createEntity(C_BfsTestTag{});
    IREntity::createEntity(C_BfsTestTag{});

    auto include = IREntity::getArchetype<C_BfsTestTag>();
    auto nodes   = IREntity::queryArchetypeNodesRelational(IREntity::CHILD_OF, include);

    int total = 0;
    for (auto *node : nodes) total += node->length_;
    EXPECT_EQ(total, 3);
}

} // namespace

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/system/components/component_static.hpp>

#include <vector>

// The entity-layer support for static residents
// (docs/design/static-residents.md): the per-component guard table, the
// cleared-by-foreign-move rule, staged entity notes and the relation hook.

namespace {

using IRComponents::C_Settled;
using IRComponents::C_Static;
using IREntity::EntityId;
using IREntity::StaticGuard;

struct C_LayerOther {
    int value_ = 0;
};

class StaticEntityLayer : public testing::Test {
  protected:
    IREntity::EntityManager m_entityManager;
};

TEST_F(StaticEntityLayer, RegistrationRecordsEachComponentsGuard) {
    using namespace IRComponents;
    EXPECT_EQ(
        m_entityManager.staticGuard(IREntity::getComponentType<C_LocalTransform>()),
        StaticGuard::INPUT
    );
    EXPECT_EQ(
        m_entityManager.staticGuard(IREntity::getComponentType<C_Modifiers>()),
        StaticGuard::INPUT
    );
    EXPECT_EQ(
        m_entityManager.staticGuard(IREntity::getComponentType<C_WorldTransform>()),
        StaticGuard::DERIVED
    );
    EXPECT_EQ(
        m_entityManager.staticGuard(IREntity::getComponentType<C_ResolvedFields>()),
        StaticGuard::DERIVED
    );
    EXPECT_EQ(
        m_entityManager.staticGuard(IREntity::getComponentType<C_LayerOther>()),
        StaticGuard::NONE
    );
    EXPECT_EQ(m_entityManager.staticGuard(IREntity::kNullComponent), StaticGuard::NONE);
}

TEST_F(StaticEntityLayer, FindComponentTypeNeverRegisters) {
    struct C_NeverNamed {};
    EXPECT_EQ(m_entityManager.findComponentType<C_NeverNamed>(), IREntity::kNullComponent);
    const IREntity::ComponentId id = IREntity::getComponentType<C_NeverNamed>();
    EXPECT_EQ(m_entityManager.findComponentType<C_NeverNamed>(), id);
}

TEST_F(StaticEntityLayer, SettledIsDroppedByEveryMoveThatDoesNotNameIt) {
    const auto hasSettled = [](EntityId entity) {
        return IREntity::getComponentOptional<C_Settled>(entity).has_value();
    };
    const EntityId added = IREntity::createEntity(C_Static{}, C_Settled{});
    IREntity::setComponent(added, C_LayerOther{4});
    EXPECT_FALSE(hasSettled(added));
    EXPECT_TRUE(IREntity::getComponentOptional<C_Static>(added).has_value());
    EXPECT_EQ(IREntity::getComponent<C_LayerOther>(added).value_, 4);

    const EntityId removed = IREntity::createEntity(C_Static{}, C_Settled{}, C_LayerOther{5});
    IREntity::removeComponent<C_LayerOther>(removed);
    EXPECT_FALSE(hasSettled(removed));

    const EntityId deferred = IREntity::createEntity(C_Static{}, C_Settled{}, C_LayerOther{6});
    IREntity::removeComponentDeferred<C_LayerOther>(deferred);
    IREntity::flushStructuralChanges();
    EXPECT_FALSE(hasSettled(deferred));

    const EntityId parent = IREntity::createEntity();
    const EntityId child = IREntity::createEntity(C_Static{}, C_Settled{});
    IREntity::setParent(child, parent);
    EXPECT_FALSE(hasSettled(child));
    IREntity::setComponent(child, C_Settled{});
    m_entityManager.clearParent(child);
    EXPECT_FALSE(hasSettled(child));

    // A move that names it keeps it. A raw write through a reference moves
    // nothing and reaches no rule.
    const EntityId kept = IREntity::createEntity(C_Static{}, C_LayerOther{7});
    IREntity::setComponent(kept, C_Settled{});
    EXPECT_TRUE(hasSettled(kept));
    IREntity::getComponent<C_LayerOther>(kept).value_ = 8;
    EXPECT_TRUE(hasSettled(kept));
    IREntity::removeComponent<C_Settled>(kept);
    EXPECT_FALSE(hasSettled(kept));
    EXPECT_EQ(IREntity::getComponent<C_LayerOther>(kept).value_, 8);
}

TEST_F(StaticEntityLayer, RemovingStaticDropsSettledInTheSameMove) {
    const EntityId entity = IREntity::createEntity(C_Static{}, C_Settled{}, C_LayerOther{9});
    const EntityId neighbour = IREntity::createEntity(C_Static{}, C_Settled{}, C_LayerOther{10});
    IREntity::removeComponentDeferred<C_Static>(entity);
    IREntity::flushStructuralChanges();
    EXPECT_FALSE(IREntity::getComponentOptional<C_Static>(entity).has_value());
    EXPECT_FALSE(IREntity::getComponentOptional<C_Settled>(entity).has_value());
    EXPECT_EQ(IREntity::getComponent<C_LayerOther>(entity).value_, 9);
    // The neighbour's row was swapped into the vacated one, column by column.
    EXPECT_TRUE(IREntity::getComponentOptional<C_Settled>(neighbour).has_value());
    EXPECT_EQ(IREntity::getComponent<C_LayerOther>(neighbour).value_, 10);
}

TEST_F(StaticEntityLayer, EntityNotesDrainInPushOrderAndOnlyOnce) {
    m_entityManager.stageEntityNote(11);
    m_entityManager.stageEntityNote(12);
    std::vector<EntityId> notes;
    m_entityManager.takeEntityNotes(notes);
    EXPECT_EQ(notes, (std::vector<EntityId>{11, 12}));
    m_entityManager.takeEntityNotes(notes);
    EXPECT_EQ(notes.size(), 2u);
}

TEST_F(StaticEntityLayer, AForeignMoveLogsTheEntityAndANamedOneDoesNot) {
    std::vector<EntityId> log;
    const EntityId entity = IREntity::createEntity(C_Static{}, C_Settled{});
    IREntity::setComponent(entity, C_LayerOther{1});
    m_entityManager.takeTrackedChanges(log);
    EXPECT_EQ(log, (std::vector<EntityId>{entity}));

    log.clear();
    IREntity::setComponent(entity, C_Settled{});
    IREntity::removeComponent<C_Settled>(entity);
    IREntity::removeComponent<C_LayerOther>(entity);
    m_entityManager.takeTrackedChanges(log);
    EXPECT_TRUE(log.empty()) << "nothing was dropped by a move that did not name it";
}

TEST_F(StaticEntityLayer, AParentChangeDropsSettledDownTheTrackedSubtreeAndLogsIt) {
    const EntityId root = IREntity::createEntity(C_Static{}, C_Settled{});
    const EntityId other = IREntity::createEntity(C_Static{}, C_Settled{});
    const EntityId child = IREntity::createEntity(C_Static{});
    const EntityId grandchild = IREntity::createEntity(C_Static{});
    const EntityId untracked = IREntity::createEntity(C_LayerOther{2});
    const EntityId below = IREntity::createEntity(C_Static{});
    IREntity::setParent(child, root);
    IREntity::setParent(grandchild, child);
    IREntity::setParent(untracked, child);
    IREntity::setParent(below, untracked);
    for (const EntityId entity : {child, grandchild, below}) {
        IREntity::setComponent(entity, C_Settled{});
    }
    std::vector<EntityId> log;
    m_entityManager.takeTrackedChanges(log);
    log.clear();
    const auto settled = [](EntityId entity) {
        return IREntity::getComponentOptional<C_Settled>(entity).has_value();
    };

    IREntity::setParent(child, root);
    m_entityManager.takeTrackedChanges(log);
    EXPECT_TRUE(log.empty()) << "a call that leaves the link as it was does nothing";
    EXPECT_TRUE(settled(child));

    IREntity::setParent(child, other);
    m_entityManager.takeTrackedChanges(log);
    EXPECT_EQ(log, (std::vector<EntityId>{child, grandchild}));
    EXPECT_FALSE(settled(child));
    EXPECT_FALSE(settled(grandchild));
    EXPECT_TRUE(settled(root));
    EXPECT_TRUE(settled(other));
    EXPECT_TRUE(settled(below)) << "the walk stops at a node that does not track its chain";
    EXPECT_TRUE(IREntity::getComponentOptional<C_Static>(grandchild).has_value());

    // Destroying a parent handles the children that still name it.
    for (const EntityId entity : {child, grandchild}) {
        IREntity::setComponent(entity, C_Settled{});
    }
    log.clear();
    m_entityManager.destroyEntity(other);
    m_entityManager.takeTrackedChanges(log);
    EXPECT_EQ(log, (std::vector<EntityId>{child, grandchild}));
    EXPECT_FALSE(settled(child));
    EXPECT_FALSE(settled(grandchild));
}

// An overwrite moves nothing, so `setComponent` applies the input-change
// rule itself: the entity always, its tracked subtree only for a pose
// component, which is what the descendants compose under.
TEST_F(StaticEntityLayer, ATypedOverwriteDropsSettledAndLogsTheEntity) {
    using IRComponents::C_LocalTransform;
    const EntityId root = IREntity::createEntity(C_Static{}, C_Settled{}, C_LayerOther{1});
    const EntityId child = IREntity::createEntity(C_Static{}, C_LayerOther{2});
    const EntityId dynamic = IREntity::createEntity(C_LayerOther{3});
    IREntity::setParent(child, root);
    IREntity::setComponent(child, C_Settled{});
    std::vector<EntityId> log;
    m_entityManager.takeTrackedChanges(log);
    log.clear();
    const auto settled = [](EntityId entity) {
        return IREntity::getComponentOptional<C_Settled>(entity).has_value();
    };

    IREntity::setComponent(dynamic, C_LayerOther{9});
    IREntity::setComponent(dynamic, C_LocalTransform{IRMath::vec3(4.0f)});
    m_entityManager.takeTrackedChanges(log);
    EXPECT_TRUE(log.empty()) << "a dynamic entity's overwrite tracks nothing";

    C_LayerOther &written = IREntity::setComponent(root, C_LayerOther{7});
    m_entityManager.takeTrackedChanges(log);
    EXPECT_EQ(log, (std::vector<EntityId>{root}));
    EXPECT_FALSE(settled(root));
    EXPECT_TRUE(settled(child)) << "not a pose component: the child composes under nothing new";
    EXPECT_EQ(&written, &IREntity::getComponent<C_LayerOther>(root))
        << "the reference is into the column the entity sits in after the move";
    EXPECT_EQ(written.value_, 7);

    log.clear();
    IREntity::setComponent(root, C_LayerOther{8});
    m_entityManager.takeTrackedChanges(log);
    EXPECT_EQ(log, (std::vector<EntityId>{root})) << "a settling entity is logged without a move";

    IREntity::setComponent(root, C_Settled{});
    log.clear();
    m_entityManager.takeTrackedChanges(log);
    log.clear();
    IREntity::setComponent(root, C_LocalTransform{IRMath::vec3(1.0f, 2.0f, 3.0f)});
    m_entityManager.takeTrackedChanges(log);
    EXPECT_EQ(log, (std::vector<EntityId>{root, child}));
    EXPECT_FALSE(settled(root));
    EXPECT_FALSE(settled(child));
    EXPECT_EQ(
        IREntity::getComponent<C_LocalTransform>(root).translation_,
        IRMath::vec3(1.0f, 2.0f, 3.0f)
    );

    for (const EntityId entity : {root, child}) {
        IREntity::setComponent(entity, C_Settled{});
    }
    log.clear();
    m_entityManager.takeTrackedChanges(log);
    log.clear();
    m_entityManager.setComponents(root, C_LayerOther{5}, C_LocalTransform{IRMath::vec3(6.0f)});
    EXPECT_FALSE(settled(root));
    EXPECT_FALSE(settled(child));

    for (const EntityId entity : {root, child}) {
        IREntity::setComponent(entity, C_Settled{});
    }
    IREntity::setComponentDeferred(root, C_LocalTransform{IRMath::vec3(8.0f)});
    EXPECT_TRUE(settled(root)) << "a deferred overwrite applies at the flush";
    m_entityManager.flushStructuralChanges();
    EXPECT_FALSE(settled(root));
    EXPECT_FALSE(settled(child));
    EXPECT_EQ(IREntity::getComponent<C_LocalTransform>(root).translation_, IRMath::vec3(8.0f));
}

TEST_F(StaticEntityLayer, ReservingRowsGrowsCapacityAndKeepsEveryRow) {
    std::vector<EntityId> made;
    for (int i = 0; i < 5; ++i) {
        made.push_back(IREntity::createEntity(C_LayerOther{i}));
    }
    IREntity::ArchetypeNode *node = m_entityManager.getRecord(made[0]).archetypeNode;
    std::vector<C_LayerOther> &column = IREntity::getComponentData<C_LayerOther>(node);
    m_entityManager.reserveRows(node, 4096);
    EXPECT_GE(column.capacity(), 4096u);
    EXPECT_GE(node->entities_.capacity(), 4096u);
    EXPECT_EQ(node->length_, 5);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(IREntity::getComponent<C_LayerOther>(made[i]).value_, i);
    }
    const std::size_t capacity = column.capacity();
    m_entityManager.reserveRows(node, 16);
    EXPECT_EQ(column.capacity(), capacity) << "never shrinks";
}

TEST_F(StaticEntityLayer, ReparentingADynamicSubtreeTracksNothing) {
    const EntityId first = IREntity::createEntity();
    const EntityId second = IREntity::createEntity();
    const EntityId top = IREntity::createEntity(C_LayerOther{0});
    IREntity::setParent(top, first);
    std::vector<EntityId> subtree;
    for (int i = 0; i < 10000; ++i) {
        subtree.push_back(IREntity::createEntity(C_LayerOther{i}));
        IREntity::setParent(subtree.back(), top);
    }
    const IREntity::ArchetypeNode *nodeBefore =
        m_entityManager.getRecord(subtree.front()).archetypeNode;
    IREntity::setParent(top, second);
    std::vector<EntityId> log;
    m_entityManager.takeTrackedChanges(log);
    EXPECT_TRUE(log.empty());
    EXPECT_EQ(m_entityManager.getRecord(subtree.front()).archetypeNode, nodeBefore);
    EXPECT_EQ(m_entityManager.getRecord(subtree.back()).archetypeNode, nodeBefore);
}

TEST_F(StaticEntityLayer, DestroyingATreeOrTheWorldTracksNoSurvivor) {
    const EntityId root = IREntity::createEntity(C_Static{}, C_Settled{});
    const EntityId child = IREntity::createEntity(C_Static{}, C_Settled{});
    IREntity::setParent(child, root);
    IREntity::setComponent(child, C_Settled{});
    std::vector<EntityId> log;
    m_entityManager.takeTrackedChanges(log);
    log.clear();
    m_entityManager.destroyTree(root);
    m_entityManager.takeTrackedChanges(log);
    EXPECT_TRUE(log.empty());

    const EntityId secondRoot = IREntity::createEntity(C_Static{}, C_Settled{});
    const EntityId secondChild = IREntity::createEntity(C_Static{});
    IREntity::setParent(secondChild, secondRoot);
    m_entityManager.takeTrackedChanges(log);
    log.clear();
    m_entityManager.destroyAllEntities();
    m_entityManager.takeTrackedChanges(log);
    EXPECT_TRUE(log.empty());
}

} // namespace

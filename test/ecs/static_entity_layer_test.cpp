#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>

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

    // A move that names it keeps it, and an overwrite is not a move.
    const EntityId kept = IREntity::createEntity(C_Static{}, C_LayerOther{7});
    IREntity::setComponent(kept, C_Settled{});
    EXPECT_TRUE(hasSettled(kept));
    IREntity::setComponent(kept, C_LayerOther{8});
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

TEST_F(StaticEntityLayer, RelationChangeHookFiresBeforeTheMoveWithTheChild) {
    std::vector<EntityId> seen;
    m_entityManager.registerRelationChangeHook([&seen](EntityId child) { seen.push_back(child); });
    const EntityId parent = IREntity::createEntity();
    const EntityId child = IREntity::createEntity();
    IREntity::setParent(child, parent);
    IREntity::setParent(child, parent);
    m_entityManager.clearParent(child);
    EXPECT_EQ(seen, (std::vector<EntityId>{child, child})) << "an unchanged parent fires nothing";
}

} // namespace

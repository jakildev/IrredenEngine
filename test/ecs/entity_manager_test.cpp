#include <gtest/gtest.h>
#include <irreden/ir_entity.hpp>

#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>

#include <stdexcept>
#include <type_traits>
#include <vector>

// These canvas components must require an explicit size at
// construction. Default-construction must be a compile error so a missing
// size shows up at the call site rather than as a runtime null-texture.
static_assert(
    !std::is_default_constructible_v<IRComponents::C_CanvasAOTexture>,
    "C_CanvasAOTexture must remain non-default-constructible — "
    "size argument is required."
);
static_assert(
    !std::is_default_constructible_v<IRComponents::C_CanvasSunShadow>,
    "C_CanvasSunShadow must remain non-default-constructible — "
    "size argument is required."
);

namespace {
struct TestMarker {};

struct TestRemovable {};

struct TestPayload {
    int value_ = 0;

    TestPayload() = default;
    explicit TestPayload(int value)
        : value_{value} {}
};

struct TestNonDefaultConstructible {
    int value_;

    TestNonDefaultConstructible() = delete;
    explicit TestNonDefaultConstructible(int value)
        : value_{value} {}
};

class IREntityTest : public testing::Test {
  protected:
    IREntityTest()
        : m_entity_manager{} {}

    ~IREntityTest() override {
        // Do tear-down work for each test here.
    }

    IREntity::EntityManager m_entity_manager;
};

TEST_F(IREntityTest, CreateEntity) {
    IREntity::EntityId newEntity = m_entity_manager.createEntity();
    EXPECT_NE(newEntity, IREntity::kNullEntity);
}

TEST_F(IREntityTest, RemoveComponentDeferredWaitsForFlush) {
    IREntity::EntityId entity = IREntity::createEntity(TestMarker{}, TestRemovable{});

    IREntity::removeComponentDeferred<TestRemovable>(entity);

    EXPECT_TRUE(IREntity::getComponentOptional<TestRemovable>(entity).has_value());

    IREntity::flushStructuralChanges();

    EXPECT_FALSE(IREntity::getComponentOptional<TestRemovable>(entity).has_value());
    EXPECT_TRUE(IREntity::getComponentOptional<TestMarker>(entity).has_value());
}

TEST_F(IREntityTest, RemoveComponentsDeferredRemovesAllMatchingEntitiesAfterFlush) {
    auto entityA = IREntity::createEntity(TestMarker{}, TestRemovable{});
    auto entityB = IREntity::createEntity(TestMarker{}, TestRemovable{});
    auto entityC = IREntity::createEntity(TestMarker{}, TestRemovable{});

    IREntity::removeComponentsDeferred<TestRemovable>(
        IREntity::getArchetype<TestMarker, TestRemovable>()
    );

    EXPECT_TRUE(IREntity::getComponentOptional<TestRemovable>(entityA).has_value());
    EXPECT_TRUE(IREntity::getComponentOptional<TestRemovable>(entityB).has_value());
    EXPECT_TRUE(IREntity::getComponentOptional<TestRemovable>(entityC).has_value());

    IREntity::flushStructuralChanges();

    EXPECT_FALSE(IREntity::getComponentOptional<TestRemovable>(entityA).has_value());
    EXPECT_FALSE(IREntity::getComponentOptional<TestRemovable>(entityB).has_value());
    EXPECT_FALSE(IREntity::getComponentOptional<TestRemovable>(entityC).has_value());
    EXPECT_TRUE(IREntity::getComponentOptional<TestMarker>(entityA).has_value());
    EXPECT_TRUE(IREntity::getComponentOptional<TestMarker>(entityB).has_value());
    EXPECT_TRUE(IREntity::getComponentOptional<TestMarker>(entityC).has_value());
}

TEST_F(IREntityTest, SetComponentDeferredAddsComponentAfterFlush) {
    IREntity::EntityId entity = IREntity::createEntity(TestMarker{});

    IREntity::setComponentDeferred(entity, TestPayload{42});

    EXPECT_FALSE(IREntity::getComponentOptional<TestPayload>(entity).has_value());

    IREntity::flushStructuralChanges();

    auto payload = IREntity::getComponentOptional<TestPayload>(entity);
    ASSERT_TRUE(payload.has_value());
    EXPECT_EQ((*payload)->value_, 42);
}

TEST_F(IREntityTest, RemoveComponentsSimpleRemovesImmediatelyFromSnapshot) {
    auto entityA = IREntity::createEntity(TestMarker{}, TestRemovable{});
    auto entityB = IREntity::createEntity(TestMarker{}, TestRemovable{});

    IREntity::removeComponentsSimple<TestRemovable>(
        IREntity::getArchetype<TestMarker, TestRemovable>()
    );

    EXPECT_FALSE(IREntity::getComponentOptional<TestRemovable>(entityA).has_value());
    EXPECT_FALSE(IREntity::getComponentOptional<TestRemovable>(entityB).has_value());
    EXPECT_TRUE(IREntity::getComponentOptional<TestMarker>(entityA).has_value());
    EXPECT_TRUE(IREntity::getComponentOptional<TestMarker>(entityB).has_value());
}

TEST_F(IREntityTest, PreDestroyHookFiresWithEntityIdBeforeDestruction) {
    IREntity::EntityId entity = IREntity::createEntity(TestMarker{});
    IREntity::EntityId observed = IREntity::kNullEntity;
    bool componentVisibleInHook = false;
    auto id = m_entity_manager.registerPreDestroyHook([&, entity](IREntity::EntityId destroyed) {
        observed = destroyed;
        // The entity must still be queryable at hook time — that's
        // the whole point of "pre-destroy", as opposed to post-.
        componentVisibleInHook = IREntity::getComponentOptional<TestMarker>(destroyed).has_value();
        EXPECT_EQ(destroyed, entity);
    });
    EXPECT_NE(id, IREntity::kInvalidPreDestroyHookId);

    m_entity_manager.destroyEntity(entity);

    EXPECT_EQ(observed, entity);
    EXPECT_TRUE(componentVisibleInHook);
}

TEST_F(IREntityTest, PreDestroyHooksFireInRegistrationOrder) {
    std::vector<int> order;
    m_entity_manager.registerPreDestroyHook([&](IREntity::EntityId) { order.push_back(1); });
    m_entity_manager.registerPreDestroyHook([&](IREntity::EntityId) { order.push_back(2); });
    m_entity_manager.registerPreDestroyHook([&](IREntity::EntityId) { order.push_back(3); });

    auto entity = IREntity::createEntity(TestMarker{});
    m_entity_manager.destroyEntity(entity);

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST_F(IREntityTest, UnregisterPreDestroyHookStopsFiring) {
    int fireCount = 0;
    auto id = m_entity_manager.registerPreDestroyHook([&](IREntity::EntityId) { ++fireCount; });

    auto entityA = IREntity::createEntity(TestMarker{});
    m_entity_manager.destroyEntity(entityA);
    EXPECT_EQ(fireCount, 1);

    m_entity_manager.unregisterPreDestroyHook(id);

    auto entityB = IREntity::createEntity(TestMarker{});
    m_entity_manager.destroyEntity(entityB);
    EXPECT_EQ(fireCount, 1);
}

// World teardown destroys in index order, not dependency order, so a hook
// that reads a dying entity's peers (the voxel-pool re-stage sweep reads the
// pool storage a set's span views alias) can fault on storage that is
// already gone. Hooks are therefore quiet for the whole drain, including the
// deferred deletes it processes first, and re-arm afterwards; `onDestroy()`
// is the per-component teardown path there.
TEST_F(IREntityTest, PreDestroyHooksAreQuietDuringDestroyAllEntities) {
    int fireCount = 0;
    m_entity_manager.registerPreDestroyHook([&](IREntity::EntityId) { ++fireCount; });

    IREntity::createEntity(TestMarker{});
    IREntity::EntityId marked = IREntity::createEntity(TestMarker{});
    m_entity_manager.markEntityForDeletion(marked);
    m_entity_manager.destroyAllEntities();
    EXPECT_EQ(fireCount, 0);

    auto survivor = IREntity::createEntity(TestMarker{});
    m_entity_manager.destroyEntity(survivor);
    EXPECT_EQ(fireCount, 1);
}

// Dead-id contract. Probing a destroyed or never-allocated id must answer
// honestly AND leave the entity index untouched. The second half is the
// load-bearing one: a lookup implemented with `std::unordered_map::operator[]`
// mints a `{nullptr, 0}` record on a miss, so the probe itself makes
// `entityExists` answer `true` for a dead id from then on — arming a delayed
// crash in innocent code that guards with `entityExists` and then derefs
// `archetypeNode`. That reads as accumulating store corruption rather than as
// one bad-id access.
//
// So every case below asserts `entityExists` is still false AFTER the probes.
// A suite that checks only the nullopt/false returns passes unchanged against
// an implementation that still pollutes the index.

TEST_F(IREntityTest, DeadIdProbesAnswerHonestlyAndDoNotPoisonIndex) {
    auto entity = IREntity::createEntity(TestMarker{}, TestPayload{5});
    const auto markerType = m_entity_manager.getComponentType<TestMarker>();
    ASSERT_TRUE(IREntity::entityExists(entity));

    m_entity_manager.destroyEntity(entity);
    ASSERT_FALSE(IREntity::entityExists(entity));

    // Honest answers for a dead id...
    EXPECT_FALSE(IREntity::getComponentOptional<TestMarker>(entity).has_value());
    EXPECT_FALSE(IREntity::getComponentOptional<TestPayload>(entity).has_value());
    EXPECT_FALSE(m_entity_manager.hasComponent(entity, markerType));
    EXPECT_EQ(m_entity_manager.getComponentDataAndRow(entity, markerType).first, nullptr);
    EXPECT_EQ(m_entity_manager.getComponentDataAndRow(entity, markerType).second, -1);

    // ...and none of those probes may resurrect it in the index.
    EXPECT_FALSE(IREntity::entityExists(entity))
        << "a probe of a dead id minted a poison record — entityExists now lies "
           "about it, which arms a delayed crash in code that trusts the answer";
    EXPECT_EQ(m_entity_manager.findRecord(entity), nullptr);
}

TEST_F(IREntityTest, FindRecordIsNullForNeverAllocatedId) {
    // A never-allocated id must probe cleanly too — the poison mint did not
    // care whether the id had ever existed.
    const IREntity::EntityId neverAllocated = IREntity::IR_MAX_ENTITIES - 1;
    ASSERT_FALSE(IREntity::entityExists(neverAllocated));

    EXPECT_EQ(m_entity_manager.findRecord(neverAllocated), nullptr);
    EXPECT_FALSE(m_entity_manager.hasComponent(
        neverAllocated,
        m_entity_manager.getComponentType<TestMarker>()
    ));
    EXPECT_FALSE(IREntity::getComponentOptional<TestMarker>(neverAllocated).has_value());
    EXPECT_FALSE(IREntity::entityExists(neverAllocated));
}

TEST_F(IREntityTest, FindRecordResolvesLiveEntityToItsPlacedRecord) {
    // Positive control for the two nullptr expectations above: findRecord is
    // not vacuously null-returning.
    auto entity = IREntity::createEntity(TestMarker{});

    const IREntity::EntityRecord *record = m_entity_manager.findRecord(entity);
    ASSERT_NE(record, nullptr);
    EXPECT_NE(record->archetypeNode, nullptr);
    EXPECT_GE(record->row, 0);
}

TEST_F(IREntityTest, GetComponentOnDeadIdAssertsInsteadOfDerefingNull) {
    auto entity = IREntity::createEntity(TestMarker{});
    m_entity_manager.destroyEntity(entity);

    // IR_ASSERT logs at critical and throws std::runtime_error; the test binary
    // is built debug, so EXPECT_THROW is the right harness. Pre-hardening this
    // was a null deref (SIGSEGV), not a throw.
    EXPECT_THROW(IREntity::getComponent<TestMarker>(entity), std::runtime_error);
    EXPECT_THROW(IREntity::getComponent<TestMarker>(IREntity::kNullEntity), std::runtime_error);

    // Even the throwing path must not have poisoned the index.
    EXPECT_FALSE(IREntity::entityExists(entity));
}

TEST_F(IREntityTest, DestroyEntityOnDeadIdAssertsAndFiresNoHooks) {
    auto entity = IREntity::createEntity(TestMarker{});
    m_entity_manager.destroyEntity(entity);

    int hookFires = 0;
    m_entity_manager.registerPreDestroyHook([&](IREntity::EntityId) { ++hookFires; });

    // A direct destroy of an already-dead id is a caller bug and must name
    // itself. The validation runs BEFORE the pre-destroy hooks so a bogus id
    // never reaches user callbacks.
    EXPECT_THROW(m_entity_manager.destroyEntity(entity), std::runtime_error);
    EXPECT_EQ(hookFires, 0);
    EXPECT_FALSE(IREntity::entityExists(entity));
}

TEST_F(IREntityTest, SetComponentDeferredOnDestroyedEntityIsDroppedAtFlush) {
    auto entity = IREntity::createEntity(TestMarker{});

    IREntity::setComponentDeferred(entity, TestPayload{42});
    m_entity_manager.destroyEntity(entity);

    // The queued op names a now-dead id. Flushing must drop it silently — and
    // must not mint a record for it on the way past.
    IREntity::flushStructuralChanges();

    EXPECT_FALSE(IREntity::entityExists(entity));
    EXPECT_EQ(m_entity_manager.findRecord(entity), nullptr);
}

TEST_F(IREntityTest, RemoveComponentDeferredOnDestroyedEntityIsDroppedAtFlush) {
    auto entity = IREntity::createEntity(TestMarker{}, TestRemovable{});
    auto survivor = IREntity::createEntity(TestMarker{}, TestRemovable{});

    IREntity::removeComponentDeferred<TestRemovable>(entity);
    IREntity::removeComponentDeferred<TestRemovable>(survivor);
    m_entity_manager.destroyEntity(entity);

    IREntity::flushStructuralChanges();

    EXPECT_FALSE(IREntity::entityExists(entity));
    EXPECT_EQ(m_entity_manager.findRecord(entity), nullptr);
    // The live entity in the same removal group is still processed — the skip
    // is per-entity, not a bail-out of the whole batch.
    EXPECT_FALSE(IREntity::getComponentOptional<TestRemovable>(survivor).has_value());
    EXPECT_TRUE(IREntity::getComponentOptional<TestMarker>(survivor).has_value());
}

TEST_F(IREntityTest, DestroyMarkedEntitiesToleratesDoubleMark) {
    auto entity = IREntity::createEntity(TestMarker{});
    auto other = IREntity::createEntity(TestMarker{});

    // Two systems marking the same entity in one frame is a set operation, not
    // a sequence — the drain must be idempotent rather than double-destroying.
    IREntity::EntityId firstMark = entity;
    IREntity::EntityId secondMark = entity;
    m_entity_manager.markEntityForDeletion(firstMark);
    m_entity_manager.markEntityForDeletion(secondMark);

    m_entity_manager.destroyMarkedEntities();

    EXPECT_FALSE(IREntity::entityExists(entity));
    EXPECT_EQ(m_entity_manager.findRecord(entity), nullptr);
    EXPECT_TRUE(IREntity::entityExists(other));
}

// Singleton-component API: one entity per component type, lazily
// created on first access, cached by ComponentId in EntityManager.
struct TestSingleton {
    int counter_ = 0;
};

TEST_F(IREntityTest, SingletonReturnsSameReferenceAcrossCalls) {
    auto &a = IREntity::singleton<TestSingleton>();
    auto &b = IREntity::singleton<TestSingleton>();
    EXPECT_EQ(&a, &b);
}

TEST_F(IREntityTest, SingletonEntityIsCachedById) {
    auto entityA = IREntity::singletonEntity<TestSingleton>();
    auto entityB = IREntity::singletonEntity<TestSingleton>();
    EXPECT_NE(entityA, IREntity::kNullEntity);
    EXPECT_EQ(entityA, entityB);
}

TEST_F(IREntityTest, SingletonMutationPersistsAcrossCalls) {
    IREntity::singleton<TestSingleton>().counter_ = 42;
    EXPECT_EQ(IREntity::singleton<TestSingleton>().counter_, 42);
    IREntity::singleton<TestSingleton>().counter_++;
    EXPECT_EQ(IREntity::singleton<TestSingleton>().counter_, 43);
}

TEST_F(IREntityTest, SingletonOrNullReturnsNullBeforeFirstCreate) {
    EXPECT_EQ(IREntity::singletonOrNull<TestSingleton>(), nullptr);
    EXPECT_EQ(IREntity::singletonEntityOrNull<TestSingleton>(), IREntity::kNullEntity);
}

TEST_F(IREntityTest, SingletonOrNullReturnsValidAfterCreate) {
    IREntity::singleton<TestSingleton>().counter_ = 7;
    auto *ptr = IREntity::singletonOrNull<TestSingleton>();
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->counter_, 7);
}

TEST_F(IREntityTest, SingletonLazyRecreateAfterExternalDestroy) {
    auto firstEntity = IREntity::singletonEntity<TestSingleton>();
    IREntity::singleton<TestSingleton>().counter_ = 99;
    m_entity_manager.destroyEntity(firstEntity);

    // Next access lazy-recreates with a default-constructed component.
    auto secondEntity = IREntity::singletonEntity<TestSingleton>();
    EXPECT_NE(secondEntity, firstEntity);
    EXPECT_EQ(IREntity::singleton<TestSingleton>().counter_, 0);
}

// destroyAllEntities() resets the singleton cache in one bulk clear, distinct
// from the per-entry eviction path covered by
// SingletonLazyRecreateAfterExternalDestroy above. Direct regression guard for
// the cache-clear invariant documented in EntityManager::destroyAllEntities().
TEST_F(IREntityTest, SingletonCacheResetAfterDestroyAllEntities) {
    auto firstEntity = IREntity::singletonEntity<TestSingleton>();
    IREntity::singleton<TestSingleton>().counter_ = 5;
    ASSERT_NE(firstEntity, IREntity::kNullEntity);
    ASSERT_EQ(IREntity::singletonEntityOrNull<TestSingleton>(), firstEntity);

    m_entity_manager.destroyAllEntities();

    EXPECT_EQ(IREntity::singletonEntityOrNull<TestSingleton>(), IREntity::kNullEntity);
    EXPECT_EQ(IREntity::singletonOrNull<TestSingleton>(), nullptr);

    // Lazy-recreate post-bulk-reset mints a fresh entity id with a
    // default-constructed component.
    auto secondEntity = IREntity::singletonEntity<TestSingleton>();
    EXPECT_NE(secondEntity, IREntity::kNullEntity);
    EXPECT_NE(secondEntity, firstEntity);
    EXPECT_EQ(IREntity::singleton<TestSingleton>().counter_, 0);
}

// setComponent must construct the new archetype slot directly from the
// caller's value rather than default-construct + assign — components like
// C_CanvasAOTexture rely on this so they can `= delete` their default ctor.
TEST_F(IREntityTest, SetComponentSupportsNonDefaultConstructibleType) {
    static_assert(
        !std::is_default_constructible_v<TestNonDefaultConstructible>,
        "Test component must remain non-default-constructible to be a "
        "meaningful regression guard."
    );

    auto entity = IREntity::createEntity(TestMarker{});

    IREntity::setComponent(entity, TestNonDefaultConstructible{7});

    auto opt = IREntity::getComponentOptional<TestNonDefaultConstructible>(entity);
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ((*opt)->value_, 7);

    IREntity::setComponent(entity, TestNonDefaultConstructible{42});
    EXPECT_EQ(IREntity::getComponent<TestNonDefaultConstructible>(entity).value_, 42);
}

// `resetGameplay`: scene-transition teardown. Destroys every gameplay
// entity but preserves singletons, C_Persistent-tagged entities, and the
// component-type backing entities. The key contrast with destroyAllEntities is
// that the singleton cache is NOT cleared and the world remains usable.

TEST_F(IREntityTest, ResetGameplayDestroysGameplayPreservesTagged) {
    auto keep = IREntity::createEntity(TestMarker{});
    IREntity::setComponent(keep, IRComponents::C_Persistent{});
    auto doomedA = IREntity::createEntity(TestMarker{});
    auto doomedB = IREntity::createEntity(TestMarker{}, TestPayload{3});

    IREntity::resetGameplay();

    EXPECT_TRUE(IREntity::entityExists(keep));
    EXPECT_FALSE(IREntity::entityExists(doomedA));
    EXPECT_FALSE(IREntity::entityExists(doomedB));
}

TEST_F(IREntityTest, ResetGameplayPreservesSingletonValueUnlikeDestroyAll) {
    IREntity::singleton<TestSingleton>().counter_ = 77;
    auto singletonId = IREntity::singletonEntity<TestSingleton>();
    IREntity::createEntity(TestMarker{}); // gameplay entity, should be destroyed

    IREntity::resetGameplay();

    // Unlike destroyAllEntities (which clears the cache + recreates with a
    // default), the singleton entity and its value survive intact.
    EXPECT_EQ(IREntity::singletonEntityOrNull<TestSingleton>(), singletonId);
    EXPECT_EQ(IREntity::singleton<TestSingleton>().counter_, 77);
}

TEST_F(IREntityTest, ResetGameplayKeepsComponentTypesUsableInNextScene) {
    // Scene A registers TestMarker + TestPayload (and their backing
    // component-type entities).
    IREntity::createEntity(TestMarker{}, TestPayload{1});

    IREntity::resetGameplay();

    // Scene B: the same component types must still create + read correctly —
    // regression guard for component-type-entity preservation.
    auto entity = IREntity::createEntity(TestMarker{}, TestPayload{99});
    auto payload = IREntity::getComponentOptional<TestPayload>(entity);
    ASSERT_TRUE(payload.has_value());
    EXPECT_EQ((*payload)->value_, 99);
}

TEST_F(IREntityTest, ResetGameplayPrunesNamesOfDestroyedEntities) {
    auto keep = IREntity::createEntity(TestMarker{});
    IREntity::setComponent(keep, IRComponents::C_Persistent{});
    IREntity::setName(keep, "keeper");

    auto doomed = IREntity::createEntity(TestMarker{});
    IREntity::setName(doomed, "doomed");

    IREntity::resetGameplay();

    // Preserved entity keeps its name; destroyed entity's stale name->id entry
    // is pruned (otherwise getEntityByName would later assert on a dead id).
    EXPECT_TRUE(m_entity_manager.hasName("keeper"));
    EXPECT_FALSE(m_entity_manager.hasName("doomed"));
    EXPECT_EQ(IREntity::getEntity("keeper"), keep);
}

TEST_F(IREntityTest, ResetGameplayLiveCountIsIdempotentAcrossCycles) {
    IREntity::singleton<TestSingleton>().counter_ = 1;
    auto persistent = IREntity::createEntity(TestMarker{});
    IREntity::setComponent(persistent, IRComponents::C_Persistent{});

    auto buildScene = [](int count) {
        for (int i = 0; i < count; ++i) {
            IREntity::createEntity(TestMarker{}, TestPayload{i});
        }
    };

    // Warm up: register every component type used, then reset so the baseline
    // reflects the post-registration steady state (ids never recycle, so the
    // baseline is a count, not specific id values).
    buildScene(5);
    IREntity::resetGameplay();
    const IREntity::EntityId baseline = IREntity::getLiveEntityCount();

    for (int cycle = 0; cycle < 12; ++cycle) {
        buildScene(7 + cycle); // vary the scene size each cycle
        EXPECT_GT(IREntity::getLiveEntityCount(), baseline);
        IREntity::resetGameplay();
        EXPECT_EQ(IREntity::getLiveEntityCount(), baseline)
            << "live entity count drifted at cycle " << cycle;
    }

    // Preserved entities survived every cycle.
    EXPECT_EQ(IREntity::singleton<TestSingleton>().counter_, 1);
    EXPECT_TRUE(IREntity::entityExists(persistent));
}

// Re-parenting replaces the CHILD_OF relation in the child's archetype; it
// never stacks a second one.
TEST_F(IREntityTest, SetParentReplacesExistingParent) {
    const auto first = IREntity::createEntity();
    const auto second = IREntity::createEntity();
    const auto child = IREntity::createEntity();
    IREntity::setParent(child, first);
    IREntity::setParent(child, second);

    EXPECT_EQ(m_entity_manager.getParent(child), second);
    EXPECT_TRUE(m_entity_manager.getChildren(first).empty());
    EXPECT_EQ(m_entity_manager.getChildren(second), std::vector<IREntity::EntityId>{child});
    int relations = 0;
    for (auto id : m_entity_manager.getRecord(child).archetypeNode->type_) {
        relations += m_entity_manager.isChildOfRelation(id) ? 1 : 0;
    }
    EXPECT_EQ(relations, 1);

    m_entity_manager.clearParent(child);
    EXPECT_EQ(m_entity_manager.getParent(child), IREntity::kNullEntity);
    EXPECT_TRUE(m_entity_manager.getChildren(second).empty());
}

TEST_F(IREntityTest, IsAncestorWalksTheParentChain) {
    const auto root = IREntity::createEntity();
    const auto mid = IREntity::createEntity();
    const auto leaf = IREntity::createEntity();
    IREntity::setParent(mid, root);
    IREntity::setParent(leaf, mid);

    EXPECT_TRUE(m_entity_manager.isAncestor(root, leaf));
    EXPECT_TRUE(m_entity_manager.isAncestor(mid, leaf));
    EXPECT_FALSE(m_entity_manager.isAncestor(leaf, root));
    EXPECT_FALSE(m_entity_manager.isAncestor(leaf, leaf));
}

TEST_F(IREntityTest, SetParentAssertsOnCycle) {
    const auto root = IREntity::createEntity();
    const auto leaf = IREntity::createEntity();
    IREntity::setParent(leaf, root);

    EXPECT_THROW(IREntity::setParent(root, leaf), std::runtime_error);
    EXPECT_THROW(IREntity::setParent(root, root), std::runtime_error);
    EXPECT_EQ(m_entity_manager.getParent(root), IREntity::kNullEntity);
    EXPECT_EQ(m_entity_manager.getParent(leaf), root);
}

// The facade marks the tree; nothing dies until the drain.
TEST_F(IREntityTest, FacadeDestroyTreeMarksUntilDrain) {
    const auto root = IREntity::createEntity();
    const auto child = IREntity::createEntity();
    const auto grandchild = IREntity::createEntity();
    const auto bystander = IREntity::createEntity();
    IREntity::setParent(child, root);
    IREntity::setParent(grandchild, child);

    IREntity::destroyTree(root);
    EXPECT_TRUE(IREntity::entityExists(root));
    EXPECT_TRUE(IREntity::entityExists(grandchild));

    m_entity_manager.destroyMarkedEntities();
    EXPECT_FALSE(IREntity::entityExists(root));
    EXPECT_FALSE(IREntity::entityExists(child));
    EXPECT_FALSE(IREntity::entityExists(grandchild));
    EXPECT_TRUE(IREntity::entityExists(bystander));
}

// Children die before their parents, so a pre-destroy hook can still read
// the parent of the entity it sees.
TEST_F(IREntityTest, DestroyTreeDestroysChildrenBeforeParents) {
    const auto root = IREntity::createEntity();
    const auto child = IREntity::createEntity();
    const auto grandchild = IREntity::createEntity();
    IREntity::setParent(child, root);
    IREntity::setParent(grandchild, child);

    std::vector<IREntity::EntityId> order;
    std::vector<bool> parentAlive;
    const auto hook = m_entity_manager.registerPreDestroyHook([&](IREntity::EntityId entity) {
        order.push_back(entity & IREntity::IR_ENTITY_ID_BITS);
        const auto parent = m_entity_manager.getParent(entity);
        parentAlive.push_back(parent == IREntity::kNullEntity || IREntity::entityExists(parent));
    });
    m_entity_manager.destroyTree(root);
    m_entity_manager.unregisterPreDestroyHook(hook);

    EXPECT_EQ(order, (std::vector<IREntity::EntityId>{grandchild, child, root}));
    EXPECT_EQ(parentAlive, (std::vector<bool>{true, true, true}));
}

// A plain mark of the root queued ahead of the tree request must not kill
// the root before its children.
TEST_F(IREntityTest, DeferredTreeOutranksEarlierPlainRootMark) {
    const auto root = IREntity::createEntity();
    const auto child = IREntity::createEntity();
    const auto grandchild = IREntity::createEntity();
    IREntity::setParent(child, root);
    IREntity::setParent(grandchild, child);

    std::vector<IREntity::EntityId> order;
    std::vector<bool> parentAlive;
    const auto hook = m_entity_manager.registerPreDestroyHook([&](IREntity::EntityId entity) {
        order.push_back(entity & IREntity::IR_ENTITY_ID_BITS);
        const auto parent = m_entity_manager.getParent(entity);
        parentAlive.push_back(parent == IREntity::kNullEntity || IREntity::entityExists(parent));
    });
    IREntity::destroyEntity(root);
    IREntity::destroyEntity(child);
    IREntity::destroyTree(root);
    m_entity_manager.destroyMarkedEntities();
    m_entity_manager.unregisterPreDestroyHook(hook);

    EXPECT_EQ(order, (std::vector<IREntity::EntityId>{grandchild, child, root}));
    EXPECT_EQ(parentAlive, (std::vector<bool>{true, true, true}));
    EXPECT_FALSE(IREntity::entityExists(root));
}

// Two tree requests on one root, with a child parented between them: the
// drain orders by the hierarchy as it stands, not by request order.
TEST_F(IREntityTest, DeferredTreeOrdersOverlappingRequestsChildFirst) {
    const auto root = IREntity::createEntity();
    const auto child = IREntity::createEntity();

    std::vector<IREntity::EntityId> order;
    const auto hook = m_entity_manager.registerPreDestroyHook([&](IREntity::EntityId entity) {
        order.push_back(entity & IREntity::IR_ENTITY_ID_BITS);
    });
    IREntity::destroyTree(root);
    IREntity::setParent(child, root);
    IREntity::destroyTree(root);
    m_entity_manager.destroyMarkedEntities();
    m_entity_manager.unregisterPreDestroyHook(hook);

    EXPECT_EQ(order, (std::vector<IREntity::EntityId>{child, root}));
}

// A hook that tree-marks a peer mid-drain: the peer's later plain mark must
// not kill it ahead of its child.
TEST_F(IREntityTest, DeferredTreeQueuedByHookOutranksLaterPlainMark) {
    const auto trigger = IREntity::createEntity();
    const auto root = IREntity::createEntity();
    const auto child = IREntity::createEntity();
    IREntity::setParent(child, root);

    std::vector<IREntity::EntityId> order;
    std::vector<bool> parentAlive;
    const auto hook = m_entity_manager.registerPreDestroyHook([&](IREntity::EntityId entity) {
        const auto id = entity & IREntity::IR_ENTITY_ID_BITS;
        order.push_back(id);
        const auto parent = m_entity_manager.getParent(entity);
        parentAlive.push_back(parent == IREntity::kNullEntity || IREntity::entityExists(parent));
        if (id == trigger) {
            IREntity::destroyTree(root);
        }
    });
    IREntity::destroyEntity(trigger);
    IREntity::destroyEntity(root);
    m_entity_manager.destroyMarkedEntities();
    m_entity_manager.unregisterPreDestroyHook(hook);

    EXPECT_EQ(order, (std::vector<IREntity::EntityId>{trigger, child, root}));
    EXPECT_EQ(parentAlive, (std::vector<bool>{true, true, true}));
    EXPECT_FALSE(IREntity::entityExists(child));
}

// The same hook firing inside the tree drain itself: the peer is already in
// the drain's list without the child it gained since its own mark.
TEST_F(IREntityTest, DeferredTreeQueuedByHookReordersTheTreeDrain) {
    const auto trigger = IREntity::createEntity();
    const auto root = IREntity::createEntity();
    const auto child = IREntity::createEntity();

    std::vector<IREntity::EntityId> order;
    std::vector<bool> parentAlive;
    const auto hook = m_entity_manager.registerPreDestroyHook([&](IREntity::EntityId entity) {
        const auto id = entity & IREntity::IR_ENTITY_ID_BITS;
        order.push_back(id);
        const auto parent = m_entity_manager.getParent(entity);
        parentAlive.push_back(parent == IREntity::kNullEntity || IREntity::entityExists(parent));
        if (id == trigger) {
            IREntity::destroyTree(root);
        }
    });
    IREntity::destroyTree(trigger);
    IREntity::destroyTree(root);
    IREntity::setParent(child, root);
    m_entity_manager.destroyMarkedEntities();
    m_entity_manager.unregisterPreDestroyHook(hook);

    EXPECT_EQ(order, (std::vector<IREntity::EntityId>{trigger, child, root}));
    EXPECT_EQ(parentAlive, (std::vector<bool>{true, true, true}));
    EXPECT_FALSE(IREntity::entityExists(child));
}

TEST_F(IREntityTest, DeferredTreeReranksAfterHookReparentsPendingPeer) {
    const auto root = IREntity::createEntity();
    const auto first = IREntity::createEntity();
    const auto second = IREntity::createEntity();
    const auto trigger = IREntity::createEntity();
    IREntity::setParent(first, root);
    IREntity::setParent(second, root);
    IREntity::setParent(trigger, first);

    std::vector<IREntity::EntityId> order;
    std::vector<bool> parentAlive;
    const auto hook = m_entity_manager.registerPreDestroyHook([&](IREntity::EntityId entity) {
        const auto id = entity & IREntity::IR_ENTITY_ID_BITS;
        order.push_back(id);
        const auto parent = m_entity_manager.getParent(entity);
        parentAlive.push_back(parent == IREntity::kNullEntity || IREntity::entityExists(parent));
        if (id == trigger) {
            IREntity::setParent(second, first);
        }
    });
    IREntity::destroyTree(root);
    m_entity_manager.destroyMarkedEntities();
    m_entity_manager.unregisterPreDestroyHook(hook);

    EXPECT_EQ(order, (std::vector<IREntity::EntityId>{trigger, second, first, root}));
    EXPECT_EQ(parentAlive, (std::vector<bool>{true, true, true, true}));
}

// The eager verb re-ranks too: destroying in snapshot post-order would kill
// `first` ahead of the `second` it adopted.
TEST_F(IREntityTest, EagerDestroyTreeReranksAfterHookReparentsPendingPeer) {
    const auto root = IREntity::createEntity();
    const auto first = IREntity::createEntity();
    const auto second = IREntity::createEntity();
    const auto trigger = IREntity::createEntity();
    IREntity::setParent(first, root);
    IREntity::setParent(second, root);
    IREntity::setParent(trigger, first);

    std::vector<IREntity::EntityId> order;
    std::vector<bool> parentAlive;
    const auto hook = m_entity_manager.registerPreDestroyHook([&](IREntity::EntityId entity) {
        const auto id = entity & IREntity::IR_ENTITY_ID_BITS;
        order.push_back(id);
        const auto parent = m_entity_manager.getParent(entity);
        parentAlive.push_back(parent == IREntity::kNullEntity || IREntity::entityExists(parent));
        if (id == trigger) {
            IREntity::setParent(second, first);
        }
    });
    m_entity_manager.destroyTree(root);
    m_entity_manager.unregisterPreDestroyHook(hook);

    EXPECT_EQ(order, (std::vector<IREntity::EntityId>{trigger, second, first, root}));
    EXPECT_EQ(parentAlive, (std::vector<bool>{true, true, true, true}));
}
} // namespace

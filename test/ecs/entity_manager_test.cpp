#include <gtest/gtest.h>
#include <irreden/ir_entity.hpp>

#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>

#include <type_traits>

// Issue #367: these canvas components must require an explicit size at
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

// Singleton-component API (T-162): one entity per component type, lazily
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
} // namespace

#include <gtest/gtest.h>
#include <irreden/ir_entity.hpp>

namespace {
struct TestMarker {
};

struct TestRemovable {
};

struct TestPayload {
    int value_ = 0;

    TestPayload() = default;
    explicit TestPayload(int value)
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
    auto id = m_entity_manager.registerPreDestroyHook(
        [&, entity](IREntity::EntityId destroyed) {
            observed = destroyed;
            // The entity must still be queryable at hook time — that's
            // the whole point of "pre-destroy", as opposed to post-.
            componentVisibleInHook =
                IREntity::getComponentOptional<TestMarker>(destroyed).has_value();
            EXPECT_EQ(destroyed, entity);
        }
    );
    EXPECT_NE(id, IREntity::kInvalidPreDestroyHookId);

    m_entity_manager.destroyEntity(entity);

    EXPECT_EQ(observed, entity);
    EXPECT_TRUE(componentVisibleInHook);
}

TEST_F(IREntityTest, PreDestroyHooksFireInRegistrationOrder) {
    std::vector<int> order;
    m_entity_manager.registerPreDestroyHook(
        [&](IREntity::EntityId) { order.push_back(1); }
    );
    m_entity_manager.registerPreDestroyHook(
        [&](IREntity::EntityId) { order.push_back(2); }
    );
    m_entity_manager.registerPreDestroyHook(
        [&](IREntity::EntityId) { order.push_back(3); }
    );

    auto entity = IREntity::createEntity(TestMarker{});
    m_entity_manager.destroyEntity(entity);

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST_F(IREntityTest, UnregisterPreDestroyHookStopsFiring) {
    int fireCount = 0;
    auto id = m_entity_manager.registerPreDestroyHook(
        [&](IREntity::EntityId) { ++fireCount; }
    );

    auto entityA = IREntity::createEntity(TestMarker{});
    m_entity_manager.destroyEntity(entityA);
    EXPECT_EQ(fireCount, 1);

    m_entity_manager.unregisterPreDestroyHook(id);

    auto entityB = IREntity::createEntity(TestMarker{});
    m_entity_manager.destroyEntity(entityB);
    EXPECT_EQ(fireCount, 1);
}
} // namespace

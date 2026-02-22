#include <gtest/gtest.h>
#include <irreden/entity/entity_manager.hpp>

namespace {
class IREntityTest : public testing::Test {
  protected:
    IREntityTest()
        : m_entityManager{} {}

    ~IREntityTest() override {
        // Do tear-down work for each test here.
    }

    IREntity::EntityManager m_entityManager;
};

TEST_F(IREntityTest, CreateEntity) {
    IREntity::EntityId newEntity = m_entityManager.createEntity();
    EXPECT_NE(newEntity, IREntity::kNullEntity);
}
} // namespace

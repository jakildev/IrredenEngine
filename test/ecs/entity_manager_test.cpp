#include <gtest/gtest.h>
#include <irreden/entity/entity_manager.hpp>

namespace {
    class IREntityTest : public testing::Test {
        protected:
            IREntityTest()
            :   m_entityManager{}
            {

            }

            ~IREntityTest() override {
                // Do tear-down work for each test here.
            }

           IRECS::EntityManager m_entityManager;
    };

    TEST_F(IREntityTest, CreateEntity) {
        IRECS::EntityId newEntity = m_entityManager.createEntity();
        EXPECT_NE(newEntity, IRECS::kNullEntity);
    }
}

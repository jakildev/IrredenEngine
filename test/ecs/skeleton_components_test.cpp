#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/voxel/components/component_skeleton.hpp>
#include <irreden/voxel/components/component_joint.hpp>

using IRComponents::C_Joint;
using IRComponents::C_Skeleton;

namespace {

class SkeletonComponentsTest : public testing::Test {
  protected:
    IREntity::EntityManager m_entity_manager;
};

TEST_F(SkeletonComponentsTest, JointTagRegistersAndQueriesByArchetype) {
    auto j0 = IREntity::createEntity(C_Joint{});
    auto j1 = IREntity::createEntity(C_Joint{});

    int count = 0;
    IREntity::forEachComponent<C_Joint>([&](C_Joint &) { ++count; });
    EXPECT_EQ(count, 2);

    EXPECT_NE(j0, j1);
}

TEST_F(SkeletonComponentsTest, SkeletonHoldsJointEntityIds) {
    auto root = IREntity::createEntity(C_Skeleton{});
    auto j0 = IREntity::createEntity(C_Joint{});
    auto j1 = IREntity::createEntity(C_Joint{});

    auto &skel = IREntity::getComponent<C_Skeleton>(root);
    skel.joints_ = {j0, j1};

    auto &readback = IREntity::getComponent<C_Skeleton>(root);
    ASSERT_EQ(readback.joints_.size(), 2u);
    EXPECT_EQ(readback.joints_[0], j0);
    EXPECT_EQ(readback.joints_[1], j1);
}

TEST_F(SkeletonComponentsTest, SeveredSlotIsKNullEntitySentinel) {
    auto root = IREntity::createEntity(C_Skeleton{});
    auto j0 = IREntity::createEntity(C_Joint{});
    auto j1 = IREntity::createEntity(C_Joint{});

    auto &skel = IREntity::getComponent<C_Skeleton>(root);
    skel.joints_ = {j0, j1};
    skel.joints_[0] = IREntity::kNullEntity;

    EXPECT_EQ(skel.joints_[0], IREntity::kNullEntity)
        << "severed slot preserves index space via kNullEntity sentinel";
    EXPECT_EQ(skel.joints_[1], j1)
        << "trailing joint indices are stable after severance at index 0";
}

} // namespace

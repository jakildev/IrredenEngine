#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/asset/rig_format.hpp>
#include <irreden/voxel/components/component_joint.hpp>
#include <irreden/voxel/components/component_skeleton.hpp>
#include <irreden/voxel/rig_bridge.hpp>
#include <irreden/voxel/skeleton.hpp>

// #1602 — bind-pose on C_Skeleton + skin-matrix helper.
//
// Geometry shared by the cases below: a straight 3-bone chain along +X. joint0
// is the root at the origin; joint1 sits 2 units out; joint2 sits 3 units past
// joint1. At rest every joint rotation is identity, so the rest world
// translations are (0,0,0), (2,0,0), (5,0,0).

namespace {

constexpr float kTolerance = 1e-4f;

IRAsset::Rig makeChainRig() {
    IRAsset::Rig rig;
    rig.joints_.resize(3);
    // Root: parentIndex == own index is the chain-terminator sentinel.
    rig.joints_[0].parentIndex_ = 0;
    rig.joints_[0].translation_ = IRMath::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    rig.joints_[1].parentIndex_ = 0;
    rig.joints_[1].translation_ = IRMath::vec4(2.0f, 0.0f, 0.0f, 0.0f);
    rig.joints_[2].parentIndex_ = 1;
    rig.joints_[2].translation_ = IRMath::vec4(3.0f, 0.0f, 0.0f, 0.0f);
    return rig;
}

void expectVec3Near(const IRMath::vec3 &actual, const IRMath::vec3 &expected) {
    EXPECT_NEAR(actual.x, expected.x, kTolerance);
    EXPECT_NEAR(actual.y, expected.y, kTolerance);
    EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

// Applies a 4×4 affine transform to a point (w == 1, no perspective divide).
IRMath::vec3 applyToPoint(const IRMath::mat4 &m, const IRMath::vec3 &p) {
    return IRMath::vec3(m * IRMath::vec4(p, 1.0f));
}

TEST(SkeletonSkinningTest, BindPoseSizeMatchesJointsAndComposesChain) {
    const IRAsset::Rig rig = makeChainRig();
    const std::vector<IRMath::SQT> pose = IRPrefab::Rig::bindPose(rig);

    ASSERT_EQ(pose.size(), rig.joints_.size());
    expectVec3Near(pose[0].translation_, IRMath::vec3(0.0f, 0.0f, 0.0f));
    expectVec3Near(pose[1].translation_, IRMath::vec3(2.0f, 0.0f, 0.0f));
    // joint2 accumulates joint1's offset: (2,0,0) + (3,0,0).
    expectVec3Near(pose[2].translation_, IRMath::vec3(5.0f, 0.0f, 0.0f));
}

TEST(SkeletonSkinningTest, SkinMatrixIsIdentityAtBindPose) {
    const IRAsset::Rig rig = makeChainRig();
    const std::vector<IRMath::SQT> pose = IRPrefab::Rig::bindPose(rig);

    // A joint sitting exactly at its bind transform contributes no deformation.
    for (const IRMath::SQT &bind : pose) {
        const IRMath::mat4 skin = IRPrefab::Skeleton::skinMatrix(bind, bind);
        expectVec3Near(applyToPoint(skin, IRMath::vec3(1.0f, 2.0f, 3.0f)),
                       IRMath::vec3(1.0f, 2.0f, 3.0f));
    }
}

TEST(SkeletonSkinningTest, RotatingMiddleJointSwingsChildByParentRotation) {
    const IRAsset::Rig rig = makeChainRig();
    const std::vector<IRMath::SQT> bind = IRPrefab::Rig::bindPose(rig);

    // Pose: rotate the middle joint (joint1) +90° about Z. joint1's posed local
    // is rest-translation with a quarter-turn; joint2 inherits it through the
    // chain. Compose the posed world transforms with the same engine
    // convention bindPose uses (sqtCompose).
    IRMath::SQT posedLocal1;
    posedLocal1.rotation_ = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), IRMath::kHalfPi);
    posedLocal1.translation_ = IRMath::vec3(2.0f, 0.0f, 0.0f);

    IRMath::SQT local2;
    local2.translation_ = IRMath::vec3(3.0f, 0.0f, 0.0f);

    const IRMath::SQT posedWorld1 = IRMath::sqtCompose(IRMath::SQT{}, posedLocal1);
    const IRMath::SQT posedWorld2 = IRMath::sqtCompose(posedWorld1, local2);

    // The child's offset (3,0,0) past joint1 swings to +Y about joint1's pivot.
    expectVec3Near(posedWorld2.translation_, IRMath::vec3(2.0f, 3.0f, 0.0f));

    const IRMath::mat4 skin = IRPrefab::Skeleton::skinMatrix(posedWorld2, bind[2]);

    // A voxel authored at joint2's bind origin lands at joint2's posed origin.
    expectVec3Near(applyToPoint(skin, bind[2].translation_),
                   IRMath::vec3(2.0f, 3.0f, 0.0f));
    // A voxel one unit further along +X in bind space lands one unit along +Y
    // past the posed child origin — confirming the parent rotation propagated
    // with the right handedness, not just the translation.
    expectVec3Near(applyToPoint(skin, IRMath::vec3(6.0f, 0.0f, 0.0f)),
                   IRMath::vec3(2.0f, 4.0f, 0.0f));
}

class SkeletonSkinningEntityTest : public testing::Test {
  protected:
    IREntity::EntityManager m_entity_manager;
};

TEST_F(SkeletonSkinningEntityTest, EntityOverloadReadsWorldTransform) {
    const IRAsset::Rig rig = makeChainRig();
    const std::vector<IRMath::SQT> bind = IRPrefab::Rig::bindPose(rig);

    // Stand in for PROPAGATE_TRANSFORM: write joint2's posed world transform
    // directly, then confirm the entity overload reproduces the SQT overload.
    auto joint = IREntity::createEntity(IRComponents::C_Joint{});
    auto &world = IREntity::getComponent<IRComponents::C_WorldTransform>(joint);
    world.rotation_ = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), IRMath::kHalfPi);
    world.translation_ = IRMath::vec3(2.0f, 3.0f, 0.0f);

    const IRMath::SQT jointWorld{world.scale_, world.rotation_, world.translation_};
    const IRMath::mat4 viaEntity = IRPrefab::Skeleton::skinMatrix(joint, bind[2]);
    const IRMath::mat4 viaSqt = IRPrefab::Skeleton::skinMatrix(jointWorld, bind[2]);

    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_NEAR(viaEntity[col][row], viaSqt[col][row], kTolerance);
        }
    }
}

} // namespace

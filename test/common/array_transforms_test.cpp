#include <gtest/gtest.h>

#include <irreden/common/array_transforms.hpp>

namespace {

void expectVec3Near(IRMath::vec3 actual, IRMath::vec3 expected) {
    EXPECT_NEAR(actual.x, expected.x, 0.0001f);
    EXPECT_NEAR(actual.y, expected.y, 0.0001f);
    EXPECT_NEAR(actual.z, expected.z, 0.0001f);
}

TEST(ArrayTransforms, RadialOffsets) {
    const auto transforms = IRPrefab::Arrays::radial(6, IRMath::vec3(0.0f, 0.0f, 1.0f), 4.0f);

    ASSERT_EQ(transforms.size(), 6u);
    for (std::size_t i = 0; i < transforms.size(); ++i) {
        EXPECT_NEAR(IRMath::length(transforms[i].translation_), 4.0f, 0.0001f);
        EXPECT_NEAR(transforms[i].translation_.z, 0.0f, 0.0001f);
        const float angle = IRMath::kTwoPi * static_cast<float>(i) / 6.0f;
        expectVec3Near(
            transforms[i].translation_,
            IRMath::vec3(4.0f * IRMath::cos(angle), 4.0f * IRMath::sin(angle), 0.0f)
        );
    }
}

TEST(ArrayTransforms, LinearOffsets) {
    const auto transforms = IRPrefab::Arrays::linear(3, IRMath::vec3(2.0f, 0.0f, 0.0f));

    ASSERT_EQ(transforms.size(), 3u);
    expectVec3Near(transforms[0].translation_, IRMath::vec3(0.0f));
    expectVec3Near(transforms[1].translation_, IRMath::vec3(2.0f, 0.0f, 0.0f));
    expectVec3Near(transforms[2].translation_, IRMath::vec3(4.0f, 0.0f, 0.0f));
}

} // namespace

#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>

namespace {

constexpr float kTolerance = 1e-5f;

using IRMath::SDF::ShapeType;

TEST(SdfEvaluate, BoxEffectiveParamsMatchVoxelCountSemantics) {
    const IRMath::vec4 rawParams{7.0f, 7.0f, 7.0f, 0.0f};
    const IRMath::vec4 params = IRMath::SDF::effectiveParams(ShapeType::BOX, rawParams);

    EXPECT_FLOAT_EQ(params.x, 6.0f);
    EXPECT_NEAR(
        IRMath::SDF::evaluate(IRMath::vec3(3.0f, 0.0f, 0.0f), ShapeType::BOX, params),
        0.0f,
        kTolerance
    );
    EXPECT_GT(
        IRMath::SDF::evaluate(IRMath::vec3(4.0f, 0.0f, 0.0f), ShapeType::BOX, params),
        IRMath::SDF::kSurfaceThreshold
    );
}

TEST(SdfEvaluate, SphereBoundaryMatchesRadius) {
    EXPECT_NEAR(
        IRMath::SDF::evaluate(
            IRMath::vec3(4.0f, 0.0f, 0.0f),
            ShapeType::SPHERE,
            IRMath::vec4(4.0f, 4.0f, 4.0f, 0.0f)
        ),
        0.0f,
        kTolerance
    );
}

TEST(SdfBoundingHalf, TorusIncludesMajorAndMinorRadius) {
    const IRMath::vec3 half =
        IRMath::SDF::boundingHalf(ShapeType::TORUS, IRMath::vec4(4.0f, 2.0f, 0.0f, 0.0f));

    EXPECT_NEAR(half.x, 6.0f, kTolerance);
    EXPECT_NEAR(half.y, 6.0f, kTolerance);
    EXPECT_NEAR(half.z, 2.0f, kTolerance);
}

TEST(SdfEvaluate, TaperedBoxKeepsBaseWiderThanTop) {
    const IRMath::vec4 params{6.0f, 6.0f, 6.0f, 0.5f};
    EXPECT_LE(
        IRMath::SDF::evaluate(IRMath::vec3(2.5f, 0.0f, -2.5f), ShapeType::TAPERED_BOX, params),
        IRMath::SDF::kSurfaceThreshold
    );
    EXPECT_GT(
        IRMath::SDF::evaluate(IRMath::vec3(2.5f, 0.0f, 2.5f), ShapeType::TAPERED_BOX, params),
        IRMath::SDF::kSurfaceThreshold
    );
}

} // namespace

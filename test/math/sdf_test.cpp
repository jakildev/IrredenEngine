#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>

#include <span>
#include <vector>

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

TEST(SdfEvaluateGrid, SphereMatchesPerCellEvaluate) {
    const IRMath::ivec3 size{8, 8, 8};
    const IRMath::vec4 params{4.0f, 4.0f, 4.0f, 0.0f};
    std::vector<float> grid(
        static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y) *
        static_cast<std::size_t>(size.z)
    );
    IRMath::SDF::evaluateGrid(size, ShapeType::SPHERE, params, grid);

    const IRMath::vec3 center = IRMath::vec3(size) * 0.5f;
    for (int z = 0; z < size.z; ++z) {
        for (int y = 0; y < size.y; ++y) {
            for (int x = 0; x < size.x; ++x) {
                const IRMath::vec3 sdfPos = IRMath::vec3(x, y, z) - center + IRMath::vec3(0.5f);
                const float expected = IRMath::SDF::evaluate(sdfPos, ShapeType::SPHERE, params);
                const std::size_t flat = static_cast<std::size_t>(
                    IRMath::index3DtoIndex1D(IRMath::ivec3{x, y, z}, size)
                );
                EXPECT_NEAR(grid[flat], expected, kTolerance);
            }
        }
    }
}

TEST(SdfEvaluateGrid, BoxInteriorBelowSurfaceThreshold) {
    const IRMath::ivec3 size{6, 6, 6};
    const IRMath::vec4 params =
        IRMath::SDF::effectiveParams(ShapeType::BOX, IRMath::vec4(6.0f, 6.0f, 6.0f, 0.0f));
    std::vector<float> grid(
        static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y) *
        static_cast<std::size_t>(size.z)
    );
    IRMath::SDF::evaluateGrid(size, ShapeType::BOX, params, grid);

    const IRMath::ivec3 centerCell{size.x / 2, size.y / 2, size.z / 2};
    const std::size_t centerFlat =
        static_cast<std::size_t>(IRMath::index3DtoIndex1D(centerCell, size));
    EXPECT_LE(grid[centerFlat], IRMath::SDF::kSurfaceThreshold);

    int filledCount = 0;
    for (float d : grid) {
        if (d <= IRMath::SDF::kSurfaceThreshold)
            ++filledCount;
    }
    EXPECT_GT(filledCount, 0);
    EXPECT_LE(static_cast<std::size_t>(filledCount), grid.size());
}

TEST(SdfEvaluateGrid, UndersizedSpanIsNoop) {
    const IRMath::ivec3 size{4, 4, 4};
    std::vector<float> grid(8, 99.0f);
    IRMath::SDF::evaluateGrid(size, ShapeType::SPHERE, IRMath::vec4(2.0f, 2.0f, 2.0f, 0.0f), grid);
    for (float d : grid) {
        EXPECT_FLOAT_EQ(d, 99.0f);
    }
}

} // namespace

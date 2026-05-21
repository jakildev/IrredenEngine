#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <irreden/ir_math.hpp>

#include <array>

namespace {

// Query points: a 4×4×4 block of positions in [-1.5, 1.5]^3, covering
// surface, interior, and exterior regions for each SDF primitive. The
// spread ensures branch paths in taperedBox and curvedPanel are exercised.
constexpr int kN = 64;
constexpr std::array<IRMath::vec3, kN> kQueryPts = [] {
    std::array<IRMath::vec3, kN> pts{};
    int idx = 0;
    for (int zi = 0; zi < 4; ++zi)
        for (int yi = 0; yi < 4; ++yi)
            for (int xi = 0; xi < 4; ++xi)
                pts[idx++] = IRMath::vec3(
                    -1.5f + xi * 1.0f,
                    -1.5f + yi * 1.0f,
                    -1.5f + zi * 1.0f
                );
    return pts;
}();

constexpr IRMath::vec3 kHalf{3.0f, 3.0f, 3.0f};
constexpr IRMath::vec4 kParams{6.0f, 6.0f, 6.0f, 0.0f};
constexpr IRMath::vec4 kParamsTapered{6.0f, 6.0f, 6.0f, 0.5f};
constexpr IRMath::vec4 kParamsCurved{6.0f, 6.0f, 2.0f, 0.3f};
constexpr IRMath::vec4 kParamsTorus{4.0f, 1.5f, 0.0f, 0.0f};

} // namespace

TEST_CASE("SDF primitive throughput") {
    BENCHMARK("box x64") {
        float acc = 0.0f;
        for (const auto& p : kQueryPts)
            acc += IRMath::SDF::box(p, kHalf);
        return acc;
    };

    BENCHMARK("sphere x64") {
        float acc = 0.0f;
        for (const auto& p : kQueryPts)
            acc += IRMath::SDF::sphere(p, 3.0f);
        return acc;
    };

    BENCHMARK("cylinder x64") {
        float acc = 0.0f;
        for (const auto& p : kQueryPts)
            acc += IRMath::SDF::cylinder(p, 2.0f, 3.0f);
        return acc;
    };

    BENCHMARK("ellipsoid x64") {
        float acc = 0.0f;
        for (const auto& p : kQueryPts)
            acc += IRMath::SDF::ellipsoid(p, kHalf);
        return acc;
    };

    BENCHMARK("taperedBox x64") {
        float acc = 0.0f;
        for (const auto& p : kQueryPts)
            acc += IRMath::SDF::taperedBox(p, kHalf, 0.5f);
        return acc;
    };

    BENCHMARK("cone x64") {
        float acc = 0.0f;
        for (const auto& p : kQueryPts)
            acc += IRMath::SDF::cone(p, 3.0f, 3.0f);
        return acc;
    };

    BENCHMARK("torus x64") {
        float acc = 0.0f;
        for (const auto& p : kQueryPts)
            acc += IRMath::SDF::torus(p, 4.0f, 1.5f);
        return acc;
    };

    BENCHMARK("wedge x64") {
        float acc = 0.0f;
        for (const auto& p : kQueryPts)
            acc += IRMath::SDF::wedge(p, kHalf);
        return acc;
    };

    BENCHMARK("curvedPanel x64") {
        float acc = 0.0f;
        for (const auto& p : kQueryPts)
            acc += IRMath::SDF::curvedPanel(p, kHalf, 0.3f);
        return acc;
    };
}

TEST_CASE("SDF evaluate dispatcher") {
    using IRMath::SDF::ShapeType;

    BENCHMARK("evaluate BOX x64") {
        float acc = 0.0f;
        for (const auto& p : kQueryPts)
            acc += IRMath::SDF::evaluate(p, ShapeType::BOX, kParams);
        return acc;
    };

    BENCHMARK("evaluate SPHERE x64") {
        float acc = 0.0f;
        for (const auto& p : kQueryPts)
            acc += IRMath::SDF::evaluate(p, ShapeType::SPHERE, kParams);
        return acc;
    };

    BENCHMARK("evaluate TAPERED_BOX x64") {
        float acc = 0.0f;
        for (const auto& p : kQueryPts)
            acc += IRMath::SDF::evaluate(p, ShapeType::TAPERED_BOX, kParamsTapered);
        return acc;
    };

    BENCHMARK("evaluate TORUS x64") {
        float acc = 0.0f;
        for (const auto& p : kQueryPts)
            acc += IRMath::SDF::evaluate(p, ShapeType::TORUS, kParamsTorus);
        return acc;
    };

    BENCHMARK("evaluate CURVED_PANEL x64") {
        float acc = 0.0f;
        for (const auto& p : kQueryPts)
            acc += IRMath::SDF::evaluate(p, ShapeType::CURVED_PANEL, kParamsCurved);
        return acc;
    };
}

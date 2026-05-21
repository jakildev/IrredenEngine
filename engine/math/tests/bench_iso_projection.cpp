#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <irreden/ir_math.hpp>

#include <array>

namespace {

// Representative voxel grid coordinates: a 16×16×8 slab centred near the
// origin, covering typical shapes placed at world-space positions the
// editor and game creation demos actually use.
constexpr int kN = 256;
constexpr std::array<IRMath::ivec3, kN> kPoints = [] {
    std::array<IRMath::ivec3, kN> pts{};
    int idx = 0;
    for (int z = 0; z < 8; ++z)
        for (int y = -16; y < 16; ++y)
            for (int x = -16; x < 0; ++x) {
                if (idx < kN)
                    pts[idx++] = IRMath::ivec3(x, y, z);
            }
    return pts;
}();

constexpr std::array<IRMath::vec3, kN> kPointsF = [] {
    std::array<IRMath::vec3, kN> pts{};
    for (int i = 0; i < kN; ++i)
        pts[i] = IRMath::vec3(kPoints[i]);
    return pts;
}();

} // namespace

TEST_CASE("iso projection throughput") {
    BENCHMARK("pos3DtoPos2DIso(ivec3) x256") {
        IRMath::ivec2 acc{0, 0};
        for (const auto& p : kPoints)
            acc += IRMath::pos3DtoPos2DIso(p);
        return acc;
    };

    BENCHMARK("pos3DtoPos2DIso(vec3) x256") {
        IRMath::vec2 acc{0.0f, 0.0f};
        for (const auto& p : kPointsF)
            acc += IRMath::pos3DtoPos2DIso(p);
        return acc;
    };

    BENCHMARK("pos3DtoDistance(ivec3) x256") {
        IRMath::Distance acc = 0;
        for (const auto& p : kPoints)
            acc += IRMath::pos3DtoDistance(p);
        return acc;
    };

    BENCHMARK("isoDepthShift x256") {
        IRMath::vec3 acc{0.0f};
        for (int i = 0; i < kN; ++i)
            acc += IRMath::isoDepthShift(kPointsF[i], float(i & 7));
        return acc;
    };
}

TEST_CASE("iso projection single-call latency") {
    BENCHMARK("pos3DtoPos2DIso(ivec3) one call") {
        return IRMath::pos3DtoPos2DIso(IRMath::ivec3(10, 20, 30));
    };

    BENCHMARK("pos3DtoPos2DIso(vec3) one call") {
        return IRMath::pos3DtoPos2DIso(IRMath::vec3(10.5f, 20.5f, 30.5f));
    };

    BENCHMARK("pos3DtoDistance(ivec3) one call") {
        return IRMath::pos3DtoDistance(IRMath::ivec3(10, 20, 30));
    };

    BENCHMARK("pos3DtoDistance(vec3) one call") {
        return IRMath::pos3DtoDistance(IRMath::vec3(10.5f, 20.5f, 30.5f));
    };
}

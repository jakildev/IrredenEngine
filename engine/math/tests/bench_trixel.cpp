#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <irreden/ir_math.hpp>

#include <vector>

namespace {

// Canvas size representative of the IRShapeDebug 64-grid mode.
constexpr IRMath::ivec2 kCanvasSize{256, 256};

} // namespace

TEST_CASE("deformedTrixelIsoPixel throughput") {
    // Runtime-initialized inputs prevent the compiler from constant-folding
    // across the 24 (face, subPixel, yaw) combinations. constexpr inputs
    // with a small iteration count are fully foldable; std::vector breaks that.
    const std::vector<int> faces{
        IRMath::kXFace, IRMath::kXFace,
        IRMath::kYFace, IRMath::kYFace,
        IRMath::kZFace, IRMath::kZFace,
    };
    const std::vector<int> subPixels{0, 1, 0, 1, 0, 1};
    const std::vector<float> yawSamples{0.0f, 0.25f, -0.25f, 0.1f};

    BENCHMARK("deformedTrixelIsoPixel all faces x4 yaws") {
        IRMath::ivec2 acc{0, 0};
        for (float yaw : yawSamples)
            for (std::size_t i = 0; i < faces.size(); ++i)
                acc += IRMath::deformedTrixelIsoPixel(faces[i], subPixels[i], yaw);
        return acc;
    };

    BENCHMARK("deformedTrixelIsoPixel yaw=0 all faces") {
        IRMath::ivec2 acc{0, 0};
        const float yaw = yawSamples[0];
        for (std::size_t i = 0; i < faces.size(); ++i)
            acc += IRMath::deformedTrixelIsoPixel(faces[i], subPixels[i], yaw);
        return acc;
    };

    BENCHMARK("deformedTrixelIsoPixel yaw=0.25 all faces") {
        IRMath::ivec2 acc{0, 0};
        const float yaw = yawSamples[1];
        for (std::size_t i = 0; i < faces.size(); ++i)
            acc += IRMath::deformedTrixelIsoPixel(faces[i], subPixels[i], yaw);
        return acc;
    };
}

TEST_CASE("trixel origin offsets") {
    // Each offset helper is called once per face-slot per canvas resize.
    // These are constexpr so the timing reflects the call-site overhead
    // at non-constexpr call sites (i.e., when canvasSize is not compile-time).
    const IRMath::ivec2 size = kCanvasSize;

    BENCHMARK("all 12 trixelOriginOffset helpers") {
        IRMath::ivec2 acc{0, 0};
        acc += IRMath::trixelOriginOffsetX1(size);
        acc += IRMath::trixelOriginOffsetX2(size);
        acc += IRMath::trixelOriginOffsetX3(size);
        acc += IRMath::trixelOriginOffsetX4(size);
        acc += IRMath::trixelOriginOffsetY1(size);
        acc += IRMath::trixelOriginOffsetY2(size);
        acc += IRMath::trixelOriginOffsetY3(size);
        acc += IRMath::trixelOriginOffsetY4(size);
        acc += IRMath::trixelOriginOffsetZ1(size);
        acc += IRMath::trixelOriginOffsetZ2(size);
        return acc;
    };
}

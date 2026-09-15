#include <gtest/gtest.h>

#include <irreden/math/edt.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <random>
#include <span>
#include <vector>

namespace {

std::vector<std::int64_t> bruteForce(std::span<const std::int64_t> input) {
    std::vector<std::int64_t> output(input.size(), IRMath::kSquaredEdtInfinity);
    for (std::size_t p = 0; p < input.size(); ++p) {
        for (std::size_t q = 0; q < input.size(); ++q) {
            if (input[q] == IRMath::kSquaredEdtInfinity) {
                continue;
            }
            const std::int64_t delta = static_cast<std::int64_t>(p) - static_cast<std::int64_t>(q);
            output[p] = std::min(output[p], input[q] + delta * delta);
        }
    }
    return output;
}

void expectTransform(std::span<const std::int64_t> input) {
    std::vector<std::int64_t> output(input.size());
    IRMath::SquaredEdtScratch scratch;
    IRMath::squaredDistanceTransform1D(input, output, scratch);
    EXPECT_EQ(output, bruteForce(input));
}

TEST(SquaredEdtKernelTest, CoversEmptySingletonFreeOccupiedWeightedAndTies) {
    expectTransform({});
    const std::array singleton{std::int64_t{0}};
    const std::array allFree{
        IRMath::kSquaredEdtInfinity,
        IRMath::kSquaredEdtInfinity,
        IRMath::kSquaredEdtInfinity,
    };
    const std::array allOccupied{std::int64_t{0}, std::int64_t{0}, std::int64_t{0}};
    const std::array tie{
        std::int64_t{0},
        IRMath::kSquaredEdtInfinity,
        std::int64_t{0},
    };
    const std::array weighted{
        std::int64_t{7},
        IRMath::kSquaredEdtInfinity,
        std::int64_t{2},
        std::int64_t{9},
    };
    expectTransform(singleton);
    expectTransform(allFree);
    expectTransform(allOccupied);
    expectTransform(tie);
    expectTransform(weighted);
}

TEST(SquaredEdtKernelTest, SeededRandomSpansMatchIndependentOracle) {
    std::mt19937 rng(0xED73161u);
    std::uniform_int_distribution<int> lengthDistribution(1, 96);
    std::uniform_int_distribution<int> kindDistribution(0, 4);
    std::uniform_int_distribution<int> costDistribution(0, 1000);
    bool observedInfinity = false;
    bool observedWeighted = false;

    for (int iteration = 0; iteration < 500; ++iteration) {
        std::vector<std::int64_t> input(lengthDistribution(rng));
        for (std::int64_t &cost : input) {
            if (kindDistribution(rng) == 0) {
                cost = IRMath::kSquaredEdtInfinity;
                observedInfinity = true;
            } else {
                cost = costDistribution(rng);
                observedWeighted |= cost != 0;
            }
        }
        expectTransform(input);
    }
    EXPECT_TRUE(observedInfinity);
    EXPECT_TRUE(observedWeighted);
}

TEST(SquaredEdtKernelTest, LongScanlineKeepsSquaredDistanceWiderThanInt32) {
    std::vector<std::int64_t> input(50001, IRMath::kSquaredEdtInfinity);
    std::vector<std::int64_t> output(input.size());
    IRMath::SquaredEdtScratch scratch;
    input.front() = 0;
    IRMath::squaredDistanceTransform1D(input, output, scratch);
    EXPECT_EQ(output.back(), INT64_C(2500000000));
}

TEST(SquaredEdtKernelTest, ValidatesCostsSizesAndNonOverlappingStorage) {
    EXPECT_NO_THROW(
        IRMath::detail::validateSquaredEdtSize(
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())
        )
    );
    EXPECT_THROW(
        IRMath::detail::validateSquaredEdtSize(
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) + 1
        ),
        std::invalid_argument
    );

    IRMath::SquaredEdtScratch scratch;
    std::array<std::int64_t, 2> input{IRMath::kSquaredEdtMaxFiniteCost, 0};
    std::array<std::int64_t, 2> output{};
    EXPECT_NO_THROW(IRMath::squaredDistanceTransform1D(input, output, scratch));
    input[0] = IRMath::kSquaredEdtMaxFiniteCost + 1;
    EXPECT_THROW(IRMath::squaredDistanceTransform1D(input, output, scratch), std::invalid_argument);
    input[0] = -1;
    EXPECT_THROW(IRMath::squaredDistanceTransform1D(input, output, scratch), std::invalid_argument);
    input[0] = 0;
    EXPECT_THROW(IRMath::squaredDistanceTransform1D(input, input, scratch), std::invalid_argument);
    EXPECT_THROW(
        IRMath::squaredDistanceTransform1D(
            std::span<const std::int64_t>{input},
            std::span<std::int64_t>{output}.first(1),
            scratch
        ),
        std::invalid_argument
    );
}

} // namespace

#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {

// Independent transcription of O'Neill's pcg32_srandom_r / pcg32_random_r.
struct ReferencePcg32 {
    std::uint64_t state_ = 0;
    std::uint64_t inc_ = 0;

    ReferencePcg32(std::uint64_t initstate, std::uint64_t initseq) {
        state_ = 0u;
        inc_ = (initseq << 1u) | 1u;
        next();
        state_ += initstate;
        next();
    }

    std::uint32_t next() {
        const std::uint64_t oldstate = state_;
        state_ = oldstate * 6364136223846793005ULL + inc_;
        const auto xorshifted = static_cast<std::uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
        const auto rot = static_cast<std::uint32_t>(oldstate >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
    }
};

TEST(Pcg32Test, ReproducesPublishedDemoOutputForSeed42Stream54) {
    IRMath::Pcg32 rng(42, 54);
    const std::array<std::uint32_t, 6> expected{
        0xa15c02b7u,
        0x7b47f409u,
        0xba1d3330u,
        0x83d2f293u,
        0xbfa4784bu,
        0xcbed606eu,
    };
    for (std::uint32_t word : expected) {
        EXPECT_EQ(rng.nextWord(), word);
    }
}

TEST(Pcg32Test, MatchesReferenceTranscriptionOverFirstKiloword) {
    for (std::uint64_t seed : {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{12345}}) {
        IRMath::Pcg32 rng(seed);
        ReferencePcg32 reference(seed, 0);
        for (int i = 0; i < 1024; ++i) {
            ASSERT_EQ(rng.nextWord(), reference.next()) << "seed " << seed << " word " << i;
        }
    }
}

TEST(Pcg32Test, FirstWordsOfSeed12345ArePinned) {
    IRMath::Pcg32 rng(12345);
    const std::array<std::uint32_t, 8> expected{
        0x1220b391u,
        0x98d38aaau,
        0x5bbddfa6u,
        0x871ffa62u,
        0x132f296fu,
        0x01c7e422u,
        0xfccc2fb2u,
        0x1f850317u,
    };
    for (std::uint32_t word : expected) {
        EXPECT_EQ(rng.nextWord(), word);
    }
}

TEST(Pcg32Test, UniformBelowIsMultiplyShiftAndNotModulo) {
    for (std::uint32_t n : {1u, 2u, 4097u, 65536u}) {
        IRMath::Pcg32 rng(777);
        IRMath::Pcg32 shadow(777);
        bool differsFromModulo = false;
        for (int i = 0; i < 4096; ++i) {
            const std::uint32_t word = shadow.nextWord();
            const std::uint32_t value = IRMath::uniformBelow(rng, n);
            ASSERT_LT(value, n);
            ASSERT_EQ(
                value,
                static_cast<std::uint32_t>((static_cast<std::uint64_t>(word) * n) >> 32u)
            );
            differsFromModulo = differsFromModulo || value != word % n;
        }
        if (n > 1) {
            EXPECT_TRUE(differsFromModulo) << "n " << n;
        }
    }
}

TEST(IsqrtTest, IsExactOverTwentyBitsAndAtInt64Max) {
    for (std::int64_t x = 0; x <= (std::int64_t{1} << 20); ++x) {
        const std::int64_t root = IRMath::isqrt(x);
        ASSERT_LE(root * root, x) << x;
        ASSERT_LT(x, (root + 1) * (root + 1)) << x;
    }
    EXPECT_EQ(IRMath::isqrt(std::numeric_limits<std::int64_t>::max()), 3037000499);
    EXPECT_THROW(IRMath::isqrt(-1), std::invalid_argument);
}

TEST(FloorDivTest, FloorsTowardNegativeInfinityUnlikeTruncation) {
    EXPECT_EQ(IRMath::floorDiv(-1, 3), -1);
    EXPECT_EQ(IRMath::floorDiv(-3, 3), -1);
    EXPECT_EQ(IRMath::floorDiv(-4, 3), -2);
    EXPECT_EQ(IRMath::floorDiv(0, 3), 0);
    EXPECT_EQ(IRMath::floorDiv(4, 3), 1);
    EXPECT_NE(IRMath::floorDiv(-1, 3), std::int64_t{-1} / 3);
    EXPECT_NE(IRMath::floorDiv(-4, 3), std::int64_t{-4} / 3);
    EXPECT_THROW(IRMath::floorDiv(1, 0), std::invalid_argument);
    EXPECT_THROW(IRMath::floorDiv(1, -3), std::invalid_argument);
}

} // namespace

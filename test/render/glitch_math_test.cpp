// Shader-free contract of the glitch passes: the tick clock, the integer hash
// ABI shared with ir_glitch_common.{glsl,metal}, the per-pixel effect plan the
// shaders mirror, the state validation, and the frame plan that decides
// whether FRAMEBUFFER_GLITCH copies history and draws. Expected hash values
// were computed independently of this code from the documented algorithm.

#include <gtest/gtest.h>

#include <irreden/render/glitch.hpp>
#include <irreden/render/glitch_math.hpp>
#include <irreden/render/systems/system_framebuffer_glitch.hpp>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

namespace {

using IRComponents::C_GlitchState;
using IRMath::ivec2;
namespace Glitch = IRPrefab::Glitch;

constexpr std::int64_t kInt64Min = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();

TEST(GlitchMathTest, HashMatchesFixedVectorsAtOverflowEdges) {
    EXPECT_EQ(Glitch::hash3(1u, 2u, 3u), 0xEEE06998u);
    EXPECT_EQ(Glitch::glitchHash(0u, Glitch::toTickWords(0), 0u, 0u), 0x00000000u);
    EXPECT_EQ(Glitch::glitchHash(1u, Glitch::toTickWords(1), 0u, 0u), 0xED8F3A39u);
    EXPECT_EQ(
        Glitch::glitchHash(0xFFFFFFFFu, Glitch::toTickWords(-1), Glitch::kEffectDepth, 83u),
        0x2E788937u
    );
    EXPECT_EQ(
        Glitch::glitchHash(12345u, Glitch::toTickWords(kInt64Min), Glitch::kEffectHold, 16u),
        0x978C2824u
    );
    EXPECT_EQ(
        Glitch::glitchHash(7u, Glitch::toTickWords(kInt64Max), Glitch::kEffectBars, 65u),
        0xA34629EAu
    );
    EXPECT_EQ(
        Glitch::glitchHash(42u, Glitch::toTickWords(-2), Glitch::kEffectCells, 33u),
        0x5D912BEEu
    );
    // Adjacent lanes of one effect and tick draw independent values.
    EXPECT_EQ(
        Glitch::glitchHash(42u, Glitch::toTickWords(-2), Glitch::kEffectCells, 32u),
        0xC6264D3Bu
    );
}

TEST(GlitchMathTest, TickWordsAreTwosComplementHalves) {
    const Glitch::TickWords minusOne = Glitch::toTickWords(-1);
    EXPECT_EQ(minusOne.low_, 0xFFFFFFFFu);
    EXPECT_EQ(minusOne.high_, 0xFFFFFFFFu);
    const Glitch::TickWords minimum = Glitch::toTickWords(kInt64Min);
    EXPECT_EQ(minimum.low_, 0u);
    EXPECT_EQ(minimum.high_, 0x80000000u);
    const Glitch::TickWords maximum = Glitch::toTickWords(kInt64Max);
    EXPECT_EQ(maximum.low_, 0xFFFFFFFFu);
    EXPECT_EQ(maximum.high_, 0x7FFFFFFFu);
    for (std::int64_t tick :
         {kInt64Min,
          std::int64_t{-4294967296},
          std::int64_t{-1},
          std::int64_t{0},
          std::int64_t{1},
          std::int64_t{4294967296},
          kInt64Max}) {
        EXPECT_EQ(Glitch::fromTickWords(Glitch::toTickWords(tick)), tick) << tick;
    }
}

TEST(GlitchMathTest, TickIndexFloorsAcrossPhaseSigns) {
    // Positive phase: ticks start at the phase, earlier times are negative.
    EXPECT_EQ(Glitch::tickIndex(0.25, 0.5, 0.5), -1);
    EXPECT_EQ(Glitch::tickIndex(0.5, 0.5, 0.5), 0);
    EXPECT_EQ(Glitch::tickIndex(1.75, 0.5, 0.5), 2);
    // Negative phase shifts the boundaries earlier.
    EXPECT_EQ(Glitch::tickIndex(0.0, 0.5, -0.25), 0);
    EXPECT_EQ(Glitch::tickIndex(0.3, 0.5, -0.25), 1);
    EXPECT_EQ(Glitch::tickIndex(-0.3, 0.5, -0.25), -1);
}

TEST(GlitchMathTest, TickIndexChangesExactlyOnTheBoundary) {
    // Period 0.25 and phase 0.5 are exact in binary, so the boundary is exact.
    const double boundary = 0.5 + 3.0 * 0.25;
    EXPECT_EQ(Glitch::tickIndex(boundary, 0.25, 0.5), 3);
    EXPECT_EQ(Glitch::tickIndex(std::nextafter(boundary, 0.0), 0.25, 0.5), 2);
    // Between boundaries nothing moves.
    EXPECT_EQ(Glitch::tickIndex(boundary + 0.1, 0.25, 0.5), 3);
    EXPECT_EQ(Glitch::tickIndex(boundary + 0.24, 0.25, 0.5), 3);
}

TEST(GlitchMathTest, HoldDivisorTwoTicksTwiceAPeriod) {
    C_GlitchState state;
    state.periodSeconds_ = 1.0;
    state.holdDivisor_ = 2;
    const Glitch::Clock early = Glitch::clockAt(state, 0.25);
    const Glitch::Clock late = Glitch::clockAt(state, 0.75);
    EXPECT_EQ(early.tick_, 0);
    EXPECT_EQ(late.tick_, 0);
    EXPECT_EQ(early.holdTick_, 0);
    EXPECT_EQ(late.holdTick_, 1);
    EXPECT_EQ(Glitch::clockAt(state, 1.25).holdTick_, 2);
}

TEST(GlitchMathTest, BarEnvelopeFallsWithinAPeriod) {
    C_GlitchState state;
    state.periodSeconds_ = 1.0;
    EXPECT_FLOAT_EQ(Glitch::clockAt(state, 2.0).barEnvelope_, 1.0f);
    EXPECT_FLOAT_EQ(Glitch::clockAt(state, 2.25).barEnvelope_, 0.75f);
}

TEST(GlitchMathTest, TickIndexSaturatesInsteadOfOverflowing) {
    EXPECT_EQ(Glitch::tickIndex(1.0, 1e-300, 0.0), kInt64Max);
    EXPECT_EQ(Glitch::tickIndex(-1.0, 1e-300, 0.0), kInt64Min);
    EXPECT_FALSE(Glitch::isRepresentableTick(Glitch::tickQuotient(1.0, 1e-300, 0.0)));
    EXPECT_TRUE(Glitch::isRepresentableTick(-9223372036854775808.0));
    EXPECT_FALSE(Glitch::isRepresentableTick(9223372036854775808.0));
}

TEST(GlitchMathTest, DefaultStateIsOffAndValid) {
    const C_GlitchState state;
    EXPECT_FLOAT_EQ(state.intensity_, 0.0f);
    EXPECT_DOUBLE_EQ(state.periodSeconds_, 1.0);
    EXPECT_DOUBLE_EQ(state.phaseSeconds_, 0.0);
    EXPECT_EQ(state.seed_, 0u);
    EXPECT_EQ(state.holdDivisor_, 1);
    EXPECT_FALSE(Glitch::isEnabled(state));
    EXPECT_EQ(Glitch::validate(state, 0.0), "");
}

TEST(GlitchMathTest, EnabledNeedsIntensityAndAWeight) {
    C_GlitchState state;
    state.intensity_ = 1.0f;
    EXPECT_FALSE(Glitch::isEnabled(state));
    state.depthWeight_ = 0.5f;
    EXPECT_TRUE(Glitch::isEnabled(state));
    state.intensity_ = 0.0f;
    EXPECT_FALSE(Glitch::isEnabled(state));
}

TEST(GlitchMathTest, ValidateRejectsEveryUnrepresentableState) {
    const auto rejects = [](auto mutate, double time = 0.0) {
        C_GlitchState state;
        mutate(state);
        return !Glitch::validate(state, time).empty();
    };
    EXPECT_TRUE(rejects([](C_GlitchState &s) { s.intensity_ = 1.5f; }));
    EXPECT_TRUE(rejects([](C_GlitchState &s) { s.tearWeight_ = -0.1f; }));
    EXPECT_TRUE(rejects([](C_GlitchState &s) {
        s.cellsWeight_ = std::numeric_limits<float>::quiet_NaN();
    }));
    EXPECT_TRUE(rejects([](C_GlitchState &s) { s.periodSeconds_ = 0.0; }));
    EXPECT_TRUE(rejects([](C_GlitchState &s) {
        s.periodSeconds_ = std::numeric_limits<double>::infinity();
    }));
    EXPECT_TRUE(rejects([](C_GlitchState &s) { s.holdDivisor_ = 0; }));
    // Every scalar is finite and in range, but the derived math is not.
    EXPECT_TRUE(rejects([](C_GlitchState &s) {
        s.periodSeconds_ = std::numeric_limits<double>::denorm_min();
        s.holdDivisor_ = 2;
    }));
    EXPECT_TRUE(rejects([](C_GlitchState &s) { s.phaseSeconds_ = 1e300; }));
    EXPECT_TRUE(rejects([](C_GlitchState &s) { s.periodSeconds_ = 1e-300; }, 1.0));
    EXPECT_TRUE(rejects(
        [](C_GlitchState &s) {
            s.periodSeconds_ = 1e-9;
            s.holdDivisor_ = std::numeric_limits<int>::max();
        },
        1e6
    ));
    // A large but representable divisor is fine.
    EXPECT_FALSE(rejects([](C_GlitchState &s) { s.holdDivisor_ = 1 << 20; }, 100.0));
}

// ---- per-pixel plan ---------------------------------------------------------

constexpr ivec2 kSize{640, 360};
constexpr ivec2 kTrixelStep{8, 4};

Glitch::FrameParams onlyEffect(float C_GlitchState::*weight, std::uint32_t seed = 3u) {
    C_GlitchState state;
    state.seed_ = seed;
    state.intensity_ = 1.0f;
    state.*weight = 1.0f;
    return Glitch::frameParams(state, Glitch::clockAt(state, 0.0), kTrixelStep, kSize);
}

float backgroundDepth(ivec2) {
    return 1.0f;
}

TEST(GlitchMathTest, TearMovesWholeTrixelsInsideItsBandsOnly) {
    const Glitch::FrameParams params = onlyEffect(&C_GlitchState::tearWeight_);
    int movedRows = 0;
    int stillRows = 0;
    for (int y = 0; y < kSize.y; y += kTrixelStep.y) {
        const ivec2 pixel{320, y};
        const Glitch::SamplePlan plan = Glitch::samplePlan(params, pixel, backgroundDepth);
        const int dx = plan.source_.x - pixel.x;
        EXPECT_EQ(plan.source_.y, pixel.y);
        EXPECT_EQ(dx % kTrixelStep.x, 0) << "row " << y;
        EXPECT_FALSE(plan.holdBand_);
        EXPECT_EQ(plan.splitOffsetPx_, 0);
        (dx != 0 ? movedRows : stillRows) += 1;
    }
    EXPECT_GT(movedRows, 0);
    EXPECT_GT(stillRows, 0);
}

TEST(GlitchMathTest, CellsMoveWholeCellsToANeighbour) {
    const Glitch::FrameParams params = onlyEffect(&C_GlitchState::cellsWeight_);
    const ivec2 cellPx = kTrixelStep * Glitch::cellTrixels(params.seed_, params.tick_);
    int moved = 0;
    // Interior cells only: a move off the framebuffer edge is clamped.
    for (int y = cellPx.y; y + 2 * cellPx.y <= kSize.y; y += cellPx.y) {
        for (int x = cellPx.x; x + 2 * cellPx.x <= kSize.x; x += cellPx.x) {
            const ivec2 pixel{x, y};
            const ivec2 delta = Glitch::samplePlan(params, pixel, backgroundDepth).source_ - pixel;
            if (delta == ivec2(0)) {
                continue;
            }
            ++moved;
            EXPECT_EQ(delta.x % cellPx.x, 0);
            EXPECT_EQ(delta.y % cellPx.y, 0);
            EXPECT_LE(IRMath::abs(delta.x / cellPx.x), 1);
            EXPECT_LE(IRMath::abs(delta.y / cellPx.y), 1);
        }
    }
    EXPECT_GT(moved, 0);
}

TEST(GlitchMathTest, SplitOffsetsRedAndBlueByWholeTrixels) {
    const Glitch::FrameParams params = onlyEffect(&C_GlitchState::splitWeight_);
    const Glitch::SamplePlan plan = Glitch::samplePlan(params, ivec2(100, 100), backgroundDepth);
    EXPECT_NE(plan.splitOffsetPx_, 0);
    EXPECT_EQ(plan.splitOffsetPx_ % kTrixelStep.x, 0);
    EXPECT_EQ(plan.source_, ivec2(100, 100));
}

TEST(GlitchMathTest, BarsAddOnlyOnTheirRows) {
    const Glitch::FrameParams params = onlyEffect(&C_GlitchState::barsWeight_);
    int barRows = 0;
    for (int y = 0; y < kSize.y; y += kTrixelStep.y) {
        const Glitch::SamplePlan plan = Glitch::samplePlan(params, ivec2(5, y), backgroundDepth);
        barRows += plan.barAdd_ != IRMath::vec3(0.0f) ? 1 : 0;
        EXPECT_EQ(plan.source_, ivec2(5, y));
    }
    EXPECT_GE(barRows, 1);
    EXPECT_LE(barRows, Glitch::kMaxBars);
}

TEST(GlitchMathTest, HoldSelectsBandsWithoutMovingTheSource) {
    const Glitch::FrameParams params = onlyEffect(&C_GlitchState::holdWeight_);
    int held = 0;
    for (int y = 0; y < kSize.y; y += kTrixelStep.y) {
        const Glitch::SamplePlan plan = Glitch::samplePlan(params, ivec2(7, y), backgroundDepth);
        held += plan.holdBand_ ? 1 : 0;
        EXPECT_EQ(plan.source_, ivec2(7, y));
    }
    EXPECT_GT(held, 0);
}

// A box of foreground depth over a background, for the depth lane.
float boxDepth(ivec2 pixel) {
    return pixel.x >= 200 && pixel.x < 440 && pixel.y >= 100 && pixel.y < 260 ? 0.5f : 1.0f;
}

std::uint32_t seedWithDepthMode(Glitch::DepthMode mode) {
    for (std::uint32_t seed = 0u; seed < 64u; ++seed) {
        if (Glitch::depthMode(seed, Glitch::toTickWords(0)) == mode) {
            return seed;
        }
    }
    ADD_FAILURE() << "no seed below 64 selects the requested depth mode";
    return 0u;
}

TEST(GlitchMathTest, DepthTearShiftsOnlyPixelsInsideTheSelectedInterval) {
    const Glitch::FrameParams params =
        onlyEffect(&C_GlitchState::depthWeight_, seedWithDepthMode(Glitch::DepthMode::TEAR));
    const Glitch::DepthInterval interval = Glitch::selectDepthInterval(params, boxDepth);
    ASSERT_TRUE(interval.valid_);
    EXPECT_FLOAT_EQ(interval.center_, 0.5f);

    const Glitch::SamplePlan inside = Glitch::samplePlan(params, ivec2(300, 150), boxDepth);
    EXPECT_NE(inside.source_.x, 300);
    EXPECT_EQ((inside.source_.x - 300) % kTrixelStep.x, 0);
    EXPECT_FLOAT_EQ(inside.depthHoldBlend_, 0.0f);
    // Rows other than the first carry the same shift: selection is by depth.
    EXPECT_EQ(
        Glitch::samplePlan(params, ivec2(300, 220), boxDepth).source_.x - 300,
        inside.source_.x - 300
    );

    const Glitch::SamplePlan outside = Glitch::samplePlan(params, ivec2(50, 150), boxDepth);
    EXPECT_EQ(outside.source_, ivec2(50, 150));
}

TEST(GlitchMathTest, DepthHoldBlendsOnlyPixelsInsideTheSelectedInterval) {
    const Glitch::FrameParams params =
        onlyEffect(&C_GlitchState::depthWeight_, seedWithDepthMode(Glitch::DepthMode::HOLD));
    const Glitch::SamplePlan inside = Glitch::samplePlan(params, ivec2(300, 150), boxDepth);
    EXPECT_FLOAT_EQ(inside.depthHoldBlend_, 1.0f);
    EXPECT_EQ(inside.source_, ivec2(300, 150));
    const Glitch::SamplePlan outside = Glitch::samplePlan(params, ivec2(50, 150), boxDepth);
    EXPECT_FLOAT_EQ(outside.depthHoldBlend_, 0.0f);
}

TEST(GlitchMathTest, DepthLaneIsInertOnAnAllBackgroundFrame) {
    const Glitch::FrameParams params = onlyEffect(&C_GlitchState::depthWeight_);
    EXPECT_FALSE(Glitch::selectDepthInterval(params, backgroundDepth).valid_);
}

TEST(GlitchMathTest, DepthTearComposesAfterOrdinaryTear) {
    // A narrow trixel keeps both offsets clear of the edge clamp.
    constexpr ivec2 kNarrowStep{2, 4};
    C_GlitchState state;
    state.seed_ = seedWithDepthMode(Glitch::DepthMode::TEAR);
    state.intensity_ = 1.0f;
    state.tearWeight_ = 1.0f;
    state.depthWeight_ = 1.0f;
    const Glitch::FrameParams both =
        Glitch::frameParams(state, Glitch::clockAt(state, 0.0), kNarrowStep, kSize);
    state.depthWeight_ = 0.0f;
    const Glitch::FrameParams tearOnly =
        Glitch::frameParams(state, Glitch::clockAt(state, 0.0), kNarrowStep, kSize);
    state.tearWeight_ = 0.0f;
    state.depthWeight_ = 1.0f;
    const Glitch::FrameParams depthOnly =
        Glitch::frameParams(state, Glitch::clockAt(state, 0.0), kNarrowStep, kSize);

    for (int y = 100; y < 260; y += kNarrowStep.y) {
        const ivec2 pixel{320, y};
        const int tearShift = Glitch::samplePlan(tearOnly, pixel, boxDepth).source_.x - pixel.x;
        const int depthShift = Glitch::samplePlan(depthOnly, pixel, boxDepth).source_.x - pixel.x;
        const int bothShift = Glitch::samplePlan(both, pixel, boxDepth).source_.x - pixel.x;
        EXPECT_EQ(bothShift, tearShift + depthShift) << "row " << y;
    }
}

TEST(GlitchMathTest, GeometryHoldsStillUntilTheNextTick) {
    C_GlitchState state;
    state.seed_ = 9u;
    state.intensity_ = 1.0f;
    state.tearWeight_ = 1.0f;
    state.periodSeconds_ = 0.5;
    const auto shiftsAt = [&state](double time) {
        const Glitch::FrameParams params =
            Glitch::frameParams(state, Glitch::clockAt(state, time), kTrixelStep, kSize);
        std::string shifts;
        for (int y = 0; y < kSize.y; y += kTrixelStep.y) {
            shifts += std::to_string(
                          Glitch::samplePlan(params, ivec2(320, y), backgroundDepth).source_.x
                      ) +
                      ",";
        }
        return shifts;
    };
    EXPECT_EQ(shiftsAt(0.01), shiftsAt(0.49));
    EXPECT_NE(shiftsAt(0.49), shiftsAt(0.51));
}

TEST(GlitchMathTest, FrameDataPacksTheTablesTheShaderReads) {
    C_GlitchState state;
    state.seed_ = 11u;
    state.intensity_ = 1.0f;
    state.tearWeight_ = 1.0f;
    state.holdWeight_ = 0.5f;
    state.barsWeight_ = 1.0f;
    const Glitch::FrameTables tables = Glitch::frameTables(
        Glitch::frameParams(state, Glitch::clockAt(state, 0.0), kTrixelStep, kSize)
    );
    const IRSystem::FrameDataFramebufferGlitch data =
        IRSystem::packGlitchFrameData(tables, Glitch::toTickWords(-3), IRMath::mat4(1.0f));
    EXPECT_EQ(data.tickWords_.z, 0xFFFFFFFDu);
    EXPECT_EQ(data.tickWords_.w, 0xFFFFFFFFu);
    EXPECT_EQ(data.counts_.x, Glitch::kMaxBands);
    EXPECT_EQ(data.counts_.y, Glitch::bandCount(0.5f));
    EXPECT_EQ(data.counts_.z, 0);
    EXPECT_GE(data.counts_.w, 1);
    for (int band = 0; band < data.counts_.x; ++band) {
        EXPECT_EQ(data.tearBands_[band].x, tables.tear_[band].start_);
        EXPECT_EQ(data.tearBands_[band].y, tables.tear_[band].end_);
        EXPECT_EQ(data.tearBands_[band].z % kTrixelStep.x, 0);
        EXPECT_NE(data.tearBands_[band].z, 0);
    }
    for (int bar = 0; bar < data.counts_.w; ++bar) {
        EXPECT_EQ(data.barRows_[bar], tables.barRows_[bar]);
    }
    EXPECT_EQ(data.hashInputs_.w, 0u) << "depth is off";
}

// ---- frame plan -------------------------------------------------------------

TEST(GlitchMathTest, FramePlanIsOffWithoutAnEnabledState) {
    EXPECT_FALSE(IRSystem::planGlitchFrame(nullptr, 0, false, 0).enabled_);
    const C_GlitchState off;
    const IRSystem::GlitchFramePlan plan = IRSystem::planGlitchFrame(&off, 0, false, 0);
    EXPECT_FALSE(plan.enabled_);
    EXPECT_FALSE(plan.copyHistory_);
}

TEST(GlitchMathTest, FramePlanCopiesHistoryOnlyOnHoldTickChangesOrWhenInvalid) {
    C_GlitchState on;
    on.intensity_ = 1.0f;
    on.holdWeight_ = 1.0f;
    EXPECT_TRUE(IRSystem::planGlitchFrame(&on, 4, false, 4).copyHistory_);
    EXPECT_FALSE(IRSystem::planGlitchFrame(&on, 4, true, 4).copyHistory_);
    EXPECT_TRUE(IRSystem::planGlitchFrame(&on, 5, true, 4).copyHistory_);
    EXPECT_TRUE(IRSystem::planGlitchFrame(&on, 5, true, 4).enabled_);
}

TEST(GlitchMathTest, FramePlanNeverCopiesHistoryNoEffectReads) {
    C_GlitchState on;
    on.intensity_ = 1.0f;
    on.tearWeight_ = 1.0f;
    on.splitWeight_ = 1.0f;
    on.barsWeight_ = 1.0f;
    on.cellsWeight_ = 1.0f;
    const IRSystem::GlitchFramePlan plan = IRSystem::planGlitchFrame(&on, 5, false, 4);
    EXPECT_TRUE(plan.enabled_);
    EXPECT_FALSE(plan.copyHistory_);
    on.depthWeight_ = 0.5f;
    EXPECT_TRUE(IRSystem::planGlitchFrame(&on, 5, false, 4).copyHistory_);
}

// ---- shader twin constants ---------------------------------------------------

std::string readShader(const std::string &relative) {
    std::ifstream file(std::string(IR_TEST_RENDER_SHADER_DIR) + "/" + relative);
    std::stringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

TEST(GlitchMathTest, ShaderTwinsCarryTheCpuConstants) {
    const std::string expected[] = {
        "kGlitchEffectTear = " + std::to_string(Glitch::kEffectTear) + "u;",
        "kGlitchEffectHold = " + std::to_string(Glitch::kEffectHold) + "u;",
        "kGlitchEffectSplit = " + std::to_string(Glitch::kEffectSplit) + "u;",
        "kGlitchEffectBars = " + std::to_string(Glitch::kEffectBars) + "u;",
        "kGlitchEffectCells = " + std::to_string(Glitch::kEffectCells) + "u;",
        "kGlitchEffectDepth = " + std::to_string(Glitch::kEffectDepth) + "u;",
        "kGlitchLaneBandBase = " + std::to_string(Glitch::kLaneBandBase) + "u;",
        "kGlitchLaneTearOffsetBase = " + std::to_string(Glitch::kLaneTearOffsetBase) + "u;",
        "kGlitchLaneCellSize = " + std::to_string(Glitch::kLaneCellSize) + "u;",
        "kGlitchLaneCellBase = " + std::to_string(Glitch::kLaneCellBase) + "u;",
        "kGlitchLaneSplitSign = " + std::to_string(Glitch::kLaneSplitSign) + "u;",
        "kGlitchLaneBarCount = " + std::to_string(Glitch::kLaneBarCount) + "u;",
        "kGlitchLaneBarBase = " + std::to_string(Glitch::kLaneBarBase) + "u;",
        "kGlitchLaneDepthMode = " + std::to_string(Glitch::kLaneDepthMode) + "u;",
        "kGlitchLaneDepthProbe = " + std::to_string(Glitch::kLaneDepthProbe) + "u;",
        "kGlitchLaneDepthWidth = " + std::to_string(Glitch::kLaneDepthWidth) + "u;",
        "kGlitchLaneDepthOffset = " + std::to_string(Glitch::kLaneDepthOffset) + "u;",
        "kGlitchMaxBands = " + std::to_string(Glitch::kMaxBands) + ";",
        "kGlitchMaxBandHalfRows = " + std::to_string(Glitch::kMaxBandHalfRows) + ";",
        "kGlitchMaxBars = " + std::to_string(Glitch::kMaxBars) + ";",
        "kGlitchBarPaletteSize = " + std::to_string(Glitch::kBarPaletteSize) + ";",
        "kGlitchDepthProbeGrid = " + std::to_string(Glitch::kDepthProbeGrid) + ";",
        "kGlitchDepthProbeCount = " + std::to_string(Glitch::kDepthProbeCount) + ";",
        "kGlitchCellHashX = 0x8DA6B343u;",
        "kGlitchCellHashY = 0xD8163841u;",
    };
    static_assert(Glitch::kCellHashX == 0x8DA6B343u && Glitch::kCellHashY == 0xD8163841u);
    for (const char *twin : {"ir_glitch_common.glsl", "metal/ir_glitch_common.metal"}) {
        const std::string source = readShader(twin);
        ASSERT_FALSE(source.empty()) << twin << " not found";
        for (const std::string &line : expected) {
            EXPECT_NE(source.find(line), std::string::npos) << twin << " lacks `" << line << "`";
        }
    }
    // Both twins hash through ir_iso_common's hash3, whose multipliers the
    // CPU mirror shares.
    for (const char *common : {"ir_iso_common.glsl", "metal/ir_iso_common.metal"}) {
        const std::string source = readShader(common);
        EXPECT_NE(source.find("0x9E3779B1u"), std::string::npos) << common;
        EXPECT_NE(source.find("0x85EBCA77u"), std::string::npos) << common;
        EXPECT_NE(source.find("0xC2B2AE3Du"), std::string::npos) << common;
    }
}

} // namespace

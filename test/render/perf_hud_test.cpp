#include <gtest/gtest.h>

#include <irreden/render/systems/system_perf_hud.hpp>

#include <cstdint>
#include <memory>
#include <string>

// The perf HUD's numbers are derived from its frame-time ring alone, so these
// drive the ring directly and read back the formatted text, the grade colours
// and the graph block — the GUI-test shots only see that the HUD drew.

namespace {

using Hud = IRSystem::System<IRSystem::PERF_HUD>;
using IRMath::Color;

constexpr float kBudgetMs = IRRender::kFrameTimeBudgetMs;
constexpr float kDropMs = kBudgetMs * static_cast<float>(IRSystem::kPerfHudGraphBudgets);
constexpr int kWidth = IRSystem::kPerfHudGraphWidth;
constexpr int kHeight = IRSystem::kPerfHudGraphHeight;
constexpr int kBudgetRow = 7;

static_assert(kHeight - 1 - kHeight / IRSystem::kPerfHudGraphBudgets == kBudgetRow);

// `Color` has no `operator==`; compare its packed form.
std::uint32_t rgba(Color color) {
    return color.toPackedRGBA();
}

class PerfHudTest : public testing::Test {
  protected:
    // The graph block alone is a few kilobytes; keep it off the test stack.
    std::unique_ptr<Hud> m_hud = std::make_unique<Hud>();

    void fill(float frameMs, int frames = IRSystem::kPerfHudHistoryFrames) {
        for (int i = 0; i < frames; ++i) {
            m_hud->recordFrame(frameMs);
        }
    }

    void setSmoothed(double fps, double frameMs) {
        m_hud->fpsEma_ = fps;
        m_hud->frameMsEma_ = frameMs;
    }

    std::string line(int row) const {
        const std::size_t newline = m_hud->text_.find('\n');
        return row == 0 ? m_hud->text_.substr(0, newline) : m_hud->text_.substr(newline + 1);
    }

    std::uint32_t pixel(int x, int y) const {
        return rgba(m_hud->graphPixels_[y * kWidth + x]);
    }

    // Rows filled with a bar colour in column `x`, counted up from the bottom
    // until the first row that is not `color`.
    int barHeight(int x, Color color) const {
        int height = 0;
        while (height < kHeight && pixel(x, kHeight - 1 - height) == rgba(color)) {
            ++height;
        }
        return height;
    }
};

TEST_F(PerfHudTest, SampleZeroIsTheNewestFrameAndOlderFramesFollowInOrder) {
    m_hud->recordFrame(10.0f);
    m_hud->recordFrame(20.0f);
    m_hud->recordFrame(30.0f);

    EXPECT_EQ(m_hud->historyCount_, 3);
    EXPECT_FLOAT_EQ(m_hud->sampleMs(0), 30.0f);
    EXPECT_FLOAT_EQ(m_hud->sampleMs(1), 20.0f);
    EXPECT_FLOAT_EQ(m_hud->sampleMs(2), 10.0f);
}

TEST_F(PerfHudTest, AFullRingOverwritesTheOldestFrameAndKeepsItsOrder) {
    constexpr int kFrames = IRSystem::kPerfHudHistoryFrames;
    for (int i = 1; i <= kFrames + 3; ++i) {
        m_hud->recordFrame(static_cast<float>(i));
    }

    EXPECT_EQ(m_hud->historyCount_, kFrames);
    for (int ago = 0; ago < kFrames; ++ago) {
        EXPECT_FLOAT_EQ(m_hud->sampleMs(ago), static_cast<float>(kFrames + 3 - ago))
            << "ago " << ago;
    }
}

TEST_F(PerfHudTest, SteadyFramesFormatBothLinesInTheirFixedColumns) {
    fill(10.0f);
    setSmoothed(60.0, 16.7);
    m_hud->buildText();

    EXPECT_EQ(m_hud->text_, "FPS  60  MIN 100\nMS 16.7  DROP  0");
    EXPECT_EQ(rgba(m_hud->fpsColor_), rgba(IRSystem::kPerfHudGood));
    EXPECT_EQ(rgba(m_hud->minFpsColor_), rgba(IRSystem::kPerfHudGood));
    EXPECT_EQ(rgba(m_hud->frameMsColor_), rgba(IRSystem::kPerfHudGood));
    EXPECT_EQ(rgba(m_hud->dropsColor_), rgba(IRSystem::kPerfHudGood));
}

// `queueText` colours each value run by fixed character offsets, so the
// labels must hold their columns whatever the values' widths.
TEST_F(PerfHudTest, ExtremeValuesClampWithoutMovingAnyLabel) {
    fill(5000.0f);
    setSmoothed(123456.0, 5000.0);
    m_hud->buildText();

    for (int row = 0; row < IRSystem::kPerfHudTextLines; ++row) {
        EXPECT_EQ(static_cast<int>(line(row).size()), IRSystem::kPerfHudLineChars) << "row " << row;
    }
    EXPECT_EQ(line(0), "FPS9999  MIN   0");
    EXPECT_EQ(line(1), "MS999.9  DROP128");
    EXPECT_EQ(line(0).substr(0, 3), "FPS");
    EXPECT_EQ(line(0).substr(9, 3), "MIN");
    EXPECT_EQ(line(1).substr(0, 2), "MS");
    EXPECT_EQ(line(1).substr(9, 4), "DROP");
}

TEST_F(PerfHudTest, MinIsTheSlowestFrameInTheWindow) {
    fill(10.0f);
    m_hud->recordFrame(40.0f);
    fill(10.0f, 5);
    setSmoothed(60.0, 16.7);
    m_hud->buildText();

    EXPECT_EQ(line(0), "FPS  60  MIN  25");
    EXPECT_EQ(rgba(m_hud->minFpsColor_), rgba(IRSystem::kPerfHudBad));
}

TEST_F(PerfHudTest, AFrameThatLeftTheWindowNoLongerSetsMinOrDrop) {
    m_hud->recordFrame(100.0f);
    fill(10.0f);
    setSmoothed(60.0, 16.7);
    m_hud->buildText();

    EXPECT_EQ(m_hud->text_, "FPS  60  MIN 100\nMS 16.7  DROP  0");
}

// `EventProfiler<RENDER>::endEvent` counts a frame dropped only when it runs strictly
// past two frame periods, so a frame exactly on the threshold is not a drop.
TEST_F(PerfHudTest, AFrameExactlyTwoBudgetsLongIsNotADropButOneJustPastIs) {
    fill(10.0f);
    m_hud->recordFrame(kDropMs);
    m_hud->recordFrame(kDropMs * 0.999f);
    setSmoothed(60.0, 16.7);
    m_hud->buildText();

    EXPECT_EQ(line(1), "MS 16.7  DROP  0");

    m_hud->recordFrame(kDropMs * 1.001f);
    m_hud->buildText();

    EXPECT_EQ(line(1), "MS 16.7  DROP  1");
}

TEST_F(PerfHudTest, DropCountGradesZeroGoodUpToTheWarnLimitWarnThenBad) {
    for (int drops = 0; drops <= IRSystem::kPerfHudWarnDrops + 1; ++drops) {
        m_hud = std::make_unique<Hud>();
        fill(10.0f);
        fill(kDropMs * 2.0f, drops);
        setSmoothed(60.0, 16.7);
        m_hud->buildText();

        const Color expected = drops == 0                             ? IRSystem::kPerfHudGood
                               : drops <= IRSystem::kPerfHudWarnDrops ? IRSystem::kPerfHudWarn
                                                                      : IRSystem::kPerfHudBad;
        EXPECT_EQ(rgba(m_hud->dropsColor_), rgba(expected)) << "drops " << drops;
        EXPECT_EQ(rgba(Hud::dropColor(drops)), rgba(expected)) << "drops " << drops;
    }
}

TEST_F(PerfHudTest, HealthBandsAreInclusiveAtTheirLowerBounds) {
    using IRSystem::kPerfHudGoodFraction;
    using IRSystem::kPerfHudWarnFraction;
    EXPECT_EQ(rgba(Hud::healthColor(1.5f)), rgba(IRSystem::kPerfHudGood));
    EXPECT_EQ(rgba(Hud::healthColor(kPerfHudGoodFraction)), rgba(IRSystem::kPerfHudGood));
    EXPECT_EQ(rgba(Hud::healthColor(kPerfHudGoodFraction - 0.001f)), rgba(IRSystem::kPerfHudWarn));
    EXPECT_EQ(rgba(Hud::healthColor(kPerfHudWarnFraction)), rgba(IRSystem::kPerfHudWarn));
    EXPECT_EQ(rgba(Hud::healthColor(kPerfHudWarnFraction - 0.001f)), rgba(IRSystem::kPerfHudBad));
    EXPECT_EQ(rgba(Hud::healthColor(0.0f)), rgba(IRSystem::kPerfHudBad));
}

// FPS grades against the target rate, frame time against the budget, so a
// faster value grades better on one and a smaller value on the other.
TEST_F(PerfHudTest, FpsAndFrameTimeGradeInOppositeDirections) {
    fill(10.0f);
    setSmoothed(50.0, kBudgetMs / 0.8);
    m_hud->buildText();
    EXPECT_EQ(rgba(m_hud->fpsColor_), rgba(IRSystem::kPerfHudWarn)) << "50 of 60 FPS is 0.83";
    EXPECT_EQ(rgba(m_hud->frameMsColor_), rgba(IRSystem::kPerfHudWarn))
        << "budget / frame time is 0.8";

    setSmoothed(30.0, kBudgetMs * 0.5);
    m_hud->buildText();
    EXPECT_EQ(rgba(m_hud->fpsColor_), rgba(IRSystem::kPerfHudBad));
    EXPECT_EQ(rgba(m_hud->frameMsColor_), rgba(IRSystem::kPerfHudGood));
}

TEST_F(PerfHudTest, AnEmptyGraphIsBackgroundCrossedByTheBudgetLine) {
    m_hud->buildGraph();

    for (int y = 0; y < kHeight; ++y) {
        const Color expected =
            y == kBudgetRow ? m_hud->theme_.borderIdle_ : IRSystem::kPerfHudGraphBackground;
        for (int x = 0; x < kWidth; ++x) {
            ASSERT_EQ(pixel(x, y), rgba(expected)) << "x " << x << " y " << y;
        }
    }
}

TEST_F(PerfHudTest, TheNewestFrameIsTheRightmostColumn) {
    m_hud->recordFrame(kBudgetMs * 0.4f);
    m_hud->recordFrame(kBudgetMs * 1.4f);
    m_hud->buildGraph();

    EXPECT_EQ(barHeight(kWidth - 1, IRSystem::kPerfHudBad), 12);
    EXPECT_EQ(barHeight(kWidth - 2, IRSystem::kPerfHudGood), 4);
    EXPECT_EQ(barHeight(kWidth - 3, IRSystem::kPerfHudGood), 0) << "no frame recorded there";
    EXPECT_EQ(pixel(kWidth - 3, kHeight - 1), rgba(IRSystem::kPerfHudGraphBackground));
}

// A bar is frame time over two budgets of height, so a frame exactly on
// budget fills up to the budget line without covering it.
TEST_F(PerfHudTest, ABudgetLengthFrameReachesTheBudgetLine) {
    m_hud->recordFrame(kBudgetMs);
    m_hud->buildGraph();

    EXPECT_EQ(barHeight(kWidth - 1, IRSystem::kPerfHudGood), kHeight - 1 - kBudgetRow);
    EXPECT_EQ(pixel(kWidth - 1, kBudgetRow), rgba(m_hud->theme_.borderIdle_));
}

TEST_F(PerfHudTest, BarHeightClampsToOneRowAndToTheFullGraph) {
    m_hud->recordFrame(kBudgetMs * 10.0f);
    m_hud->recordFrame(0.01f);
    m_hud->recordFrame(0.0f);
    m_hud->buildGraph();

    EXPECT_EQ(barHeight(kWidth - 3, IRSystem::kPerfHudBad), kHeight);
    EXPECT_EQ(barHeight(kWidth - 2, IRSystem::kPerfHudGood), 1);
    EXPECT_EQ(pixel(kWidth - 1, kHeight - 1), rgba(IRSystem::kPerfHudGraphBackground))
        << "a zero-length frame draws no bar";
}

TEST_F(PerfHudTest, ContentHeightStacksTheTextBlockTheGapAndTheGraph) {
    EXPECT_EQ(Hud::textBlockHeight(), IRRender::kGlyphStepY + IRRender::kGlyphHeight);
    EXPECT_EQ(
        Hud::contentSize(),
        IRMath::ivec2(kWidth, Hud::textBlockHeight() + IRSystem::kPerfHudGraphGap + kHeight)
    );
}

} // namespace

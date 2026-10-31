#include <gtest/gtest.h>

#include <irreden/profile/profile_report.hpp>
#include <irreden/profile/scope_timer.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

// One directory per test: CTest registers each test separately and may run two
// of them at once. The read stream is closed before remove_all because Windows
// refuses to delete a file that still has an open handle.
std::string writeAndRead(const IRProfile::ProfileReport &report) {
    const std::string test = testing::UnitTest::GetInstance()->current_test_info()->name();
    const auto path = std::filesystem::temp_directory_path() / ("ir_profile_report_" + test) /
                      "profile_report.txt";
    IRProfile::writeProfileReport(report, path.string().c_str());
    std::stringstream text;
    {
        std::ifstream file(path);
        text << file.rdbuf();
    }
    std::filesystem::remove_all(path.parent_path());
    return text.str();
}

TEST(ProfileReportWitness, SteadyLineExcludesTheLeadingQuarter) {
    IRProfile::ProfileReport report;
    report.frameTimesMs_ = {100.0f, 90.0f, 20.0f, 21.0f, 19.0f, 20.0f, 22.0f, 20.0f};
    report.totalFrames_ = 8;
    const std::string text = writeAndRead(report);
    EXPECT_NE(text.find("Frame time:   avg=39.00ms"), std::string::npos) << text;
    EXPECT_NE(
        text.find("Steady frame time (first 2 of 8 frames excluded):   avg=20.33ms"),
        std::string::npos
    ) << text;
    EXPECT_NE(text.find("p99=22.00ms   min=19.00ms   max=22.00ms"), std::string::npos) << text;
    EXPECT_NE(
        text.find(
            "--- Frame times (ms, in order) ---\n100.000 90.000 20.000 21.000 19.000 "
            "20.000 22.000 20.000\n"
        ),
        std::string::npos
    ) << text;
}

TEST(ProfileReportWitness, WitnessSectionIsWrittenEvenWhenNothingWasSampled) {
    IRProfile::ProfileReport report;
    report.witness_.yawFirstDeg_ = -135.0f;
    report.witness_.yawLastDeg_ = -135.0f;
    report.witness_.zoomFirst_ = 4.0f;
    report.witness_.zoomLast_ = 4.0f;
    report.witness_.zoomMin_ = 1.0f;
    report.witness_.zoomMax_ = 32.0f;
    report.witness_.poseSamples_ = 8;
    report.witness_.explicitPivotSamples_ = 8;
    report.witness_.overflowSamples_ = 7;
    report.witness_.maxOverflowEntries_ = 630842;
    report.witness_.maxOverflowDropped_ = 3;
    report.witness_.overflowCap_ = 1048576;
    const std::string text = writeAndRead(report);
    EXPECT_NE(
        text.find("Camera yaw: first=-135.000deg last=-135.000deg travel=0.000deg samples=8\n"),
        std::string::npos
    ) << text;
    EXPECT_NE(text.find("Camera zoom: first=4.000 last=4.000\n"), std::string::npos) << text;
    EXPECT_NE(text.find("Camera zoom range: min=1.000 max=32.000\n"), std::string::npos) << text;
    EXPECT_NE(text.find("Camera pivot: explicit focus on 8 of 8 frames\n"), std::string::npos)
        << text;
    EXPECT_NE(
        text.find("Per-axis overflow: maxEntries=630842 maxDropped=3 cap=1048576 samples=7\n"),
        std::string::npos
    ) << text;

    const std::string empty = writeAndRead(IRProfile::ProfileReport{});
    EXPECT_NE(empty.find("Camera zoom range: min=0.000 max=0.000\n"), std::string::npos) << empty;
    EXPECT_NE(
        empty.find("Per-axis overflow: maxEntries=0 maxDropped=0 cap=0 samples=0\n"),
        std::string::npos
    ) << empty;
    EXPECT_EQ(empty.find("Steady frame time"), std::string::npos);
    EXPECT_EQ(empty.find("--- Frame times"), std::string::npos);
}

TEST(ProfileReportWitness, UpdateTickSeriesIsWrittenInFrameOrder) {
    IRProfile::ProfileReport report;
    report.frameTimesMs_ = {40.0f, 700.0f, 160.0f};
    report.frameUpdateTicks_ = {2, 8, 8};
    report.totalFrames_ = 3;
    const std::string text = writeAndRead(report);
    EXPECT_NE(text.find("--- Update ticks (per frame, in order) ---\n2 8 8\n"), std::string::npos)
        << text;
}

TEST(ProfileReportWitness, ALongRunKeepsItsSteadyLineAndDropsItsSeries) {
    IRProfile::ProfileReport report;
    report.frameTimesMs_.assign(IRProfile::kProfileSeriesMaxFrames + 1, 16.0f);
    report.totalFrames_ = static_cast<uint32_t>(report.frameTimesMs_.size());
    const std::string text = writeAndRead(report);
    EXPECT_NE(
        text.find("Steady frame time (first 2048 of 8193 frames excluded)"),
        std::string::npos
    );
    EXPECT_EQ(text.find("--- Frame times"), std::string::npos);
}

TEST(ProfileReportCpuScopes, ScopeRowsFollowTheFixedPhasesWithRunWideSampleCounts) {
    IRProfile::CpuFrameHistogram histogram;
    histogram.enabled_ = true;
    histogram.record("ScopeB", 0.5);
    histogram.record("ScopeA", 1.0);
    histogram.record("ScopeA", 3.0);
    histogram.endFrame();
    histogram.record("ScopeA", 2.0);
    histogram.endFrame();

    IRProfile::ProfileReport report;
    report.cpuPhases_.push_back({"FixedPhase", 4.0, 4.0, 1});
    IRProfile::appendCpuScopePhases(report, histogram);
    const std::string text = writeAndRead(report);

    // Same row format as the writer: the name column is 36 wide.
    const auto row = [&](const char *name, double avg, double max, unsigned samples) {
        char line[128];
        std::snprintf(line, sizeof(line), "%-36s %9.3f %9.3f %7u\n", name, avg, max, samples);
        return text.find(line);
    };
    const auto fixed = row("FixedPhase", 4.0, 4.0, 1);
    const auto a = row("ScopeA", 2.0, 3.0, 3);
    const auto b = row("ScopeB", 0.5, 0.5, 1);
    ASSERT_NE(fixed, std::string::npos) << text;
    ASSERT_NE(a, std::string::npos) << text;
    ASSERT_NE(b, std::string::npos) << text;
    EXPECT_LT(fixed, a) << text;
    EXPECT_LT(a, b) << text;
}

// A scope still open at the frame boundary exits after it, so no later
// `endFrame()` sees its final exit; the report must count it anyway.
TEST(ProfileReportCpuScopes, AScopeOpenAcrossTheFrameBoundaryKeepsItsFinalExit) {
    auto &histogram = IRProfile::cpuFrameHistogram();
    const bool wasEnabled = histogram.enabled_;
    histogram.enabled_ = true;
    histogram.resetRun();
    constexpr unsigned kFrames = 3;
    for (unsigned frame = 0; frame < kFrames; ++frame) {
        IRProfile::ScopeTimer outer{"OpenAcrossBoundary"};
        histogram.endFrame();
    }

    IRProfile::ProfileReport report;
    IRProfile::appendCpuScopePhases(report, histogram);
    histogram.endFrame();
    histogram.resetRun();
    histogram.enabled_ = wasEnabled;

    unsigned samples = 0;
    for (const auto &phase : report.cpuPhases_) {
        if (phase.name_ == "OpenAcrossBoundary") {
            samples = phase.sampleCount_;
        }
    }
    EXPECT_EQ(samples, kFrames);
}

TEST(ProfileReportCpuScopes, ADisabledHistogramAppendsNothing) {
    IRProfile::CpuFrameHistogram histogram;
    histogram.record("ScopeA", 1.0);
    histogram.endFrame();

    IRProfile::ProfileReport report;
    IRProfile::appendCpuScopePhases(report, histogram);
    EXPECT_TRUE(report.cpuPhases_.empty());
}

TEST(ProfileReportCpuScopes, ResetRunDropsTotalsButKeepsTheHistogramEnabled) {
    IRProfile::CpuFrameHistogram histogram;
    histogram.enabled_ = true;
    histogram.record("ScopeA", 1.0);
    histogram.endFrame();
    histogram.resetRun();

    EXPECT_TRUE(histogram.runTotals().empty());
    EXPECT_TRUE(histogram.enabled_);
}

} // namespace

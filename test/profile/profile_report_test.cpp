#include <gtest/gtest.h>

#include <irreden/profile/profile_report.hpp>
#include <irreden/profile/scope_timer.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

IRProfile::GpuStageEntry
stageEntry(const char *name, float avg, uint32_t samples, float windowAvg, uint32_t windowSamples) {
    IRProfile::GpuStageEntry entry;
    entry.name_ = name;
    entry.totalMs_ = avg * static_cast<float>(samples);
    entry.minMs_ = samples > 0 ? avg : 0.0f;
    entry.maxMs_ = samples > 0 ? avg : 0.0f;
    entry.sampleCount_ = samples;
    entry.windowTotalMs_ = windowAvg * static_cast<float>(windowSamples);
    entry.windowMinMs_ = windowAvg;
    entry.windowMaxMs_ = windowAvg;
    entry.windowSampleCount_ = windowSamples;
    return entry;
}

TEST(ProfileReportGpuStages, RowsCarryExclusiveTimeAndTheWindowBesideIt) {
    IRProfile::ProfileReport report;
    report.gpuFrameTimings_.push_back({"envelope", 200.0f, 18.0f, 22.0f, 10});
    report.gpuFrameTimings_.push_back({"commandBufferSpans", 200.0f, 18.0f, 22.0f, 10});
    report.gpuStages_.push_back(stageEntry("computeLightVolume", 0.5f, 10, 17.0f, 10));
    report.gpuStages_.push_back(stageEntry("voxelStage1", 4.5f, 8, 5.0f, 10));
    report.gpuStages_.push_back(stageEntry("voxelPerAxisOverflow", 0.0f, 0, 16.0f, 10));
    report.gpuStages_.push_back(stageEntry("shapePass0", 0.0f, 0, 0.0f, 0));
    const std::string text = writeAndRead(report);

    EXPECT_NE(text.find("--- GPU stage timing ---\nFormat: exclusive+window\n"), std::string::npos)
        << text;
    EXPECT_NE(
        text.find(
            "Stage                                  Avg(ms)   Min(ms)   Max(ms) Samples "
            "Window(ms) WindowOnly\n"
        ),
        std::string::npos
    ) << text;
    EXPECT_NE(
        text.find(
            "computeLightVolume                       0.500     0.500     0.500      10     "
            "17.000          0\n"
        ),
        std::string::npos
    ) << text;
    EXPECT_NE(
        text.find(
            "voxelStage1                              4.500     4.500     4.500       8      "
            "5.000          2\n"
        ),
        std::string::npos
    ) << text;
    EXPECT_NE(
        text.find(
            "voxelPerAxisOverflow                         -         -         -       0     "
            "16.000         10 window-only\n"
        ),
        std::string::npos
    ) << text;
    EXPECT_EQ(text.find("shapePass0"), std::string::npos) << text;
    // A window-only row adds nothing to the sum: 0.5 + 4.5 against a 20 ms span.
    EXPECT_NE(
        text.find("Exclusive sum: 5.000 ms; commandBufferSpans: 20.000 ms (25.0%)\n"),
        std::string::npos
    ) << text;
}

TEST(ProfileReportGpuStages, TheSumLineSaysWhenThereIsNoSpan) {
    IRProfile::ProfileReport report;
    report.gpuStages_.push_back(stageEntry("trixelToFb", 0.25f, 4, 0.25f, 4));
    const std::string text = writeAndRead(report);
    EXPECT_NE(
        text.find("Exclusive sum: 0.250 ms; commandBufferSpans: unavailable\n"),
        std::string::npos
    ) << text;
}

TEST(ProfileReportVoxelCull, SunFaceAdmittedRowCarriesAverageMaximumAndSamples) {
    IRProfile::ProfileReport report;
    report.voxelCullStats_.sunFaceAdmittedSum_ = 1536;
    report.voxelCullStats_.maxSunFaceAdmitted_ = 1024;
    report.voxelCullStats_.sampleCount_ = 2;
    const std::string text = writeAndRead(report);

    EXPECT_NE(
        text.find("SunFaceAdmit          768.0           1024          2\n"),
        std::string::npos
    ) << text;
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

IRProfile::SystemTimingEntry systemEntry(const char *pipeline, uint64_t totalNs) {
    IRProfile::SystemTimingEntry entry;
    entry.name_ = std::string(pipeline) + "_SYSTEM";
    entry.pipeline_ = pipeline;
    entry.totalNs_ = totalNs;
    entry.callCount_ = 1;
    return entry;
}

// The decomposition's rows, in print order: label, ms per frame, share of the mean.
struct DecompositionRow {
    std::string label_;
    double ms_ = 0.0;
    double percent_ = 0.0;
    std::string line_;
};

bool isDecompositionLabel(const std::string &label) {
    for (const char *known : {"INPUT", "UPDATE", "RENDER", "GPU", "Pacing", "Remainder", "Frame"}) {
        if (label == known)
            return true;
    }
    return false;
}

std::vector<DecompositionRow> decompositionRows(const std::string &text) {
    std::vector<DecompositionRow> rows;
    const std::string header = "--- Frame decomposition ---\n";
    const size_t begin = text.find(header);
    if (begin == std::string::npos)
        return rows;
    const size_t end = text.find("\n\n", begin);
    std::istringstream lines(text.substr(begin + header.size(), end - begin - header.size()));
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        DecompositionRow row;
        std::string msWord;
        std::string percentWord;
        if (!(words >> row.label_ >> msWord) || !isDecompositionLabel(row.label_))
            continue;
        row.line_ = line;
        char *parsedEnd = nullptr;
        row.ms_ = std::strtod(msWord.c_str(), &parsedEnd);
        if (parsedEnd == msWord.c_str() || *parsedEnd != '\0') {
            if (msWord == "unavailable")
                rows.push_back(row);
            continue;
        }
        if (words >> percentWord)
            row.percent_ = std::strtod(percentWord.c_str(), nullptr);
        rows.push_back(row);
    }
    return rows;
}

const DecompositionRow *findRow(const std::vector<DecompositionRow> &rows, const char *label) {
    for (const auto &row : rows) {
        if (row.label_ == label)
            return &row;
    }
    return nullptr;
}

IRProfile::ProfileReport decompositionReport() {
    IRProfile::ProfileReport report;
    report.totalFrames_ = 4;
    report.frameTimesMs_ = {50.0f, 50.0f, 50.0f, 50.0f};
    report.systemTimings_.push_back(systemEntry("INPUT", 40000));
    report.systemTimings_.push_back(systemEntry("UPDATE", 30000000));
    report.systemTimings_.push_back(systemEntry("UPDATE", 10000000));
    report.systemTimings_.push_back(systemEntry("RENDER", 24000000));
    report.gpuFrameTimingSupported_ = true;
    report.gpuFrameTimings_.push_back({"envelope", 100.0f, 25.0f, 25.0f, 4});
    report.gpuFrameTimings_.push_back({"commandBufferSpans", 80.0f, 20.0f, 20.0f, 4});
    report.cpuPhases_.push_back({std::string(IRProfile::kOffscreenPacingPhase), 33.32, 8.33, 4});
    return report;
}

TEST(ProfileReportDecomposition, RowsCarryTheExpectedPerFrameFigures) {
    const auto rows = decompositionRows(writeAndRead(decompositionReport()));
    ASSERT_EQ(rows.size(), 7u);
    EXPECT_NEAR(findRow(rows, "INPUT")->ms_, 0.010, 1e-9);
    EXPECT_NEAR(findRow(rows, "UPDATE")->ms_, 10.0, 1e-9);
    EXPECT_NEAR(findRow(rows, "RENDER")->ms_, 6.0, 1e-9);
    EXPECT_NEAR(findRow(rows, "GPU")->ms_, 20.0, 1e-9);
    EXPECT_NEAR(findRow(rows, "Pacing")->ms_, 8.33, 1e-9);
    EXPECT_NEAR(findRow(rows, "Remainder")->ms_, 5.66, 1e-9);
    EXPECT_NEAR(findRow(rows, "Frame")->ms_, 50.0, 1e-9);
    EXPECT_NEAR(findRow(rows, "UPDATE")->percent_, 20.0, 1e-9);
    EXPECT_NEAR(findRow(rows, "Frame")->percent_, 100.0, 1e-9);
}

TEST(ProfileReportDecomposition, RowsAddUpToTheFrameMeanAtThePrintedPrecision) {
    IRProfile::ProfileReport report = decompositionReport();
    report.frameTimesMs_ = {33.3333f, 41.1111f, 38.7777f};
    report.systemTimings_[1].totalNs_ = 13333333;
    report.systemTimings_[3].totalNs_ = 7777777;
    const auto rows = decompositionRows(writeAndRead(report));
    ASSERT_EQ(rows.size(), 7u);
    double sum = 0.0;
    for (const auto &row : rows) {
        if (row.label_ != "Frame")
            sum += row.ms_;
    }
    EXPECT_NEAR(sum, findRow(rows, "Frame")->ms_, 1e-9);
}

TEST(ProfileReportDecomposition, ANegativeRemainderPrintsAsItIs) {
    IRProfile::ProfileReport report = decompositionReport();
    report.gpuFrameTimings_[1] = {"commandBufferSpans", 160.0f, 40.0f, 40.0f, 4};
    const auto rows = decompositionRows(writeAndRead(report));
    ASSERT_NE(findRow(rows, "Remainder"), nullptr);
    EXPECT_NEAR(findRow(rows, "Remainder")->ms_, -14.34, 1e-9);
}

TEST(ProfileReportDecomposition, UnsupportedGpuTimingMarksTheRowUnavailable) {
    IRProfile::ProfileReport report = decompositionReport();
    report.gpuFrameTimingSupported_ = false;
    report.gpuFrameTimings_.clear();
    const auto rows = decompositionRows(writeAndRead(report));
    ASSERT_EQ(rows.size(), 7u);
    EXPECT_NE(findRow(rows, "GPU")->line_.find("unavailable"), std::string::npos);
    EXPECT_NE(findRow(rows, "Remainder")->line_.find("includes GPU time"), std::string::npos);
    EXPECT_NEAR(findRow(rows, "Remainder")->ms_, 25.66, 1e-9);
}

TEST(ProfileReportDecomposition, AGpuTimingWithNoValidFrameMarksTheRowUnavailable) {
    IRProfile::ProfileReport report = decompositionReport();
    report.gpuFrameTimings_[0].sampleCount_ = 0;
    report.gpuFrameTimings_[1].sampleCount_ = 0;
    const auto rows = decompositionRows(writeAndRead(report));
    EXPECT_NE(findRow(rows, "GPU")->line_.find("unavailable"), std::string::npos);
    EXPECT_NE(findRow(rows, "Remainder")->line_.find("includes GPU time"), std::string::npos);
}

TEST(ProfileReportDecomposition, AMissingPacingSampleReadsZero) {
    IRProfile::ProfileReport report = decompositionReport();
    report.cpuPhases_.clear();
    const auto rows = decompositionRows(writeAndRead(report));
    ASSERT_NE(findRow(rows, "Pacing"), nullptr);
    EXPECT_EQ(findRow(rows, "Pacing")->ms_, 0.0);
    EXPECT_NEAR(findRow(rows, "Remainder")->ms_, 13.99, 1e-9);
}

TEST(ProfileReportDecomposition, NoRecordedFrameWritesNoSection) {
    IRProfile::ProfileReport report = decompositionReport();
    report.frameTimesMs_.clear();
    EXPECT_EQ(writeAndRead(report).find("Frame decomposition"), std::string::npos);
}

// The scope timer must exist in every build type: a Release `--auto-profile`
// report is only as detailed as the scopes this macro records.
TEST(ProfileScope, EnabledHistogramRecordsOneSamplePerScopeExit) {
    auto &histogram = IRProfile::cpuFrameHistogram();
    histogram.enabled_ = true;
    histogram.resetRun();
    {
        IR_PROFILE_SCOPE("profile_scope_test_enabled");
    }
    const auto totals = histogram.runTotals();
    histogram.enabled_ = false;
    histogram.resetRun();
    const auto it = totals.find(std::string_view{"profile_scope_test_enabled"});
    ASSERT_NE(it, totals.end());
    EXPECT_EQ(it->second.count_, 1u);
}

TEST(ProfileScope, DisabledHistogramRecordsNothing) {
    auto &histogram = IRProfile::cpuFrameHistogram();
    histogram.enabled_ = false;
    histogram.resetRun();
    {
        IR_PROFILE_SCOPE("profile_scope_test_disabled");
    }
    const auto totals = histogram.runTotals();
    EXPECT_EQ(totals.find(std::string_view{"profile_scope_test_disabled"}), totals.end());
}

} // namespace

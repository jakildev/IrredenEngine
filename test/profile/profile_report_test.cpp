#include <gtest/gtest.h>

#include <irreden/profile/profile_report.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string writeAndRead(const IRProfile::ProfileReport &report) {
    const auto path =
        std::filesystem::temp_directory_path() / "ir_profile_report_test" / "profile_report.txt";
    IRProfile::writeProfileReport(report, path.string().c_str());
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
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
    report.witness_.poseSamples_ = 8;
    report.witness_.overflowSamples_ = 7;
    report.witness_.maxOverflowEntries_ = 630842;
    report.witness_.maxOverflowDropped_ = 3;
    report.witness_.overflowCap_ = 1048576;
    const std::string text = writeAndRead(report);
    EXPECT_NE(
        text.find("Camera yaw: first=-135.000deg last=-135.000deg travel=0.000deg samples=8\n"),
        std::string::npos
    ) << text;
    EXPECT_NE(
        text.find("Per-axis overflow: maxEntries=630842 maxDropped=3 cap=1048576 samples=7\n"),
        std::string::npos
    ) << text;

    const std::string empty = writeAndRead(IRProfile::ProfileReport{});
    EXPECT_NE(
        empty.find("Per-axis overflow: maxEntries=0 maxDropped=0 cap=0 samples=0\n"),
        std::string::npos
    ) << empty;
    EXPECT_EQ(empty.find("Steady frame time"), std::string::npos);
    EXPECT_EQ(empty.find("--- Frame times"), std::string::npos);
}

} // namespace

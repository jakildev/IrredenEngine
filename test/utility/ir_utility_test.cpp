#include <gtest/gtest.h>
#include <irreden/ir_utility.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

// ─────────────────────────────────────────────
// joinPath
// ─────────────────────────────────────────────

TEST(JoinPathTest, JoinsDirectoryFilenameAndExtension) {
    std::string result = IRUtility::joinPath("data", "texture", ".png");
    std::string expected = (std::filesystem::path("data") / "texture.png").string();
    EXPECT_EQ(result, expected);
}

TEST(JoinPathTest, PrependsDotToExtensionIfMissing) {
    std::string result = IRUtility::joinPath("data", "file", "txt");
    std::string expected = (std::filesystem::path("data") / "file.txt").string();
    EXPECT_EQ(result, expected);
}

TEST(JoinPathTest, LeavesExtensionWithLeadingDotUnchanged) {
    std::string result = IRUtility::joinPath("dir", "base", ".hpp");
    std::string expected = (std::filesystem::path("dir") / "base.hpp").string();
    EXPECT_EQ(result, expected);
}

TEST(JoinPathTest, EmptyExtensionProducesNoExtension) {
    std::string result = IRUtility::joinPath("dir", "base", "");
    std::string expected = (std::filesystem::path("dir") / "base").string();
    EXPECT_EQ(result, expected);
}

TEST(JoinPathTest, EmptyDirectoryGivesFilenameOnly) {
    std::string result = IRUtility::joinPath("", "base", ".txt");
    EXPECT_EQ(result, "base.txt");
}

// ─────────────────────────────────────────────
// pathWithExtension
// ─────────────────────────────────────────────

TEST(PathWithExtensionTest, ReplacesExistingExtension) {
    std::string result = IRUtility::pathWithExtension("file.txt", ".md");
    EXPECT_EQ(result, "file.md");
}

TEST(PathWithExtensionTest, PrependsDotToExtensionIfMissing) {
    std::string result = IRUtility::pathWithExtension("file.txt", "md");
    EXPECT_EQ(result, "file.md");
}

TEST(PathWithExtensionTest, AddsExtensionWhenNonePresent) {
    std::string result = IRUtility::pathWithExtension("file", ".txt");
    EXPECT_EQ(result, "file.txt");
}

TEST(PathWithExtensionTest, WorksWithSubdirectoryPath) {
    std::string result = IRUtility::pathWithExtension("data/textures/tile.png", ".txl");
    std::string expected =
        (std::filesystem::path("data/textures/tile.png").replace_extension(".txl")).string();
    EXPECT_EQ(result, expected);
}

// ─────────────────────────────────────────────
// formatNumberedFilename
// ─────────────────────────────────────────────

TEST(FormatNumberedFilenameTest, ZeroPadsToWidth) {
    EXPECT_EQ(IRUtility::formatNumberedFilename("screenshot_", 7, 4, ".png"), "screenshot_0007.png");
}

TEST(FormatNumberedFilenameTest, NoWidthMeansNoPadding) {
    EXPECT_EQ(IRUtility::formatNumberedFilename("frame", 42, 0, ".jpg"), "frame42.jpg");
}

TEST(FormatNumberedFilenameTest, IndexZeroWithWidth) {
    EXPECT_EQ(IRUtility::formatNumberedFilename("log", 0, 3, ".txt"), "log000.txt");
}

TEST(FormatNumberedFilenameTest, IndexWiderThanFieldWritesFullNumber) {
    // std::setw only pads; it never truncates
    EXPECT_EQ(IRUtility::formatNumberedFilename("f", 99999, 3, ".ext"), "f99999.ext");
}

TEST(FormatNumberedFilenameTest, PrependsDotToExtensionIfMissing) {
    EXPECT_EQ(IRUtility::formatNumberedFilename("file", 1, 2, "ext"), "file01.ext");
}

TEST(FormatNumberedFilenameTest, SingleDigitIndexWithWidth1) {
    EXPECT_EQ(IRUtility::formatNumberedFilename("a", 5, 1, ".b"), "a5.b");
}

// ─────────────────────────────────────────────
// readFileAsString
// ─────────────────────────────────────────────

TEST(ReadFileAsStringTest, ReadsExistingFileContent) {
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "ir_utility_test_read.txt";
    const std::string expected = "hello utility\nline two\n";
    {
        std::ofstream out(tmp);
        out << expected;
    }
    std::string result = IRUtility::readFileAsString(tmp.string());
    std::filesystem::remove(tmp);
    EXPECT_EQ(result, expected);
}

TEST(ReadFileAsStringTest, ReadsEmptyFile) {
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "ir_utility_test_empty.txt";
    { std::ofstream out(tmp); }
    std::string result = IRUtility::readFileAsString(tmp.string());
    std::filesystem::remove(tmp);
    EXPECT_EQ(result, "");
}

} // namespace

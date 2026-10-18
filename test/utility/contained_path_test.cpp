#include <gtest/gtest.h>
#include <irreden/utility/path_utils.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace {

// <root>/base is the canonical base; <root>/base2 shares its string prefix and
// <root>/outside.txt sits beside it.
class ContainedPathTest : public testing::Test {
  protected:
    ContainedPathTest() {
        const std::filesystem::path root =
            std::filesystem::temp_directory_path() /
            ("ir_contained_path_" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(root / "base" / "sub");
        std::filesystem::create_directories(root / "base2");
        std::ofstream(root / "base" / "sub" / "file.txt") << "in";
        std::ofstream(root / "base2" / "secret.txt") << "sibling";
        std::ofstream(root / "outside.txt") << "out";
        m_root = std::filesystem::canonical(root);
        m_base = m_root / "base";
    }

    ~ContainedPathTest() override {
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }

    // False when the host refuses to create directory symlinks (Windows
    // without developer mode); the caller skips.
    bool linkDirectory(const std::filesystem::path &target, const std::filesystem::path &link) {
        std::error_code error;
        std::filesystem::create_directory_symlink(target, link, error);
        return !error;
    }

    std::filesystem::path m_root;
    std::filesystem::path m_base;
};

TEST_F(ContainedPathTest, RejectsEveryLexicalCase) {
    const std::vector<std::string> rejected = {
        "",
        std::string("sub\0file.txt", 12),
        "/etc",
        "/etc/passwd",
        "C:\\x",
        "c:x",
        "\\x",
        "\\\\host\\s",
        "//host/s",
        "..",
        "../outside.txt",
        "sub/..",
        "sub/../sub/file.txt",
        "sub\\..\\..\\outside.txt",
        "sub/../../base2/secret.txt",
    };
    for (const std::string &path : rejected) {
        EXPECT_TRUE(IRUtility::containedPathRejection(path).has_value()) << path;
        EXPECT_FALSE(IRUtility::resolveContainedPath(m_base, path).has_value()) << path;
    }
}

TEST_F(ContainedPathTest, ResolvesPathsInsideTheBase) {
    EXPECT_EQ(IRUtility::resolveContainedPath(m_base, "sub/file.txt"), m_base / "sub" / "file.txt");
    EXPECT_EQ(
        IRUtility::resolveContainedPath(m_base, "./sub/file.txt"),
        m_base / "sub" / "file.txt"
    );
    EXPECT_EQ(IRUtility::resolveContainedPath(m_base, "."), m_base);
    EXPECT_EQ(
        IRUtility::resolveContainedPath(m_base, "missing/new.txt"),
        m_base / "missing" / "new.txt"
    );
    EXPECT_EQ(IRUtility::resolveContainedPath(m_base, "a..b"), m_base / "a..b");
    for (std::string_view path : {"sub/file.txt", "a..b", "x/...", "dir\\name"}) {
        EXPECT_FALSE(IRUtility::containedPathRejection(path).has_value()) << path;
    }
}

TEST_F(ContainedPathTest, RejectsASymlinkThatEscapesTheBase) {
    if (!linkDirectory(m_root, m_base / "escape")) {
        GTEST_SKIP() << "directory symlinks unavailable on this host";
    }
    EXPECT_FALSE(IRUtility::resolveContainedPath(m_base, "escape/outside.txt").has_value());
    EXPECT_FALSE(IRUtility::resolveContainedPath(m_base, "escape").has_value());
}

TEST_F(ContainedPathTest, RejectsAStringPrefixSibling) {
    if (!linkDirectory(m_root / "base2", m_base / "sibling")) {
        GTEST_SKIP() << "directory symlinks unavailable on this host";
    }
    const std::filesystem::path sibling = m_root / "base2" / "secret.txt";
    ASSERT_EQ(sibling.string().rfind(m_base.string(), 0), 0u) << "fixture lost its shared prefix";
    EXPECT_FALSE(IRUtility::resolveContainedPath(m_base, "sibling/secret.txt").has_value());
}

TEST_F(ContainedPathTest, ResolvesASymlinkThatStaysInside) {
    if (!linkDirectory(m_base / "sub", m_base / "inside")) {
        GTEST_SKIP() << "directory symlinks unavailable on this host";
    }
    EXPECT_EQ(
        IRUtility::resolveContainedPath(m_base, "inside/file.txt"),
        m_base / "sub" / "file.txt"
    );
}

} // namespace

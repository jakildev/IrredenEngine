#include <irreden/world/tool_spawn.hpp>

#include <gtest/gtest.h>

#include "../common/env_var.hpp"

#include <chrono>
#include <cerrno>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <thread>

namespace {

class ToolSpawnTest : public testing::Test {
  protected:
    void SetUp() override {
        m_root = std::filesystem::temp_directory_path() /
                 ("ir_tool_spawn_" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(m_root / "creations" / "inside");
        writeExecutable(
            m_root / "bin" / "ir-build",
            "#!/usr/bin/env bash\n"
            "if [[ \"$2\" == Sleep ]]; then echo \"parent:$$\"; sleep 10 & echo \"child:$!\"; "
            "wait; exit 0; fi\n"
            "if [[ \"$2\" == Flood ]]; then for ((i=0; i<2058; ++i)); do echo \"line-$i\"; done; "
            "exit 0; fi\n"
            "if [[ \"$2\" == Env ]]; then printf '%s\\n%s\\n' \"$PWD\" "
            "\"${IRREDEN_BUILD_DIR-unset}\"; exit 0; fi\n"
            "printf 'one\\ntw'\n"
            "sleep 0.05\n"
            "printf 'o\\nthree\\n'\n"
        );
        writeExecutable(m_root / "bin" / "ir-run", "#!/usr/bin/env bash\nexit 0\n");
#ifndef _WIN32
        writeExecutable(
            m_root / "bin" / "cursor",
            "#!/usr/bin/env bash\nprintf '%s\\n%s\\n' \"$1\" \"${IRREDEN_BUILD_DIR-unset}\"\n"
        );
#endif
        m_wrapper = std::make_unique<IRTest::ScopedEnv>(
            "IR_RUN_WRAPPER",
            (m_root / "bin" / "ir-run").string().c_str()
        );
        const char *path = std::getenv("PATH");
#ifdef _WIN32
        constexpr char kPathSeparator = ';';
#else
        constexpr char kPathSeparator = ':';
#endif
        const std::string augmentedPath =
            (m_root / "bin").string() + kPathSeparator + (path == nullptr ? "" : path);
        m_path = std::make_unique<IRTest::ScopedEnv>("PATH", augmentedPath.c_str());
        m_parentBuildDir =
            std::make_unique<IRTest::ScopedEnv>("IRREDEN_BUILD_DIR", "/parent/build");
        m_service = std::make_unique<IREngine::ToolService>(m_root);
    }

    void TearDown() override {
        m_service.reset();
        m_parentBuildDir.reset();
        m_path.reset();
        m_wrapper.reset();
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }

    static void writeExecutable(const std::filesystem::path &path, const std::string &text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path);
        output << text;
        output.close();
        std::filesystem::permissions(
            path,
            std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read |
                std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace
        );
    }

    static std::optional<int>
    processId(const std::vector<std::string> &lines, std::string_view key) {
        for (const std::string &line : lines) {
            if (line.starts_with(key)) {
                return std::stoi(line.substr(key.size()));
            }
        }
        return std::nullopt;
    }

#ifndef _WIN32
    static bool processExists(int process) {
        errno = 0;
        return ::kill(process, 0) == 0 || errno != ESRCH;
    }

    static bool waitForProcessExit(int process) {
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (!processExists(process)) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }
#endif

    std::filesystem::path m_root;
    std::unique_ptr<IRTest::ScopedEnv> m_wrapper;
    std::unique_ptr<IRTest::ScopedEnv> m_path;
    std::unique_ptr<IRTest::ScopedEnv> m_parentBuildDir;
    std::unique_ptr<IREngine::ToolService> m_service;
};

TEST_F(ToolSpawnTest, PollAcrossChunks) {
    auto spawned = m_service->spawn({IREngine::ToolName::IR_BUILD, "Lines", ""});
    ASSERT_TRUE(spawned.has_value()) << spawned.error();
    std::vector<std::string> lines;
    for (int attempt = 0; attempt < 100 && !spawned->exitCode().has_value(); ++attempt) {
        std::vector<std::string> next = spawned->poll();
        lines.insert(lines.end(), next.begin(), next.end());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::vector<std::string> next = spawned->poll();
    lines.insert(lines.end(), next.begin(), next.end());
    EXPECT_EQ(lines, (std::vector<std::string>{"one", "two", "three"}));
    ASSERT_TRUE(spawned->exitCode().has_value());
    EXPECT_EQ(*spawned->exitCode(), 0);
}

TEST_F(ToolSpawnTest, KillReaps) {
    auto spawned = m_service->spawn({IREngine::ToolName::IR_BUILD, "Sleep", ""});
    ASSERT_TRUE(spawned.has_value()) << spawned.error();
    std::vector<std::string> lines;
    for (int attempt = 0; attempt < 100 && lines.size() < 2; ++attempt) {
        std::vector<std::string> next = spawned->poll();
        lines.insert(lines.end(), next.begin(), next.end());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto start = std::chrono::steady_clock::now();
    spawned->kill();
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(3));
    EXPECT_TRUE(spawned->exitCode().has_value());
    EXPECT_FALSE(m_service->live());
#ifndef _WIN32
    ASSERT_TRUE(processId(lines, "parent:").has_value());
    ASSERT_TRUE(processId(lines, "child:").has_value());
    EXPECT_TRUE(waitForProcessExit(*processId(lines, "parent:")));
    EXPECT_TRUE(waitForProcessExit(*processId(lines, "child:")));
#endif
}

TEST_F(ToolSpawnTest, HandleDestructionLeavesTheChildRunning) {
    {
        auto spawned = m_service->spawn({IREngine::ToolName::IR_BUILD, "Sleep", ""});
        ASSERT_TRUE(spawned.has_value()) << spawned.error();
    }
    EXPECT_TRUE(m_service->live());
    m_service->shutdown();
}

TEST_F(ToolSpawnTest, BoundsOutputAndRejectsStaleHandles) {
    auto first = m_service->spawn({IREngine::ToolName::IR_BUILD, "Flood", ""});
    ASSERT_TRUE(first.has_value()) << first.error();
    for (int attempt = 0; attempt < 200 && !first->exitCode().has_value(); ++attempt) {
        m_service->tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(first->exitCode().has_value());
    EXPECT_EQ(first->poll().size(), 2048);
    EXPECT_EQ(first->droppedLines(), 10);

    auto second = m_service->spawn({IREngine::ToolName::IR_BUILD, "Lines", ""});
    ASSERT_TRUE(second.has_value()) << second.error();
    EXPECT_THROW(first->poll(), std::runtime_error);
    second->kill();
}

TEST_F(ToolSpawnTest, UsesRootAsCwdAndSuppliesBuildDirectory) {
    auto spawned = m_service->spawn({IREngine::ToolName::IR_BUILD, "Env", ""});
    ASSERT_TRUE(spawned.has_value()) << spawned.error();
    for (int attempt = 0; attempt < 100 && !spawned->exitCode().has_value(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const std::vector<std::string> lines = spawned->poll();
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(
        std::filesystem::weakly_canonical(lines[0]),
        std::filesystem::weakly_canonical(m_root)
    );
    EXPECT_EQ(
        std::filesystem::weakly_canonical(lines[1]),
        std::filesystem::weakly_canonical(m_root / "build")
    );
}

TEST_F(ToolSpawnTest, ResolvesInRootSymlinksAndRemovesBuildDirectoryForEditor) {
#ifdef _WIN32
    GTEST_SKIP() << "POSIX stub executable test";
#else
    std::error_code linkError;
    std::filesystem::create_directory_symlink(
        m_root / "creations" / "inside",
        m_root / "creations" / "link",
        linkError
    );
    if (linkError) {
        GTEST_SKIP() << "could not create an in-root symlink: " << linkError.message();
    }
    auto spawned = m_service->spawn({IREngine::ToolName::EDITOR, "", "creations/link"});
    ASSERT_TRUE(spawned.has_value()) << spawned.error();
    for (int attempt = 0; attempt < 100 && !spawned->exitCode().has_value(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const std::vector<std::string> lines = spawned->poll();
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(
        std::filesystem::weakly_canonical(lines[0]),
        std::filesystem::weakly_canonical(m_root / "creations" / "inside")
    );
    EXPECT_EQ(lines[1], "unset");
#endif
}

TEST_F(ToolSpawnTest, ServiceDestructionKillsTheChildTree) {
    auto service = std::make_unique<IREngine::ToolService>(m_root);
    auto spawned = service->spawn({IREngine::ToolName::IR_BUILD, "Sleep", ""});
    ASSERT_TRUE(spawned.has_value()) << spawned.error();
    std::vector<std::string> lines;
    for (int attempt = 0; attempt < 100 && lines.size() < 2; ++attempt) {
        std::vector<std::string> next = spawned->poll();
        lines.insert(lines.end(), next.begin(), next.end());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto start = std::chrono::steady_clock::now();
    service.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(3));
#ifndef _WIN32
    ASSERT_TRUE(processId(lines, "parent:").has_value());
    ASSERT_TRUE(processId(lines, "child:").has_value());
    EXPECT_TRUE(waitForProcessExit(*processId(lines, "parent:")));
    EXPECT_TRUE(waitForProcessExit(*processId(lines, "child:")));
#endif
}

TEST_F(ToolSpawnTest, Refusals) {
    EXPECT_FALSE(m_service->spawn({static_cast<IREngine::ToolName>(99), "", ""}).has_value());
    EXPECT_FALSE(m_service->spawn({IREngine::ToolName::IR_BUILD, "-x", ""}).has_value());
    EXPECT_FALSE(m_service->spawn({IREngine::ToolName::IR_BUILD, "--build-dir", ""}).has_value());
    EXPECT_FALSE(m_service->spawn({IREngine::ToolName::IR_BUILD, "a/b", ""}).has_value());
    EXPECT_FALSE(m_service->spawn({IREngine::ToolName::IR_BUILD, "IR*", ""}).has_value());
    EXPECT_FALSE(m_service->spawn({IREngine::ToolName::IR_BUILD, "NUL", ""}).has_value());
    EXPECT_FALSE(
        m_service->spawn({IREngine::ToolName::IR_BUILD, std::string{"IR\0Demo", 7}, ""}).has_value()
    );
    EXPECT_FALSE(m_service->spawn({IREngine::ToolName::EDITOR, "", "/etc"}).has_value());
    EXPECT_FALSE(m_service->spawn({IREngine::ToolName::EDITOR, "", "C:\\x"}).has_value());
    EXPECT_FALSE(m_service->spawn({IREngine::ToolName::EDITOR, "", "\\\\host\\s"}).has_value());
    EXPECT_FALSE(
        m_service->spawn({IREngine::ToolName::OPEN_FOLDER, "", "creations/../.."}).has_value()
    );
    const std::filesystem::path outside = m_root.string() + "2";
    std::filesystem::create_directories(outside);
    std::error_code linkError;
    std::filesystem::create_directory_symlink(outside, m_root / "creations" / "escape", linkError);
    if (!linkError) {
        EXPECT_FALSE(
            m_service->spawn({IREngine::ToolName::OPEN_FOLDER, "", "creations/escape"}).has_value()
        );
    }

    auto first = m_service->spawn({IREngine::ToolName::IR_BUILD, "Sleep", ""});
    ASSERT_TRUE(first.has_value()) << first.error();
    auto second = m_service->spawn({IREngine::ToolName::IR_BUILD, "Lines", ""});
    ASSERT_FALSE(second.has_value());
    EXPECT_NE(second.error().find("already running"), std::string::npos);
    first->kill();

    IREngine::ToolService withoutRoot;
    EXPECT_FALSE(withoutRoot.spawn({IREngine::ToolName::IR_BUILD, "Lines", ""}).has_value());
    EXPECT_THROW(m_service->setRoot(m_root), std::invalid_argument);
    std::filesystem::remove_all(outside);
}

} // namespace

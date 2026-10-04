#include <gtest/gtest.h>
#include <irreden/world/config.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

using IRWindow::WindowMode;

// `window_mode` rides the same config < override precedence as the worker
// pool: the creation's config.lua spelling is parsed through
// IRWindow::parseWindowMode, and a resolved `--window-mode` / IR_WINDOW_MODE
// value replaces it. A spelling outside the set is a config error, not a
// silent fallback to normal — the one way a fleet host would otherwise end
// up with a demo on screen.
class WorldConfigWindowModeTest : public testing::Test {
  protected:
    void TearDown() override {
        std::error_code error;
        std::filesystem::remove(m_configFile, error);
    }

    const char *writeConfig(const std::string &windowModeLine) {
        m_configFile = std::filesystem::temp_directory_path() / "ir_world_config_window_mode.lua";
        std::ofstream out{m_configFile};
        out << "config = {\n" << windowModeLine << "\n}\n";
        m_configPath = m_configFile.string();
        return m_configPath.c_str();
    }

    std::filesystem::path m_configFile;
    std::string m_configPath;
};

TEST_F(WorldConfigWindowModeTest, DefaultsToNormal) {
    IREngine::WorldConfig config{writeConfig("init_window_width = 640,")};
    EXPECT_EQ(static_cast<WindowMode>(config["window_mode"].get_enum()), WindowMode::NORMAL);
}

TEST_F(WorldConfigWindowModeTest, ConfigSpellingParses) {
    IREngine::WorldConfig config{writeConfig("window_mode = \"hidden\",")};
    EXPECT_EQ(static_cast<WindowMode>(config["window_mode"].get_enum()), WindowMode::HIDDEN);
}

TEST_F(WorldConfigWindowModeTest, OverrideReplacesTheConfigValue) {
    IREngine::WorldConfig config{
        writeConfig("window_mode = \"hidden\","),
        nullptr,
        std::nullopt,
        WindowMode::BACKGROUND
    };
    EXPECT_EQ(static_cast<WindowMode>(config["window_mode"].get_enum()), WindowMode::BACKGROUND);
}

TEST_F(WorldConfigWindowModeTest, AbsentOverrideKeepsTheConfigValue) {
    IREngine::WorldConfig
        config{writeConfig("window_mode = \"background\","), nullptr, std::nullopt, std::nullopt};
    EXPECT_EQ(static_cast<WindowMode>(config["window_mode"].get_enum()), WindowMode::BACKGROUND);
}

TEST_F(WorldConfigWindowModeTest, RejectsASpellingOutsideTheSet) {
    // IR_ASSERT throws std::runtime_error in debug builds; the test binary is
    // built debug, so the parse failure is the exception, not a process death.
    const char *path = writeConfig("window_mode = \"minimized\",");
    EXPECT_THROW(IREngine::WorldConfig config{path}, std::runtime_error);
}

} // namespace

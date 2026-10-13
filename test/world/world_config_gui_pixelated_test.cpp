#include <gtest/gtest.h>
#include <irreden/world/config.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

// `gui_pixelated` selects which composite draws the GUI canvas; `World` hands
// the parsed value to `IRPrefab::GuiCanvas::setPixelated`. The screen
// composite is the default, so a config that never names the key must read
// false.
class WorldConfigGuiPixelatedTest : public testing::Test {
  protected:
    void TearDown() override {
        std::error_code error;
        std::filesystem::remove(m_configFile, error);
    }

    const char *writeConfig(const std::string &line) {
        m_configFile = std::filesystem::temp_directory_path() / "ir_world_config_gui_pixelated.lua";
        std::ofstream out{m_configFile};
        out << "config = {\n" << line << "\n}\n";
        m_configPath = m_configFile.string();
        return m_configPath.c_str();
    }

    std::filesystem::path m_configFile;
    std::string m_configPath;
};

TEST_F(WorldConfigGuiPixelatedTest, DefaultsToTheScreenComposite) {
    IREngine::WorldConfig config{writeConfig("init_window_width = 640,")};
    EXPECT_FALSE(config["gui_pixelated"].get_boolean());
}

TEST_F(WorldConfigGuiPixelatedTest, ConfigValueParses) {
    IREngine::WorldConfig config{writeConfig("gui_pixelated = true,")};
    EXPECT_TRUE(config["gui_pixelated"].get_boolean());
}

} // namespace

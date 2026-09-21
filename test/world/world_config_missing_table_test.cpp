#include <gtest/gtest.h>

#include <irreden/world/config.hpp>

#include <filesystem>
#include <fstream>

namespace {

// `WorldConfig` is the one `LuaScript::getTable("config")` caller that hands
// the result straight to `LuaConfig::parse`. A missing global comes back as an
// invalid table, whose `operator[]` indexes a null `lua_State*`, so this call
// site falls back to the entry defaults rather than forwarding it. A
// `config.lua` carrying only creation-owned tables is the reachable input.
class WorldConfigMissingTableTest : public testing::Test {
  protected:
    void SetUp() override {
        m_configFile =
            std::filesystem::temp_directory_path() / "ir_world_config_no_config_table.lua";
        std::ofstream out{m_configFile};
        out << "-- Declares no `config` global.\nunrelated = { init_window_width = 640 }\n";
    }

    void TearDown() override {
        std::error_code error;
        std::filesystem::remove(m_configFile, error);
    }

    std::filesystem::path m_configFile;
};

TEST_F(WorldConfigMissingTableTest, MissingConfigTableKeepsDefaults) {
    IREngine::WorldConfig config{m_configFile.string().c_str()};

    EXPECT_EQ(config["init_window_width"].get_integer(), 1920);
    EXPECT_EQ(config["init_window_height"].get_integer(), 1080);
    EXPECT_FALSE(config["fullscreen"].get_boolean());
}

} // namespace

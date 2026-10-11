#include <gtest/gtest.h>

#include <irreden/world/config.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

class WorldConfigAudioMonitorTest : public testing::Test {
  protected:
    void TearDown() override {
        std::error_code error;
        std::filesystem::remove(m_configFile, error);
        std::filesystem::remove(m_presetFile, error);
    }

    const char *
    write(const std::filesystem::path &path, const std::string &body, std::string &pathStorage) {
        std::ofstream out{path};
        out << "config = {\n" << body << "\n}\n";
        pathStorage = path.string();
        return pathStorage.c_str();
    }

    std::filesystem::path m_configFile =
        std::filesystem::temp_directory_path() / "ir_world_audio_monitor_config.lua";
    std::filesystem::path m_presetFile =
        std::filesystem::temp_directory_path() / "ir_world_audio_monitor_preset.lua";
    std::string m_configPath;
    std::string m_presetPath;
};

TEST_F(WorldConfigAudioMonitorTest, DefaultsAreDisabledAndUseTheDefaultOutput) {
    IREngine::WorldConfig config{write(m_configFile, "init_window_width = 640,", m_configPath)};

    EXPECT_FALSE(config["video_capture_audio_monitor_enabled"].get_boolean());
    EXPECT_TRUE(config["video_capture_audio_monitor_device_name"].get_string().empty());
}

TEST_F(WorldConfigAudioMonitorTest, PresetOverlaysMonitorFieldsOnly) {
    const char *configPath =
        write(m_configFile, "video_capture_audio_sample_rate = 44100,", m_configPath);
    const char *presetPath = write(
        m_presetFile,
        "video_capture_audio_monitor_enabled = true,\n"
        "video_capture_audio_monitor_device_name = \"Studio Output\",",
        m_presetPath
    );

    IREngine::WorldConfig config{configPath, presetPath};

    EXPECT_TRUE(config["video_capture_audio_monitor_enabled"].get_boolean());
    EXPECT_EQ(config["video_capture_audio_monitor_device_name"].get_string(), "Studio Output");
    EXPECT_EQ(config["video_capture_audio_sample_rate"].get_integer(), 44'100);
}

} // namespace

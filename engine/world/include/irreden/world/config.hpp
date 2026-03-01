#ifndef CONFIG_H
#define CONFIG_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_script.hpp>
#include <irreden/ir_render.hpp>

#include <string>

using namespace IRMath;

namespace IREngine {

// TODO: https://chatgpt.com/c/67034198-ce68-8005-aa2c-d3a3e08d0d02
// Remove the interface and make this fully compile time.
class WorldConfig {
  public:
    WorldConfig(const char *luaConfigFile)
        : m_lua{luaConfigFile}
        , m_config{} {
        m_config.addEntry(
            "init_window_width",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(1920)
        );
        m_config.addEntry(
            "init_window_height",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(1080)
        );
        m_config.addEntry(
            "game_resolution_width",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(1920)
        );
        m_config.addEntry(
            "game_resolution_height",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(1080)
        );
        m_config.addEntry(
            "fit_mode",
            std::make_unique<IRScript::LuaValue<IRScript::ENUM, IRRender::FitMode>>(
                IRRender::FitMode::FIT,
                [](const std::string &enumString) {
                    if (enumString == "fit")
                        return IRRender::FitMode::FIT;
                    if (enumString == "stretch")
                        return IRRender::FitMode::STRETCH;
                    IR_ASSERT(false, "Invalid enum value for fit_mode");
                    return IRRender::FitMode::UNKNOWN;
                }
            )
        );
        m_config.addEntry(
            "fullscreen",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::BOOLEAN>>(false)
        );
        m_config.addEntry(
            "monitor_index",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(-1)
        );
        m_config.addEntry(
            "monitor_name",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::STRING>>("")
        );
        m_config.addEntry(
            "voxel_render_mode",
            std::make_unique<IRScript::LuaValue<IRScript::ENUM, IRRender::VoxelRenderMode>>(
                IRRender::VoxelRenderMode::SNAPPED,
                [](const std::string &enumString) {
                    if (enumString == "snapped")
                        return IRRender::VoxelRenderMode::SNAPPED;
                    if (enumString == "smooth")
                        return IRRender::VoxelRenderMode::SMOOTH;
                    IR_ASSERT(false, "Invalid enum value for voxel_render_mode");
                    return IRRender::VoxelRenderMode::SNAPPED;
                }
            )
        );
        m_config.addEntry(
            "voxel_render_subdivisions",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(1)
        );
        m_config.addEntry(
            "video_capture_output_file",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::STRING>>("capture.mp4")
        );
        m_config.addEntry(
            "video_capture_fps",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(60)
        );
        m_config.addEntry(
            "video_capture_bitrate",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(10'000'000)
        );
        m_config.addEntry(
            "video_capture_audio_input_enabled",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::BOOLEAN>>(false)
        );
        m_config.addEntry(
            "video_capture_audio_input_device_name",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::STRING>>("")
        );
        m_config.addEntry(
            "video_capture_audio_sample_rate",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(48'000)
        );
        m_config.addEntry(
            "video_capture_audio_channels",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(2)
        );
        m_config.addEntry(
            "video_capture_audio_bitrate",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(320'000)
        );
        m_config.addEntry(
            "video_capture_audio_mux_enabled",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::BOOLEAN>>(true)
        );
        m_config.addEntry(
            "video_capture_audio_wav_enabled",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::BOOLEAN>>(true)
        );
        m_config.addEntry(
            "video_capture_audio_sync_offset_ms",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::NUMBER>>(0.0)
        );
        m_config.addEntry(
            "screenshot_output_dir",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::STRING>>(
                "save_files/screenshots"
            )
        );
        m_config.addEntry(
            "start_updates_on_first_key_press",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::BOOLEAN>>(false)
        );
        m_config.addEntry(
            "start_recording_on_first_key_press",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::BOOLEAN>>(false)
        );
        m_config.addEntry(
            "profiling_enabled",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::BOOLEAN>>(true)
        );
        m_config.addEntry(
            "gui_scale",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(2)
        );
        sol::table configTable = m_lua.getTable("config");
        m_config.parse(configTable);
    }

    IRScript::ILuaValue &operator[](const std::string &key) {
        return m_config[key];
    }

  private:
    IRScript::LuaScript m_lua;
    IRScript::LuaConfig m_config;
};

}; // namespace IREngine

#endif /* CONFIG_H */

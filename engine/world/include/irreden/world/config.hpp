#ifndef CONFIG_H
#define CONFIG_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_script.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/job/job_manager.hpp>

#include <optional>
#include <string>

using namespace IRMath;

namespace IREngine {

// FUTURE: drop the runtime ILuaValue interface and make WorldConfig
// fully compile-time, with each entry typed via a constexpr key list.
class WorldConfig {
  public:
    /// @p presetFile (optional) is a second Lua file whose `config` table
    /// overlays @p luaConfigFile's: only the keys it carries change, so a
    /// per-run preset (`--config-preset`) can set two capture keys without
    /// restating the creation's whole config.
    ///
    /// @p workerThreadsOverride is the `--worker-threads` value, applied on
    /// top of both files so precedence reads defaults < config.lua < preset
    /// < command line. `std::nullopt` (an absent flag) leaves the configured
    /// `worker_thread_count` alone.
    WorldConfig(
        const char *luaConfigFile,
        const char *presetFile = nullptr,
        std::optional<int> workerThreadsOverride = std::nullopt
    )
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
            "subdivision_mode",
            std::make_unique<IRScript::LuaValue<IRScript::ENUM, IRRender::SubdivisionMode>>(
                IRRender::SubdivisionMode::FULL,
                [](const std::string &enumString) {
                    if (enumString == "none")
                        return IRRender::SubdivisionMode::NONE;
                    if (enumString == "position")
                        return IRRender::SubdivisionMode::POSITION_ONLY;
                    if (enumString == "full")
                        return IRRender::SubdivisionMode::FULL;
                    IR_ASSERT(false, "Invalid enum value for subdivision_mode");
                    return IRRender::SubdivisionMode::FULL;
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
        // Encoded frame size; both 0 = follow the render output resolution;
        // one non-zero derives the other from the render output's aspect.
        m_config.addEntry(
            "video_capture_output_width",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(0)
        );
        m_config.addEntry(
            "video_capture_output_height",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(0)
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
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(1)
        );
        m_config.addEntry(
            "hovered_trixel_visible",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::BOOLEAN>>(true)
        );
        m_config.addEntry(
            "gpu_stage_timing",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::BOOLEAN>>(false)
        );
        m_config.addEntry(
            "gpu_stage_timing_legacy",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::BOOLEAN>>(false)
        );
        m_config.addEntry(
            "entity_count_override",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(0)
        );
        m_config.addEntry(
            "worker_thread_count",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(-1)
        );
        m_config.addEntry(
            "max_update_ticks_per_frame",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(8)
        );
        sol::table configTable = m_lua.getTable("config");
        // A config file with no `config` global yields an invalid table, whose
        // `operator[]` would index a null `lua_State*`; an empty table takes
        // the same every-key-missing path and leaves the defaults standing.
        m_config.parse(configTable.valid() ? configTable : m_lua.lua().create_table());
        // A preset may carry only creation-owned tables (perf_grid's do), so a
        // missing `config` table is not a fault.
        if (presetFile != nullptr && presetFile[0] != '\0') {
            IRScript::LuaScript preset{presetFile};
            sol::table presetTable = preset.getTable("config");
            if (presetTable.valid()) {
                IRE_LOG_INFO("Config preset overlay: {}", presetFile);
                m_config.overlay(presetTable);
            }
        }
        applyWorkerThreadsOverride(workerThreadsOverride);
    }

    IRScript::ILuaValue &operator[](const std::string &key) {
        return m_config[key];
    }

  private:
    /// Replaces the parsed `worker_thread_count` with the command-line
    /// value. Re-adding the entry is the write path — `ILuaValue` is
    /// parse-only, and `addEntry` replaces by key.
    void applyWorkerThreadsOverride(std::optional<int> workerThreadsOverride) {
        if (!workerThreadsOverride.has_value()) {
            return;
        }
        const int requested = *workerThreadsOverride;
        if (requested < IRJob::JobManager::kAutoWorkerCount) {
            IRE_LOG_WARN(
                "Ignoring --worker-threads {}: below {} (auto); keeping worker_thread_count = {}",
                requested,
                IRJob::JobManager::kAutoWorkerCount,
                m_config["worker_thread_count"].get_integer()
            );
            return;
        }
        IRE_LOG_INFO("--worker-threads override: worker_thread_count = {}", requested);
        m_config.addEntry(
            "worker_thread_count",
            std::make_unique<IRScript::LuaValue<IRScript::LuaType::INTEGER>>(requested)
        );
    }

    IRScript::LuaScript m_lua;
    IRScript::LuaConfig m_config;
};

}; // namespace IREngine

#endif /* CONFIG_H */

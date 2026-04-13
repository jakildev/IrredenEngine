#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_audio.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/profile/profile_report.hpp>

#include <irreden/world.hpp>

#include <chrono>

namespace IREngine {

World::World(const char *configFileName)
    : m_worldConfig{configFileName}
    , m_IRGLFWWindow{
          ivec2(
              m_worldConfig["init_window_width"].get_integer(),
              m_worldConfig["init_window_height"].get_integer()
          ),
          m_worldConfig["fullscreen"].get_boolean(),
          m_worldConfig["monitor_index"].get_integer(),
          m_worldConfig["monitor_name"].get_string()
      }
    , m_entityManager{}
    , m_commandManager{}
    , m_systemManager{}
    , m_inputManager{}
    , m_renderingResourceManager{}
    , m_renderer{
          ivec2(
              m_worldConfig["game_resolution_width"].get_integer(),
              m_worldConfig["game_resolution_height"].get_integer()
          ),
          static_cast<IRRender::FitMode>(m_worldConfig["fit_mode"].get_enum())
      }
    , m_audioManager{}
    , m_timeManager{}
    , m_videoManager{}
    , m_lua{}
    , m_waitForFirstUpdateInput{
          m_worldConfig["start_updates_on_first_key_press"].get_boolean()
      }
    , m_startRecordingOnFirstInput{
          m_worldConfig["start_recording_on_first_key_press"].get_boolean()
      }
    , m_hasHandledFirstInput{false} {
    IRRender::setVoxelRenderMode(
        static_cast<IRRender::VoxelRenderMode>(m_worldConfig["voxel_render_mode"].get_enum())
    );
    IRRender::setVoxelRenderSubdivisions(m_worldConfig["voxel_render_subdivisions"].get_integer());
    IRRender::setGuiScale(m_worldConfig["gui_scale"].get_integer());
    IRRender::setHoveredTrixelVisible(m_worldConfig["hovered_trixel_visible"].get_boolean());
    IRProfile::CPUProfiler::instance().setEnabled(m_worldConfig["profiling_enabled"].get_boolean());
    IRRender::gpuStageTiming().enabled_ = m_worldConfig["gpu_stage_timing"].get_boolean();
    IRRender::ImageData icon{"data/images/irreden_engine_logo_v6_alpha.png"};
    GLFWimage iconGlfw{icon.width_, icon.height_, icon.data_};
    m_IRGLFWWindow.setWindowIcon(&iconGlfw);
    m_renderer.printRenderInfo();
    m_videoManager.configureCapture(
        m_worldConfig["video_capture_output_file"].get_string(),
        m_worldConfig["video_capture_fps"].get_integer(),
        m_worldConfig["video_capture_bitrate"].get_integer(),
        m_worldConfig["video_capture_audio_input_enabled"].get_boolean(),
        m_worldConfig["video_capture_audio_input_device_name"].get_string(),
        m_worldConfig["video_capture_audio_sample_rate"].get_integer(),
        m_worldConfig["video_capture_audio_channels"].get_integer(),
        m_worldConfig["video_capture_audio_bitrate"].get_integer(),
        m_worldConfig["video_capture_audio_mux_enabled"].get_boolean(),
        m_worldConfig["video_capture_audio_wav_enabled"].get_boolean(),
        m_worldConfig["video_capture_audio_sync_offset_ms"].get_number(),
        &IRAudio::getAudioCaptureSource()
    );
    m_videoManager.configureScreenshotOutputDir(
        m_worldConfig["screenshot_output_dir"].get_string()
    );
    IR_PROFILE_MAIN_THREAD;
    IRE_LOG_INFO("Initalized game world");
}

World::~World() {
    IRE_LOG_INFO("Clean shutdown complete.");
}

void World::setupLuaBindings(const std::vector<LuaBindingRegistration> &bindings) {
    for (const auto &bind : bindings) {
        bind(m_lua);
    }
}

void World::runScript(const char *fileName) {
    m_lua.scriptFile(fileName);
}

void World::gameLoop() {
    using Clock = std::chrono::steady_clock;
    try {
        start();
        if (m_waitForFirstUpdateInput) {
            // Prime render-facing state so paused mode shows initialized voxels.
            update();
        }
        while (!m_IRGLFWWindow.shouldClose()) {
            m_timeManager.beginMainLoop();

            Clock::time_point frameStart;
            if (m_frameTimingEnabled) {
                frameStart = Clock::now();
            }

            uint32_t updateTicksThisFrame = 0;
            while (m_timeManager.shouldUpdate()) {
                m_IRGLFWWindow.pollEvents();
                input();
                if (!m_hasHandledFirstInput && IRInput::hasAnyButtonPressedThisFrame()) {
                    m_hasHandledFirstInput = true;
                    if (m_startRecordingOnFirstInput && !m_videoManager.isRecording()) {
                        m_videoManager.toggleRecording();
                    }
                }

                if (m_waitForFirstUpdateInput && !m_hasHandledFirstInput) {
                    // Consume one fixed update slot while paused so render still advances and we
                    // don't accumulate a large catch-up update burst after unpausing.
                    m_timeManager.skipUpdate();
                    break;
                }

                update();
                ++updateTicksThisFrame;
            }
            render();

            if (m_frameTimingEnabled) {
                auto elapsed = Clock::now() - frameStart;
                float ms = std::chrono::duration<float, std::milli>(elapsed).count();
                m_frameTimesMs.push_back(ms);
                m_frameTotalUpdateTicks += updateTicksThisFrame;
                if (updateTicksThisFrame > m_frameMaxUpdateTicksPerFrame) {
                    m_frameMaxUpdateTicksPerFrame = updateTicksThisFrame;
                }
            }
        }
    } catch (...) {
        IRE_LOG_ERROR("Unhandled exception in game loop, running cleanup");
        end();
        throw;
    }
    end();
}

void World::input() {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_UPDATE);

    m_inputManager.tick();
    m_inputManager.advanceInputState(IRTime::Events::INPUT);
    m_audioManager.getMidiIn().tick();

    m_systemManager.executePipeline(IRTime::Events::INPUT);
}

void World::start() {
    m_timeManager.start();
    IRProfile::CPUProfiler::instance().mainThread();
}

void World::end() {
    buildAndWriteProfileReport();
    m_videoManager.shutdown();
    // Ensure component onDestroy hooks run while managers are still valid
    // (e.g. MIDI cleanup that sends NOTE_OFF on shutdown).
    m_entityManager.destroyAllEntities();
    IRProfile::CPUProfiler::instance().shutdown();
    IRProfile::shutdownLogging();
}

void World::update() {
    m_timeManager.beginEvent<IRTime::UPDATE>();
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_UPDATE);

    m_inputManager.advanceInputState(IRTime::Events::UPDATE);
    m_commandManager.executeUserKeyboardCommandsAll();
    m_commandManager.executeDeviceMidiCCCommandsAll();
    m_commandManager.executeDeviceMidiNoteCommandsAll();
    m_systemManager.executePipeline(IRTime::Events::UPDATE);
    m_entityManager.destroyMarkedEntities();
    m_videoManager.notifyFixedUpdate();
    m_timeManager.endEvent<IRTime::UPDATE>();
}

void World::render() {
    m_timeManager.beginEvent<IRTime::RENDER>();
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

    m_inputManager.advanceInputState(IRTime::Events::RENDER);
    m_renderer.beginFrame();
    m_renderer.renderFrame();
    m_videoManager.render();
    m_renderer.presentFrame();

    m_timeManager.endEvent<IRTime::RENDER>();
}

void World::enableFrameTiming(bool enabled) {
    m_frameTimingEnabled = enabled;
    m_systemManager.setTimingEnabled(enabled);
    if (enabled) {
        m_frameTimesMs.clear();
        m_frameTimesMs.reserve(1024);
        m_frameTotalUpdateTicks = 0;
        m_frameMaxUpdateTicksPerFrame = 0;
        m_systemManager.resetTimingStats();
    }
}

void World::buildAndWriteProfileReport() {
    if (!m_frameTimingEnabled) return;

    IRProfile::ProfileReport report;
    report.totalFrames_ = static_cast<uint32_t>(m_frameTimesMs.size());
    report.frameTimesMs_ = std::move(m_frameTimesMs);
    report.totalUpdateTicks_ = m_frameTotalUpdateTicks;
    report.maxUpdateTicksPerFrame_ = m_frameMaxUpdateTicksPerFrame;
    report.entityCount_ = IREntity::getLiveEntityCount();
    report.archetypeCount_ = static_cast<uint32_t>(m_entityManager.getArchetypeNodes().size());

    // Collect per-system timing, grouped by pipeline
    auto pipelineName = [](IRTime::Events e) -> const char * {
        switch (e) {
            case IRTime::Events::INPUT:  return "INPUT";
            case IRTime::Events::UPDATE: return "UPDATE";
            case IRTime::Events::RENDER: return "RENDER";
            default: return "OTHER";
        }
    };

    const auto &pipelines = m_systemManager.getPipelines();
    for (auto &[event, systemList] : pipelines) {
        for (auto systemId : systemList) {
            const auto &acc = m_systemManager.getTimingAccum(systemId);
            if (acc.callCount_ == 0) continue;
            IRProfile::SystemTimingEntry entry;
            entry.name_ = m_systemManager.getSystemName(systemId);
            entry.pipeline_ = pipelineName(event);
            entry.totalNs_ = acc.totalNs_;
            entry.minNs_ = acc.minNs_;
            entry.maxNs_ = acc.maxNs_;
            entry.callCount_ = acc.callCount_;
            entry.totalEntityCount_ = acc.totalEntityCount_;
            report.systemTimings_.push_back(std::move(entry));
        }
    }

    // Collect GPU stage timing if enabled
    auto &gpu = IRRender::gpuStageTiming();
    if (gpu.enabled_ && report.totalFrames_ > 0) {
        auto addGpuStage = [&](const char *name, float perFrameMs) {
            IRProfile::GpuStageEntry stage;
            stage.name_ = name;
            stage.totalMs_ = perFrameMs * static_cast<float>(report.totalFrames_);
            stage.maxMs_ = perFrameMs;  // Only per-frame snapshot available
            stage.sampleCount_ = report.totalFrames_;
            report.gpuStages_.push_back(std::move(stage));
        };
        addGpuStage("canvas_clear", gpu.canvasClearMs_);
        addGpuStage("voxel_compact", gpu.voxelCompactMs_);
        addGpuStage("voxel_stage_1", gpu.voxelStage1Ms_);
        addGpuStage("voxel_stage_2", gpu.voxelStage2Ms_);
        addGpuStage("shape_compact", gpu.shapeCompactMs_);
        addGpuStage("shape_pass_0", gpu.shapePass0Ms_);
        addGpuStage("shape_pass_1", gpu.shapePass1Ms_);
        addGpuStage("trixel_to_framebuffer", gpu.trixelToFbMs_);
        addGpuStage("entity_canvas_to_framebuffer", gpu.entityCanvasToFbMs_);
        addGpuStage("framebuffer_to_screen", gpu.fbToScreenMs_);
    }

    IRProfile::writeProfileReport(report, "save_files/profile_report.txt");
    IRE_LOG_INFO("Profile report written to save_files/profile_report.txt ({} frames)",
                 report.totalFrames_);
}

} // namespace IREngine

#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_audio.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/profile/profile_report.hpp>
#include <irreden/video/auto_screenshot.hpp>

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
    , m_lua{}
    , m_entityManager{}
    , m_commandManager{}
    , m_systemManager{}
    , m_jobManager{m_worldConfig["worker_thread_count"].get_integer()}
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
    , m_waitForFirstUpdateInput{
          m_worldConfig["start_updates_on_first_key_press"].get_boolean()
      }
    , m_startRecordingOnFirstInput{
          m_worldConfig["start_recording_on_first_key_press"].get_boolean()
      }
    , m_hasHandledFirstInput{false}
    , m_maxUpdateTicksPerFrame{static_cast<uint32_t>(
          m_worldConfig["max_update_ticks_per_frame"].get_integer()
      )} {
    auto iconHandle = IRRender::loadImageAsync("data/images/irreden_engine_logo_v6_alpha.png");
    IRRender::setSubdivisionMode(
        static_cast<IRRender::SubdivisionMode>(m_worldConfig["subdivision_mode"].get_enum())
    );
    IRRender::setVoxelRenderSubdivisions(m_worldConfig["voxel_render_subdivisions"].get_integer());
    IRRender::setGuiScale(m_worldConfig["gui_scale"].get_integer());
    IRRender::setHoveredTrixelVisible(m_worldConfig["hovered_trixel_visible"].get_boolean());
    IRProfile::CPUProfiler::instance().setEnabled(m_worldConfig["profiling_enabled"].get_boolean());
    IRRender::gpuStageTiming().enabled_ = m_worldConfig["gpu_stage_timing"].get_boolean();
    IRRender::gpuStageTiming().legacyFinishTiming_ =
        m_worldConfig["gpu_stage_timing_legacy"].get_boolean();
    auto iconData = iconHandle.take();
    GLFWimage iconGlfw{iconData.width_, iconData.height_, iconData.pixels_.data()};
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
    // T-225: size the EntityManager's per-worker deferred-mutation
    // staging vector now that JobManager exists. Slot 0 is main,
    // slots 1..N are IRJob worker threads, so the total is
    // `workerCount() + 1`.
    m_entityManager.resizeWorkerStaging(static_cast<std::size_t>(m_jobManager.workerCount() + 1));
    IR_PROFILE_MAIN_THREAD;
    IRE_LOG_INFO("Initalized game world");
}

World::~World() {
    // No-op after `end()` (which ran during `gameLoop()` while the GL context
    // was still live and already cleared the observers). On the `IREngine` path
    // this runs at the tail of `IREngine::gameLoop()`, which resets `g_world`
    // while the driver is still loaded; `end()` is still the real cleanup,
    // since it also covers the exception path and any owner driving `World`
    // directly. Releasing device resources from here instead would reach a
    // torn-down driver on any path that does destruct at process exit — MSYS2
    // unloads the GL driver first, so `glDeleteQueries` from the
    // GpuStageTimingObserver dtor hits dead driver state (#2031). Idempotent,
    // so it stays a safety net for a path that never ran the loop.
    m_systemManager.clearTickObservers();
    IRE_LOG_INFO("Clean shutdown complete.");
}

void World::setupLuaBindings(const std::vector<LuaBindingRegistration> &bindings) {
    sol::state &lua = m_lua.lua();
    sol::table ir = lua["ir"].valid() ? lua["ir"].get<sol::table>() : lua.create_named_table("ir");
    sol::table render = ir["render"].valid() ? ir["render"].get<sol::table>() : lua.create_table();
    ir["render"] = render;

    render["getFrameTimeBudgetMs"] = []() { return IRRender::kFrameTimeBudgetMs; };
    render["isGpuTimingEnabled"] = []() { return IRRender::gpuStageTiming().enabled_; };
    render["setGpuTimingEnabled"] = [](bool enabled) {
        IRRender::gpuStageTiming().enabled_ = enabled;
    };
    render["isCpuTimingEnabled"] = []() { return IRProfile::cpuFrameHistogram().enabled_; };
    render["setCpuTimingEnabled"] = [](bool enabled) {
        IRProfile::cpuFrameHistogram().enabled_ = enabled;
    };
    render["getCpuPassTimings"] = [&lua]() {
        sol::table out = lua.create_table();
        int index = 1;
        for (const auto &[name, stats] : IRProfile::cpuFrameHistogram().lastFrame()) {
            sol::table row = lua.create_table();
            row["name"] = name;
            row["ms"] = stats.totalMs_;
            row["maxMs"] = stats.maxMs_;
            row["count"] = stats.count_;
            out[index++] = row;
        }
        return out;
    };
    render["getCpuPassTiming"] = [](std::string_view name) {
        return IRProfile::cpuFrameHistogram().lastFrameMs(name);
    };
    render["getPassTimings"] = [&lua]() {
        sol::table out = lua.create_table();
        const auto &registry = IRRender::gpuStageRegistry();
        const auto &timing = IRRender::gpuStageTiming();
        int index = 1;
        for (const auto &info : registry) {
            sol::table row = lua.create_table();
            row["name"] = info.name_;
            const float ms = timing.*info.field_;
            const float budget = IRRender::budgetMsFor(info);
            row["ms"] = ms;
            row["budgetMs"] = budget;
            row["budgetShare"] = info.budgetShare_;
            row["overBudget"] = ms > budget;
            out[index++] = row;
        }
        return out;
    };
    // Unknown names return 0.0f — indistinguishable from a pass that
    // legitimately took 0ms. Callers that need to detect typos should
    // enumerate names via `getPassTimings` first. The registry is tiny,
    // so linear lookup is fine.
    render["getPassTiming"] = [](std::string_view name) {
        const auto &registry = IRRender::gpuStageRegistry();
        const auto &timing = IRRender::gpuStageTiming();
        for (const auto &info : registry) {
            if (info.name_ == name)
                return timing.*info.field_;
        }
        return 0.0f;
    };
    // Voxel cull diagnostic. Returns last-sampled visible + total
    // voxel counts plus the running average + max collected since the
    // last enableFrameTiming(true). Only populated while
    // gpu_stage_timing is enabled.
    render["getVoxelCullStats"] = [&lua]() {
        sol::table out = lua.create_table();
        const auto &timing = IRRender::gpuStageTiming();
        const auto &acc = IRRender::voxelCullAccumulator();
        out["visible"] = timing.visibleVoxelCount_;
        out["total"] = timing.totalVoxelCount_;
        out["samples"] = acc.sampleCount_;
        out["avgVisible"] =
            acc.sampleCount_ > 0 ? static_cast<double>(acc.visibleSum_) / acc.sampleCount_ : 0.0;
        out["avgTotal"] =
            acc.sampleCount_ > 0 ? static_cast<double>(acc.totalSum_) / acc.sampleCount_ : 0.0;
        out["maxVisible"] = acc.maxVisible_;
        out["maxTotal"] = acc.maxTotal_;
        return out;
    };

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
        if (IRVideo::isAutoCaptureActive()) {
            // Headless --auto-screenshot capture: advance the sim exactly one
            // UPDATE tick per render frame so per-tick animation (AUTO_SPIN,
            // etc.) is deterministic and not starved by the uncapped
            // (vsync-off) loop racing through the frame-counted capture window.
            m_timeManager.enableFixedStep();
        }
        if (m_waitForFirstUpdateInput) {
            // Prime render-facing state so paused mode shows initialized voxels.
            update();
        }
        while (!m_IRGLFWWindow.shouldClose()) {
            m_timeManager.beginMainLoop();
            m_timeManager.clampUpdateLag(m_maxUpdateTicksPerFrame);

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
    IR_PROFILE_SCOPE("input");

    m_inputManager.tick();
    m_inputManager.advanceInputState(IRTime::Events::INPUT);
    m_audioManager.getMidiIn().tick();
    m_audioManager.getAudioPlayback().tickPlayback();

    m_systemManager.executePipeline(IRTime::Events::INPUT);
}

void World::start() {
    // T-224: cross-system pipeline-group validation runs once after
    // every system + pipeline is registered, before the first tick.
    // FATALs on the first conflict, naming both systems + the
    // offending component. Single-system groups (every legacy
    // `registerPipeline` call) are trivially clean and short-circuit.
    m_systemManager.validateAllPipelineGroups();
    m_timeManager.start();
    IRProfile::CPUProfiler::instance().mainThread();
}

void World::end() {
    buildAndWriteProfileReport();
    // Release the GPU stage-timing observer's GL timestamp queries here, while
    // the render context is guaranteed live. The observer is program-bound and
    // only ~World() would otherwise destroy it — but that runs at static
    // destruction, after the GL driver/context may already be gone (#2031).
    // `clearTickObservers()` is idempotent, so the dtor's later call no-ops.
    m_systemManager.clearTickObservers();
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
    IR_PROFILE_SCOPE("update");

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
    IR_PROFILE_SCOPE("render");

    m_inputManager.advanceInputState(IRTime::Events::RENDER);
    m_renderer.beginFrame();
    m_renderer.renderFrame();
    m_videoManager.render();
    m_renderer.presentFrame();

    // Frame-aligned histogram swap: every IR_PROFILE_SCOPE that ran since the
    // previous render() landed in the "current" map; the HUD reads the
    // newly-swapped "last frame" map next frame, so display state is stable
    // across the entire next-frame INPUT/UPDATE/RENDER cycle.
    IRProfile::cpuFrameHistogram().endFrame();

    m_timeManager.endEvent<IRTime::RENDER>();
}

int World::entityCountOverride() {
    return m_worldConfig["entity_count_override"].get_integer();
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
        IRRender::computeLightVolumeTiming().reset();
        IRRender::voxelCullAccumulator().reset();
        IRRender::resetGpuStageAccumulators();
    }
}

void World::buildAndWriteProfileReport() {
    if (!m_frameTimingEnabled)
        return;

    IRProfile::ProfileReport report;
    report.totalFrames_ = static_cast<uint32_t>(m_frameTimesMs.size());
    report.frameTimesMs_ = std::move(m_frameTimesMs);
    report.totalUpdateTicks_ = m_frameTotalUpdateTicks;
    report.maxUpdateTicksPerFrame_ = m_frameMaxUpdateTicksPerFrame;
    report.entityCount_ = IREntity::getLiveEntityCount();
    report.archetypeCount_ = static_cast<uint32_t>(m_entityManager.getArchetypeNodes().size());
    const auto &lightVolumeTiming = IRRender::computeLightVolumeTiming();
    report.cpuPhases_.push_back(
        {"ComputeLightVolume::Clear",
         lightVolumeTiming.clear_.totalMs_,
         lightVolumeTiming.clear_.maxMs_,
         lightVolumeTiming.clear_.sampleCount_}
    );
    report.cpuPhases_.push_back(
        {"ComputeLightVolume::Populate",
         lightVolumeTiming.populate_.totalMs_,
         lightVolumeTiming.populate_.maxMs_,
         lightVolumeTiming.populate_.sampleCount_}
    );
    report.cpuPhases_.push_back(
        {"ComputeLightVolume::Upload",
         lightVolumeTiming.upload_.totalMs_,
         lightVolumeTiming.upload_.maxMs_,
         lightVolumeTiming.upload_.sampleCount_}
    );

    const auto &cull = IRRender::voxelCullAccumulator();
    report.voxelCullStats_.visibleSum_ = cull.visibleSum_;
    report.voxelCullStats_.totalSum_ = cull.totalSum_;
    report.voxelCullStats_.maxVisible_ = cull.maxVisible_;
    report.voxelCullStats_.maxTotal_ = cull.maxTotal_;
    report.voxelCullStats_.sampleCount_ = cull.sampleCount_;

    // Collect per-system timing, grouped by pipeline
    auto pipelineName = [](IRTime::Events e) -> const char * {
        switch (e) {
        case IRTime::Events::INPUT:
            return "INPUT";
        case IRTime::Events::UPDATE:
            return "UPDATE";
        case IRTime::Events::RENDER:
            return "RENDER";
        default:
            return "OTHER";
        }
    };

    const auto &pipelines = m_systemManager.getPipelines();
    for (auto &[event, systemList] : pipelines) {
        for (auto systemId : systemList) {
            const auto &acc = m_systemManager.getTimingAccum(systemId);
            if (acc.callCount_ == 0)
                continue;
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

    auto &gpu = IRRender::gpuStageTiming();
    if (gpu.enabled_ && report.totalFrames_ > 0) {
        // Drain the per-stage accumulator filled by `gpu_stage_timing_observer`
        // each frame (indexed parallel to `gpuStageRegistry()`). This is a real
        // running sum / min / max across every sampled frame — not the old
        // last-frame-snapshot approximation, which reported Avg == Max on every
        // stage because `GpuStageTiming::*Ms_` only retains the latest sample
        // (#1738). Stages with no resolved samples (no writer, or readback never
        // ready) stay at sampleCount_ == 0 and are skipped by the report writer.
        const auto &accumulators = IRRender::gpuStageAccumulators();
        const auto &registry = IRRender::gpuStageRegistry();
        for (std::size_t i = 0; i < registry.size(); ++i) {
            const auto &acc = accumulators[i];
            IRProfile::GpuStageEntry stage;
            stage.name_ = std::string(registry[i].name_);
            stage.totalMs_ = static_cast<float>(acc.sumMs_);
            stage.minMs_ = acc.minMs_;
            stage.maxMs_ = acc.maxMs_;
            stage.sampleCount_ = acc.sampleCount_;
            report.gpuStages_.push_back(std::move(stage));
        }
    }

    IRProfile::writeProfileReport(report, "save_files/profile_report.txt");
    IRE_LOG_INFO(
        "Profile report written to save_files/profile_report.txt ({} frames)",
        report.totalFrames_
    );
}

} // namespace IREngine

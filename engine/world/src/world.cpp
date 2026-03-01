#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_audio.hpp>

#include <irreden/world.hpp>

namespace IREngine {
// TODO: replace initalization constants with config file.

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
    IRProfile::CPUProfiler::instance().setEnabled(m_worldConfig["profiling_enabled"].get_boolean());
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
    try {
        start();
        if (m_waitForFirstUpdateInput) {
            // Prime render-facing state so paused mode shows initialized voxels.
            update();
        }
        while (!m_IRGLFWWindow.shouldClose()) {
            m_timeManager.beginMainLoop();

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
            }
            render();
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

    // TODO: Make this an event from timeManager after
    // making that more generic.
    m_inputManager.tick();
    m_audioManager.getMidiIn().tick();

    m_systemManager.executePipeline(IRTime::Events::INPUT);
}

void World::start() {
    m_timeManager.start();
    IRProfile::CPUProfiler::instance().mainThread();
}

void World::end() {
    m_videoManager.shutdown();
    // Ensure component onDestroy hooks run while managers are still valid
    // (e.g. MIDI cleanup that sends NOTE_OFF on shutdown).
    m_entityManager.destroyAllEntities();
}

void World::update() {
    m_timeManager.beginEvent<IRTime::UPDATE>();
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_UPDATE);

    m_commandManager.executeUserKeyboardCommandsAll();
    m_commandManager.executeDeviceMidiCCCommandsAll();
    m_commandManager.executeDeviceMidiNoteCommandsAll();
    m_systemManager.executePipeline(IRTime::Events::UPDATE);
    m_entityManager.destroyMarkedEntities();
    m_videoManager.notifyFixedUpdate();
    m_timeManager.endEvent<IRTime::UPDATE>();
}

void World::render() {
    // Possible oppertunity for promise style await here...
    m_timeManager.beginEvent<IRTime::RENDER>();
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

    m_inputManager.tickRender();
    m_renderer.tick();
    m_videoManager.render();

    m_timeManager.endEvent<IRTime::RENDER>();
}

} // namespace IREngine
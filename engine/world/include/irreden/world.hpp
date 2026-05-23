#ifndef WORLD_H
#define WORLD_H

#include <irreden/window/ir_glfw_window.hpp>
#include <irreden/input/input_manager.hpp>
#include <irreden/ir_command.hpp>
// #include <irreden/command/command_manager.hpp>
#include <irreden/system/system_manager.hpp>
#include <irreden/job/job_manager.hpp>
#include <irreden/render/render_manager.hpp>
#include <irreden/render/rendering_rm.hpp>
#include <irreden/ir_audio.hpp>
#include <irreden/audio/audio_manager.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/time/time_manager.hpp>
#include <irreden/video/video_manager.hpp>
#include <irreden/world/config.hpp>
#include <sol/sol.hpp>
#include <functional>
#include <vector>

namespace IREngine {

constexpr const char *kTestLuaConfig = "data/configs/default.irconf";

class World {
  public:
    using LuaBindingRegistration = std::function<void(IRScript::LuaScript &)>;
    World(const char *configFileName);
    virtual ~World();
    void gameLoop();
    void setupLuaBindings(const std::vector<LuaBindingRegistration> &bindings);
    void runScript(const char *fileName);
    void enableFrameTiming(bool enabled);
    int entityCountOverride();

    // void setPlayer(const IREntity::EntityId& player);
    // void setCameraPosition3D(const vec3& position);
  private:
    WorldConfig m_worldConfig;
    IRWindow::IRGLFWWindow m_IRGLFWWindow;
    // m_lua must lead the manager block: EntityManager's archetype columns
    // can hold sol::object refs (Lua-typed components, see T-100). Members
    // destruct in reverse declaration order, so sol::state has to outlive
    // EntityManager or those refs UAF on shutdown.
    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    // T-221: worker pool. Lives between World construction and
    // destruction; sets g_jobManager. No engine system schedules on
    // it yet (T-222 is the first consumer).
    IRJobs::JobManager m_jobManager;
    IRInput::InputManager m_inputManager;
    IRCommand::CommandManager m_commandManager;
    IRRender::RenderingResourceManager m_renderingResourceManager;
    IRRender::RenderManager m_renderer;
    IRAudio::AudioManager m_audioManager;
    IRTime::TimeManager m_timeManager;
    IRVideo::VideoManager m_videoManager;
    bool m_waitForFirstUpdateInput = false;
    bool m_startRecordingOnFirstInput = false;
    bool m_hasHandledFirstInput = false;

    // Agent-readable profiling: frame timing accumulation
    bool m_frameTimingEnabled = false;
    std::vector<float> m_frameTimesMs;
    uint32_t m_frameTotalUpdateTicks = 0;
    uint32_t m_frameMaxUpdateTicksPerFrame = 0;
    // adding to world for user should just be attaching things to world ecs
    // entity! I have tried this before btw but wasnt ready
    // EntityHandle m_worldEngine;
    // EntityHandle m_worldGame;
    void initEngineSystems();
    void initIROutputSystems();
    void initIRInputSystems();
    void initIRUpdateSystems();
    void initIRRenderSystems();
    void initEngineCommands();

    void input();
    void update();
    void start();
    void end();
    void render();
    void buildAndWriteProfileReport();
};

} // namespace IREngine

#endif /* WORLD_H */

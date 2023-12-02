/*
 * Project: Irreden Engine
 * File: ir_world.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_WORLD_H
#define IR_WORLD_H

#include <irreden/input/ir_glfw_window.hpp>
#include <irreden/input/input_manager.hpp>
#include <irreden/ir_ecs.hpp>
#include <irreden/ir_command.hpp>
// #include <irreden/command/command_manager.hpp>
#include <irreden/system/system_manager.hpp>
#include <irreden/render/render_manager.hpp>
#include <irreden/render/rendering_rm.hpp>
#include <irreden/ir_audio.hpp>
#include <irreden/audio/audio_manager.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/time/time_manager.hpp>

class IRWorld {

public:
    IRWorld(int &argc, char **argv);
    virtual ~IRWorld();
    void gameLoop();

    // void setPlayer(const IRECS::EntityId& player);
    // void setCameraPosition3D(const vec3& position);
private:
    IRInput::IRGLFWWindow m_IRGLFWWindow;
    IRECS::EntityManager m_entityManager;
    IRECS::SystemManager m_systemManager;
    IRInput::InputManager m_inputManager;
    IRCommand::CommandManager m_commandManager;
    IRRender::RenderingResourceManager m_renderingResourceManager;
    IRRender::RenderManager m_renderer;
    IRAudio::AudioManager m_audioManager;
    IRTime::TimeManager m_timeManager;
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
};

#endif /* IR_WORLD_H */

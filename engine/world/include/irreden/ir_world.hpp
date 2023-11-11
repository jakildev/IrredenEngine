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
#include <irreden/ir_ecs.hpp>
#include <irreden/ir_command.hpp>
// #include <irreden/command/command_manager.hpp>
#include <irreden/system/system_manager.hpp>
#include <irreden/render/renderer.hpp>
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

    // template <IRCommands::CommandNames commandName>
    // void bindEntityToCommand(EntityHandle entity)
    // {
    //     m_commandManager.bindEntityToCommand<commandName>(entity);
    // }

    // template <
    //     typename Function,
    //     typename... Args
    // >
    // int registerMidiNoteCommand(
    //     int device,
    //     InputTypes InputType,
    //     Function command,
    //     Args... fixedArgs
    // )
    // {
    //     return m_commandManager.registerMidiNoteCommand(
    //         device,
    //         InputType,
    //         command,
    //         fixedArgs...
    //     );
    // }

    // template <
    //     typename Function
    // >
    // int registerMidiCCCommand(
    //     int device,
    //     InputTypes inputType,
    //     unsigned char ccMessage,
    //     Function command
    // )
    // {
    //     return m_commandManager.registerMidiCCCommand(
    //         device,
    //         inputType,
    //         ccMessage,
    //         command
    //     );
    // }

    // template <typename Function>
    // int registerCommand(
    //     InputTypes InputType,
    //     int button,
    //     Function command
    // )
    // {
    //     return m_commandManager.registerCommand(
    //         InputType,
    //         button,
    //         command
    //     );
    // }

    void setPlayer(const IRECS::EntityId& player);
    void setCameraPosition3D(const vec3& position);

    template <typename... Components>
    std::vector<IRECS::EntityId> createEntitiesBatch(
        const std::vector<Components>&... components
    )
    {
        return m_entityManager.createEntitiesBatch(
            components...
        );
    }

    void addEntityToScene(
        IRECS::EntityHandle entity,
        IRECS::EntityHandle parent = IRECS::EntityHandle{0}
    );
private:

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

    // TODO: Remove these virtual commands and perhaps use a CRTP
    // or encapulation.
    virtual void initGameSystems() = 0;
    virtual void initGameEntities() = 0;

    IRInput::IRGLFWWindow m_IRGLFWWindow;
    IRECS::EntityManager m_entityManager;
    IRCommand::CommandManager m_commandManager;
    IRECS::SystemManager m_systemManager;
    IRRender::RenderingResourceManager m_renderingResourceManager;
    IRRender::RenderManager m_renderer;
    IRAudio::AudioManager m_audioManager;
    IRTime::TimeManager m_timeManager;
    // int m_velocitySystemId;

    void input();
    void update();
    void start();
    void end();
    void render();
};

#endif /* IR_WORLD_H */

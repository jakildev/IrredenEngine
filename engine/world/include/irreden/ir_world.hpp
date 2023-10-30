/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\world\ir_world.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_WORLD_H
#define IR_WORLD_Hjj

#include <irreden/input/ir_glfw_window.hpp>
#include <irreden/ecs/entity_manager.hpp>
// #include <irreden/command/command_manager.hpp>
#include <irreden/ecs/system_manager.hpp>
#include <irreden/render/renderer.hpp>
#include <irreden/render/rendering_rm.hpp>
#include <irreden/ir_audio.hpp>
#include <irreden/audio/audio_manager.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/time/time_manager.hpp>

#include <irreden/ecs/entity_handle.hpp>
#include <irreden/ecs/prefabs.hpp>

class IRWorld {

public:
    IRWorld(int &argc, char **argv);
    virtual ~IRWorld();

    void gameLoop();

    // template <
    //     PrefabTypes type,
    //     typename... Args
    // >
    // EntityHandle createPrefab(Args&&... args)
    // {
    //     return Prefab<type>::create(
    //         args...
    //     );
    // }

    // template <IRCommands::IRCommandNames commandName>
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
    //     IRInputTypes InputType,
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
    //     IRInputTypes inputType,
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
    // int registerUserCommand(
    //     IRInputTypes InputType,
    //     int button,
    //     Function command
    // )
    // {
    //     return m_commandManager.registerUserCommand(
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
        void initIRInputSystems();
        void initIRUpdateSystems();
        void initIRRenderSystems();
    void initEngineCommands();

    // TODO: Remove these virtual commands and perhaps use a CRTP
    // or encapulation.
    virtual void initGameSystems() = 0;
    virtual void initGameEntities() = 0;

    IRGLFW::IRGLFWWindow m_IRGLFWWindow;
    IRECS::EntityManager& m_entityManager;
    // IRCommands::CommandManager m_commandManager;
    IRECS::SystemManager& m_systemManager;
    IRRender::Renderer m_renderer;
    IRRender::RenderingResourceManager& m_renderingResourceManager;
    IRAudio::AudioManager& m_audioManager;
    IRTime::TimeManager& m_timeManager;

    void input();
    void update();
    void start();
    void end();
    void render();
};

#endif /* IR_WORLD_H */
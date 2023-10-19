/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\world\ir_world.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_WORLD_H
#define IR_WORLD_H

#include "../world/ir_glfw_window.hpp"
#include "../ecs/entity_manager.hpp"
#include "../commands/command_manager.hpp"
#include "../ecs/system_manager.hpp"
#include "../rendering/renderer.hpp"
#include "../rendering/rendering_rm.hpp"
#include "../audio/ir_audio.hpp"
#include "../audio/audio_manager.hpp"
#include "../time/ir_time.hpp"
#include "../time/time_manager.hpp"
#include "../commands/command_manager.hpp"

#include "../ecs/entity_handle.hpp"
#include "../ecs/prefabs.hpp"

class IRWorld {

public:
    IRWorld(int &argc, char **argv);
    virtual ~IRWorld();

    void gameLoop();

    template <
        PrefabTypes type,
        typename... Args
    >
    EntityHandle createPrefab(Args&&... args)
    {
        return Prefab<type>::create(
            args...
        );
    }

    template <IRCommands::IRCommandNames commandName>
    void bindEntityToCommand(EntityHandle entity)
    {
        m_commandManager.bindEntityToCommand<commandName>(entity);
    }

    template <
        typename Function,
        typename... Args
    >
    int registerMidiNoteCommand(
        int device,
        IRInputTypes InputType,
        Function command,
        Args... fixedArgs
    )
    {
        return m_commandManager.registerMidiNoteCommand(
            device,
            InputType,
            command,
            fixedArgs...
        );
    }

    template <
        typename Function
    >
    int registerMidiCCCommand(
        int device,
        IRInputTypes inputType,
        unsigned char ccMessage,
        Function command
    )
    {
        return m_commandManager.registerMidiCCCommand(
            device,
            inputType,
            ccMessage,
            command
        );
    }

    template <typename Function>
    int registerUserCommand(
        IRInputTypes InputType,
        int button,
        Function command
    )
    {
        return m_commandManager.registerUserCommand(
            InputType,
            button,
            command
        );
    }

    void setPlayer(const EntityId& player);
    void setCameraPosition3D(const vec3& position);

    template <typename... Components>
    std::vector<EntityId> createEntitiesBatch(
        const std::vector<Components>&... components
    )
    {
        return m_entityManager.createEntitiesBatch(
            components...
        );
    }

    template<IRSystemName systemName>
    IRSystem<systemName>* getSystem() {
        return m_systemManager.get<systemName>();
    }

    void addEntityToScene(
        EntityHandle entity,
        EntityHandle parent = EntityHandle{0}
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
    IRECS::EntityManager m_entityManager;
    IRCommands::CommandManager m_commandManager;
    IRECS::SystemManager m_systemManager;
    IRRendering::Renderer m_renderer;
    IRRendering::RenderingResourceManager m_renderingResourceManager;
    IRAudio::AudioManager m_audioManager;
    IRTime::TimeManager m_timeManager;

    void input();
    void update();
    void start();
    void end();
    void render();
};

#endif /* IR_WORLD_H */

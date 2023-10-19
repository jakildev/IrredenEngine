/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\world\global.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Simple global struct to hold non-owning pointers
// to larger management systems.

// TODO: Switch to a depencency injection system and eliminate
// need for this in the codebase.

#ifndef GLOBAL_H
#define GLOBAL_H

#include "ir_constants.hpp"
#include "../ecs/ir_ecs.hpp"

namespace IRECS {
    class EntityManager;
    class SystemManager;
}

namespace IRCommands {
    class CommandManager;
}

namespace IRRendering {
    class RenderingResourceManager;
    class Renderer;
}

namespace IRGLFW {
    class IRGLFWWindow;
}

namespace IRTime {
    class TimeManager;
}

class IRWorld;

struct Global {
    IRWorld* world_;
    IRECS::EntityManager* entityManager_;
    IRECS::SystemManager* systemManager_;
    IRTime::TimeManager* timeManager_;
    IRCommands::CommandManager* commandManager_;
    IRRendering::RenderingResourceManager* renderingResourceManager_;
};

extern Global global;


#endif /* GLOBAL_H */

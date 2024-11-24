#ifndef IR_ENGINE_H
#define IR_ENGINE_H

#include <irreden/world.hpp>

namespace IREngine {
    std::unique_ptr<World> g_world = nullptr;
    World& getWorld() {
        IR_ASSERT(
            g_world != nullptr,
            "World not initalized"
        );
        return *g_world;
    }

    void init(const char* configFileName = kTestLuaConfig) {
        g_world = std::make_unique<World>(configFileName);
        g_world->setupLuaBindings();
    }

    void runScript(const char* scriptFileName) {
        g_world->runScript(scriptFileName);
    }

    void gameLoop() {
        getWorld().gameLoop();
    }
}

#endif /* IR_ENGINE_H */

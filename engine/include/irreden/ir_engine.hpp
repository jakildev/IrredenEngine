#ifndef IR_ENGINE_H
#define IR_ENGINE_H

#include <irreden/world.hpp>
#include <irreden/ir_ecs.hpp>

namespace IREngine {
    std::unique_ptr<World> g_world = nullptr;
    World& getWorld() {
        IR_ASSERT(
            g_world != nullptr,
            "World not initalized"
        );
        return *g_world;
    }

    void init(int &argc, char **argv) {
        g_world = std::make_unique<World>(argc, argv);
    }

    void gameLoop() {
        getWorld().gameLoop();
    }
}

#endif /* IR_ENGINE_H */

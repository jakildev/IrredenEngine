#ifndef IR_ENGINE_H
#define IR_ENGINE_H

#include <irreden/ir_world.hpp>

namespace IREngine {
    std::unique_ptr<IRWorld> g_world = nullptr;
    IRWorld& getWorld() {
        IR_ASSERT(
            g_world != nullptr,
            "IRWorld not initalized"
        );
        return *g_world;
    }

    void init(int &argc, char **argv) {
        g_world = std::make_unique<IRWorld>(argc, argv);
    }

    void gameLoop() {
        getWorld().gameLoop();
    }
}

#endif /* IR_ENGINE_H */

#ifndef IR_ENGINE_H
#define IR_ENGINE_H

#include <irreden/world.hpp>
#include <functional>
#include <vector>

namespace IREngine {
    using LuaBindingRegistration =
        std::function<void(IRScript::LuaScript&)>;

    inline std::unique_ptr<World> g_world = nullptr;
    inline std::vector<LuaBindingRegistration> g_luaBindingRegistrations;
    inline World& getWorld() {
        IR_ASSERT(
            g_world != nullptr,
            "World not initalized"
        );
        return *g_world;
    }

    inline void registerLuaBindings(const LuaBindingRegistration& registration) {
        g_luaBindingRegistrations.push_back(registration);
    }

    inline void clearLuaBindings() {
        g_luaBindingRegistrations.clear();
    }

    inline void init(const char* configFileName = kTestLuaConfig) {
        g_world = std::make_unique<World>(configFileName);
        g_world->setupLuaBindings(g_luaBindingRegistrations);
    }

    inline void runScript(const char* scriptFileName) {
        g_world->runScript(scriptFileName);
    }

    inline void gameLoop() {
        getWorld().gameLoop();
    }
}

#endif /* IR_ENGINE_H */

#ifndef IR_ENGINE_H
#define IR_ENGINE_H

#include <irreden/world.hpp>
#include <filesystem>
#include <functional>
#include <vector>

namespace IREngine {
using LuaBindingRegistration = std::function<void(IRScript::LuaScript &)>;

inline std::unique_ptr<World> g_world = nullptr;
inline std::vector<LuaBindingRegistration> g_luaBindingRegistrations;
inline std::filesystem::path g_scriptsDir;

inline World &getWorld() {
    IR_ASSERT(g_world != nullptr, "World not initalized");
    return *g_world;
}

inline void registerLuaBindings(const LuaBindingRegistration &registration) {
    g_luaBindingRegistrations.push_back(registration);
}

inline void clearLuaBindings() {
    g_luaBindingRegistrations.clear();
}

// Bare filenames (e.g. "config.lua") resolve from the per-creation
// scripts directory.  Paths that already contain a directory component
// (e.g. "data/configs/default.irconf") resolve directly from cwd.
inline std::string resolveScriptPath(const char *filename) {
    std::filesystem::path p(filename);
    if (!g_scriptsDir.empty() && !p.has_parent_path()) {
        return (g_scriptsDir / filename).string();
    }
    return filename;
}

// Sets cwd to the executable's directory and derives a per-creation
// scripts directory from the executable's name (e.g. IRMidiPolyrhythm.exe
// looks for scripts in IRMidiPolyrhythm/).  All relative engine paths
// (shaders/, data/) resolve from the exe directory.
inline void init(const char *argv0, const char *configFileName = "config.lua") {
    auto exePath = std::filesystem::weakly_canonical(std::filesystem::path(argv0));
    auto exeDir = exePath.parent_path();
    std::filesystem::current_path(exeDir);
    g_scriptsDir = exeDir / exePath.stem();
    g_world = std::make_unique<World>(resolveScriptPath(configFileName).c_str());
    g_world->setupLuaBindings(g_luaBindingRegistrations);
}

inline void runScript(const char *scriptFileName) {
    g_world->runScript(resolveScriptPath(scriptFileName).c_str());
}

inline void gameLoop() {
    getWorld().gameLoop();
}
} // namespace IREngine

#endif /* IR_ENGINE_H */

#ifndef LUA_SIM_BINDINGS_H
#define LUA_SIM_BINDINGS_H

#include <irreden/common/sim_clock.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <cstdint>
#include <string>

namespace IRScript::detail {

// Exposes the sim-clock / cycle / timer / stopwatch service as the `IRSim` Lua
// table, mirroring the IR<Module> convention (IRSpatial, IRModifier, IRSystem).
// It is the service surface only — create + query + control. The advance
// systems (SIM_CLOCK_ADVANCE / CYCLE_BOUNDARY_DETECT / TIMER_FIRE) are composed
// into the UPDATE pipeline C++-side, the same way most prefab systems are not
// individually exposed to Lua pipeline assembly.
//
// Discrete events (cycle boundary / timer fired) are exposed as POLL functions
// (cycleBoundaryCrossed / timerFired) rather than registered callbacks: a Lua
// system reads them the tick they fire, matching the engine's
// events-as-components model. A callback/listener form is a follow-up if a
// real consumer needs it.
inline void bindSimApi(LuaScript &script) {
    sol::state &lua = script.lua();
    if (!lua["IRSim"].valid()) {
        lua["IRSim"] = lua.create_table();
    }
    sol::table sim = lua["IRSim"];

    // Clock.
    sim["tick"] = []() -> lua_Integer { return static_cast<lua_Integer>(IRSim::tick()); };
    sim["timeScale"] = []() -> double { return static_cast<double>(IRSim::timeScale()); };
    sim["isPaused"] = []() -> bool { return IRSim::isPaused(); };
    sim["setTimeScale"] = [](double scale) { IRSim::setTimeScale(static_cast<float>(scale)); };
    sim["pause"] = []() { IRSim::pause(); };
    sim["resume"] = []() { IRSim::resume(); };

    // Cycles.
    sim["createCycle"] =
        [](const std::string &name, lua_Integer period, sol::optional<lua_Integer> phaseOffset)
        -> lua_Integer {
        const std::uint64_t offset =
            phaseOffset ? static_cast<std::uint64_t>(*phaseOffset) : 0u;
        return static_cast<lua_Integer>(
            IRSim::createCycle(name, static_cast<std::uint64_t>(period), offset)
        );
    };
    sim["cycleNumber"] = [](const std::string &name) -> lua_Integer {
        return static_cast<lua_Integer>(IRSim::cycleNumber(name));
    };
    sim["cycleTickWithin"] = [](const std::string &name) -> lua_Integer {
        return static_cast<lua_Integer>(IRSim::cycleTickWithin(name));
    };
    sim["cycleFraction"] = [](const std::string &name) -> double {
        return static_cast<double>(IRSim::cycleFraction(name));
    };
    sim["cycleBoundaryCrossed"] = [](const std::string &name) -> bool {
        return IRSim::cycleBoundaryCrossed(name);
    };

    // Timers.
    sim["createTimer"] =
        [](const std::string &name, lua_Integer targetTick, sol::optional<lua_Integer> interval)
        -> lua_Integer {
        const std::uint64_t intervalTicks =
            interval ? static_cast<std::uint64_t>(*interval) : 0u;
        return static_cast<lua_Integer>(
            IRSim::createTimer(name, static_cast<std::uint64_t>(targetTick), intervalTicks)
        );
    };
    sim["timerActive"] = [](const std::string &name) -> bool {
        return IRSim::timerActive(name);
    };
    sim["timerTicksRemaining"] = [](const std::string &name) -> lua_Integer {
        return static_cast<lua_Integer>(IRSim::timerTicksRemaining(name));
    };
    sim["timerFraction"] = [](const std::string &name) -> double {
        return static_cast<double>(IRSim::timerFraction(name));
    };
    sim["timerFired"] = [](const std::string &name) -> bool {
        return IRSim::timerFired(name);
    };

    // Stopwatches.
    sim["createStopwatch"] = [](const std::string &name) -> lua_Integer {
        return static_cast<lua_Integer>(IRSim::createStopwatch(name));
    };
    sim["stopwatchElapsed"] = [](const std::string &name) -> lua_Integer {
        return static_cast<lua_Integer>(IRSim::stopwatchElapsed(name));
    };
    sim["stopwatchRunning"] = [](const std::string &name) -> bool {
        return IRSim::stopwatchRunning(name);
    };
    sim["stopwatchPause"] = [](const std::string &name) { IRSim::stopwatchPause(name); };
    sim["stopwatchResume"] = [](const std::string &name) { IRSim::stopwatchResume(name); };
    sim["stopwatchReset"] = [](const std::string &name) { IRSim::stopwatchReset(name); };
}

} // namespace IRScript::detail

#endif /* LUA_SIM_BINDINGS_H */

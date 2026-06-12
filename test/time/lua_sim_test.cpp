#include <gtest/gtest.h>

#include <cstdint>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/sim_clock.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/script/lua_sim_bindings.hpp>
#include <irreden/update/systems/system_cycle_boundary_detect.hpp>
#include <irreden/update/systems/system_sim_clock_advance.hpp>
#include <irreden/update/systems/system_timer_fire.hpp>

namespace {

// Fixture: a LuaScript with the IRSim service bound, plus the real
// SIM_CLOCK_ADVANCE / CYCLE_BOUNDARY_DETECT / TIMER_FIRE pipeline so Lua can
// drive and observe sim time. The advance is stepped C++-side (the pipeline);
// Lua reads/writes through the IRSim table. Stands in for #199's full Lua
// round-trip until save/load lands.
class LuaSimTest : public testing::Test {
  protected:
    LuaSimTest()
        : m_lua{}
        , m_entity_manager{}
        , m_system_manager{} {
        IRScript::detail::bindSimApi(m_lua);
        IRSim::clock(); // materialize the singleton before SIM_CLOCK_ADVANCE runs
        m_system_manager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::SIM_CLOCK_ADVANCE>(),
             IRSystem::createSystem<IRSystem::CYCLE_BOUNDARY_DETECT>(),
             IRSystem::createSystem<IRSystem::TIMER_FIRE>()}
        );
    }

    void advance(std::uint64_t ticks) {
        for (std::uint64_t i = 0; i < ticks; ++i) {
            m_system_manager.executePipeline(IRTime::Events::UPDATE);
        }
    }

    // m_lua first so it is destroyed last (sol handles outlive ECS cleanup).
    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(LuaSimTest, ClockTickReadableFromLua) {
    EXPECT_EQ(m_lua.lua().script("return IRSim.tick()").get<int>(), 0);
    advance(10);
    EXPECT_EQ(m_lua.lua().script("return IRSim.tick()").get<int>(), 10);
}

TEST_F(LuaSimTest, SetTimeScaleFromLua) {
    m_lua.lua().script("IRSim.setTimeScale(0.5)");
    advance(10);
    EXPECT_EQ(m_lua.lua().script("return IRSim.tick()").get<int>(), 5);
    EXPECT_FALSE(m_lua.lua().script("return IRSim.isPaused()").get<bool>());
    m_lua.lua().script("IRSim.pause()");
    EXPECT_TRUE(m_lua.lua().script("return IRSim.isPaused()").get<bool>());
}

TEST_F(LuaSimTest, CreateCycleAndReadFraction) {
    m_lua.lua().script("IRSim.createCycle('day', 100)");
    advance(25);
    EXPECT_NEAR(m_lua.lua().script("return IRSim.cycleFraction('day')").get<double>(), 0.25, 1e-5);
    EXPECT_EQ(m_lua.lua().script("return IRSim.cycleNumber('day')").get<int>(), 0);
    advance(75); // crosses the period boundary at sim tick 100
    EXPECT_TRUE(m_lua.lua().script("return IRSim.cycleBoundaryCrossed('day')").get<bool>());
    EXPECT_EQ(m_lua.lua().script("return IRSim.cycleNumber('day')").get<int>(), 1);
}

TEST_F(LuaSimTest, CycleBreakpointsAndSegmentFromLua) {
    // Create a 100-tick cycle with two breakpoints (at 25% and 75%) from Lua.
    m_lua.lua().script(R"(
        IRSim.createCycle('phase', 100)
        IRSim.cycleAddBreakpoint('phase', 0.25)
        IRSim.cycleAddBreakpoint('phase', 0.75)
    )");
    // Prime silently.
    m_system_manager.executePipeline(IRTime::Events::UPDATE);
    EXPECT_EQ(m_lua.lua().script("return IRSim.cycleSegment('phase')").get<int>(), 0);
    EXPECT_FALSE(m_lua.lua().script("return IRSim.cycleBoundaryCrossed('phase')").get<bool>());

    advance(24); // sim tick 25: crosses into segment 1
    EXPECT_EQ(m_lua.lua().script("return IRSim.cycleSegment('phase')").get<int>(), 1);
    EXPECT_TRUE(m_lua.lua().script("return IRSim.cycleBoundaryCrossed('phase')").get<bool>());

    advance(50); // sim tick 75: crosses into segment 2
    EXPECT_EQ(m_lua.lua().script("return IRSim.cycleSegment('phase')").get<int>(), 2);

    advance(25); // sim tick 100: period wrap, back to segment 0
    EXPECT_EQ(m_lua.lua().script("return IRSim.cycleSegment('phase')").get<int>(), 0);
    EXPECT_TRUE(m_lua.lua().script("return IRSim.cycleBoundaryCrossed('phase')").get<bool>());
}

TEST_F(LuaSimTest, TimerCreateAndPollFromLua) {
    m_lua.lua().script("IRSim.createTimer('reload', 50)");
    EXPECT_TRUE(m_lua.lua().script("return IRSim.timerActive('reload')").get<bool>());
    advance(49);
    EXPECT_FALSE(m_lua.lua().script("return IRSim.timerFired('reload')").get<bool>());
    advance(1); // sim tick 50: fires
    EXPECT_TRUE(m_lua.lua().script("return IRSim.timerFired('reload')").get<bool>());
    EXPECT_FALSE(m_lua.lua().script("return IRSim.timerActive('reload')").get<bool>());
}

TEST_F(LuaSimTest, StopwatchFromLua) {
    m_lua.lua().script("IRSim.createStopwatch('run')");
    advance(40);
    EXPECT_EQ(m_lua.lua().script("return IRSim.stopwatchElapsed('run')").get<int>(), 40);
    m_lua.lua().script("IRSim.stopwatchPause('run')");
    advance(20);
    EXPECT_EQ(m_lua.lua().script("return IRSim.stopwatchElapsed('run')").get<int>(), 40);
    m_lua.lua().script("IRSim.stopwatchReset('run')");
    EXPECT_EQ(m_lua.lua().script("return IRSim.stopwatchElapsed('run')").get<int>(), 0);
}

} // namespace

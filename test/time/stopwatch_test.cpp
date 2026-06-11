#include <gtest/gtest.h>

#include <cstdint>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_stopwatch.hpp>
#include <irreden/common/sim_clock.hpp>
#include <irreden/update/systems/system_sim_clock_advance.hpp>

namespace {

// Stopwatch elapsed is computed-on-read from the live sim tick, so the only
// system needed is SIM_CLOCK_ADVANCE — there is no per-frame stopwatch system
// (by design; see component_stopwatch.hpp).
class StopwatchTest : public testing::Test {
  protected:
    StopwatchTest()
        : m_entity_manager{}
        , m_system_manager{} {
        IRSim::clock();
        m_system_manager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::SIM_CLOCK_ADVANCE>()}
        );
    }

    void advance(std::uint64_t ticks) {
        for (std::uint64_t i = 0; i < ticks; ++i) {
            m_system_manager.executePipeline(IRTime::Events::UPDATE);
        }
    }

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(StopwatchTest, ElapsedAdvancesPausesResumesResets) {
    IRSim::createStopwatch("run");
    EXPECT_EQ(IRSim::stopwatchElapsed("run"), 0u);

    advance(100);
    EXPECT_EQ(IRSim::stopwatchElapsed("run"), 100u);
    EXPECT_TRUE(IRSim::stopwatchRunning("run"));

    IRSim::stopwatchPause("run");
    EXPECT_FALSE(IRSim::stopwatchRunning("run"));
    advance(50);
    EXPECT_EQ(IRSim::stopwatchElapsed("run"), 100u); // frozen while paused

    IRSim::stopwatchResume("run");
    advance(30);
    EXPECT_EQ(IRSim::stopwatchElapsed("run"), 130u);

    IRSim::stopwatchReset("run");
    EXPECT_EQ(IRSim::stopwatchElapsed("run"), 0u);
    advance(10);
    EXPECT_EQ(IRSim::stopwatchElapsed("run"), 10u);
}

TEST_F(StopwatchTest, CreatedAfterSimStartCountsFromCreation) {
    advance(500); // sim tick 500
    IRSim::createStopwatch("late"); // startTick_ snapshot == 500
    EXPECT_EQ(IRSim::stopwatchElapsed("late"), 0u);
    advance(40);
    EXPECT_EQ(IRSim::stopwatchElapsed("late"), 40u);
}

} // namespace

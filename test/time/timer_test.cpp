#include <gtest/gtest.h>

#include <cstdint>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_timer.hpp>
#include <irreden/common/sim_clock.hpp>
#include <irreden/update/systems/system_sim_clock_advance.hpp>
#include <irreden/update/systems/system_timer_fire.hpp>

namespace {

using IRComponents::C_Timer;

class TimerTest : public testing::Test {
  protected:
    TimerTest()
        : m_entity_manager{}
        , m_system_manager{} {
        IRSim::clock();
        m_system_manager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::SIM_CLOCK_ADVANCE>(),
             IRSystem::createSystem<IRSystem::TIMER_FIRE>()}
        );
    }

    int advanceCountingFires(IREntity::EntityId id, std::uint64_t ticks) {
        int fires = 0;
        for (std::uint64_t i = 0; i < ticks; ++i) {
            m_system_manager.executePipeline(IRTime::Events::UPDATE);
            if (IREntity::getComponent<C_Timer>(id).fired_) {
                ++fires;
            }
        }
        return fires;
    }

    void advance(std::uint64_t ticks) {
        for (std::uint64_t i = 0; i < ticks; ++i) {
            m_system_manager.executePipeline(IRTime::Events::UPDATE);
        }
    }

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(TimerTest, OneShotFiresExactlyOnce) {
    auto id = IRSim::createTimer("reload", 50);
    EXPECT_EQ(advanceCountingFires(id, 50), 1);
    EXPECT_FALSE(IREntity::getComponent<C_Timer>(id).active_);
    EXPECT_EQ(advanceCountingFires(id, 50), 0); // stays expired
}

TEST_F(TimerTest, RecurringFiresEachInterval) {
    auto id = IRSim::createTimer("metronome", 20, 20);
    EXPECT_EQ(advanceCountingFires(id, 100), 5); // 20, 40, 60, 80, 100
    EXPECT_TRUE(IREntity::getComponent<C_Timer>(id).active_);
}

TEST_F(TimerTest, TicksRemainingCountsDown) {
    IRSim::createTimer("reload", 50);
    EXPECT_EQ(IRSim::timerTicksRemaining("reload"), 50u);
    advance(30);
    EXPECT_EQ(IRSim::timerTicksRemaining("reload"), 20u);
    advance(20); // fires at tick 50
    EXPECT_EQ(IRSim::timerTicksRemaining("reload"), 0u);
    EXPECT_FALSE(IRSim::timerActive("reload"));
}

TEST_F(TimerTest, FractionProgressesLinearly) {
    IRSim::createTimer("reload", 100);
    advance(50);
    EXPECT_NEAR(IRSim::timerFraction("reload"), 0.5f, 0.01f);
    advance(50); // reaches target, fires
    EXPECT_NEAR(IRSim::timerFraction("reload"), 1.0f, 0.01f);
}

} // namespace

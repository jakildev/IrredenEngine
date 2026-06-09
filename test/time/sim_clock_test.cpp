#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_sim_clock.hpp>
#include <irreden/common/sim_clock.hpp>
#include <irreden/update/systems/system_sim_clock_advance.hpp>

namespace {

using IRComponents::C_SimClock;

// Exercises the sim clock in isolation: SIM_CLOCK_ADVANCE driving the
// C_SimClock singleton at various time scales, plus pause/resume and a
// determinism replay. The clock is the "sim tick" half of the two-clock model
// (the engine-tick half, IRTime::tick(), is not exercised here — it needs a
// live TimeManager, which the gtest harness does not boot).
class SimClockTest : public testing::Test {
  protected:
    SimClockTest()
        : m_entity_manager{}
        , m_system_manager{} {
        // Materialize the singleton so SIM_CLOCK_ADVANCE has a row to advance.
        IRSim::clock();
        m_system_manager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::SIM_CLOCK_ADVANCE>()}
        );
    }

    void advance(std::uint64_t engineTicks) {
        for (std::uint64_t i = 0; i < engineTicks; ++i) {
            m_system_manager.executePipeline(IRTime::Events::UPDATE);
        }
    }

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(SimClockTest, MonotonicAdvanceAtScaleOne) {
    EXPECT_EQ(IRSim::tick(), 0u);
    advance(1000);
    EXPECT_EQ(IRSim::tick(), 1000u);
}

TEST_F(SimClockTest, PauseFreezesThenResumeAndScale) {
    advance(100);
    EXPECT_EQ(IRSim::tick(), 100u);

    IRSim::pause();
    EXPECT_TRUE(IRSim::isPaused());
    advance(100);
    EXPECT_EQ(IRSim::tick(), 100u); // frozen while paused

    IRSim::resume();
    EXPECT_FALSE(IRSim::isPaused());
    advance(100);
    EXPECT_EQ(IRSim::tick(), 200u);

    IRSim::setTimeScale(0.5f);
    advance(100);
    EXPECT_EQ(IRSim::tick(), 250u); // +50 at half rate
}

TEST_F(SimClockTest, SlowMoHalfRate) {
    IRSim::setTimeScale(0.5f);
    advance(100);
    EXPECT_EQ(IRSim::tick(), 50u);
}

TEST_F(SimClockTest, FastForwardDoubleRate) {
    IRSim::setTimeScale(2.0f);
    advance(100);
    EXPECT_EQ(IRSim::tick(), 200u);
}

TEST_F(SimClockTest, NegativeScaleClampsToPaused) {
    IRSim::setTimeScale(-3.0f);
    EXPECT_TRUE(IRSim::isPaused());
    advance(50);
    EXPECT_EQ(IRSim::tick(), 0u);
}

// The scalar-only clock struct survives a raw byte round-trip unchanged — the
// in-memory stand-in for #199 save/load until that integration lands.
TEST_F(SimClockTest, ClockStateByteExactRoundTrip) {
    IRSim::setTimeScale(0.5f);
    advance(137);
    const C_SimClock original = IRSim::clock();

    unsigned char buffer[sizeof(C_SimClock)];
    std::memcpy(buffer, &original, sizeof(C_SimClock));
    C_SimClock restored{};
    std::memcpy(&restored, buffer, sizeof(C_SimClock));

    EXPECT_EQ(std::memcmp(&original, &restored, sizeof(C_SimClock)), 0);
    EXPECT_EQ(restored.tickCount_, original.tickCount_);
    EXPECT_FLOAT_EQ(restored.timeScale_, original.timeScale_);
    EXPECT_FLOAT_EQ(restored.subTickAccum_, original.subTickAccum_);
}

// Restoring a snapshot and replaying the same span reproduces the same state —
// advancement is a pure function of (snapshot, ticks), including the
// fractional-scale sub-tick remainder.
TEST_F(SimClockTest, ReplayFromSnapshotIsDeterministic) {
    IRSim::setTimeScale(0.5f);
    advance(137);
    const C_SimClock snapshot = IRSim::clock();

    advance(213);
    const C_SimClock runA = IRSim::clock();

    IRSim::clock() = snapshot; // restore in place
    advance(213);
    const C_SimClock runB = IRSim::clock();

    EXPECT_EQ(runA.tickCount_, runB.tickCount_);
    EXPECT_FLOAT_EQ(runA.timeScale_, runB.timeScale_);
    EXPECT_FLOAT_EQ(runA.subTickAccum_, runB.subTickAccum_);
}

} // namespace

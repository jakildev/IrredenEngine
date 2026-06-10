#include <gtest/gtest.h>

#include <cstdint>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_cycle.hpp>
#include <irreden/common/sim_clock.hpp>
#include <irreden/update/systems/system_cycle_boundary_detect.hpp>
#include <irreden/update/systems/system_sim_clock_advance.hpp>

namespace {

using IRComponents::C_Cycle;

// SIM_CLOCK_ADVANCE then CYCLE_BOUNDARY_DETECT, the real pipeline order: the
// boundary detector reads the tick the advance system just produced.
class CycleTest : public testing::Test {
  protected:
    CycleTest()
        : m_entity_manager{}
        , m_system_manager{} {
        IRSim::clock();
        m_system_manager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::SIM_CLOCK_ADVANCE>(),
             IRSystem::createSystem<IRSystem::CYCLE_BOUNDARY_DETECT>()}
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

TEST_F(CycleTest, BoundaryFiresOncePerPeriod) {
    auto id = IRSim::createCycle("day", 100);

    int boundaries = 0;
    std::uint64_t lastFrom = 0;
    std::uint64_t lastTo = 0;
    for (std::uint64_t i = 0; i < 250; ++i) {
        m_system_manager.executePipeline(IRTime::Events::UPDATE);
        const C_Cycle &cycle = IREntity::getComponent<C_Cycle>(id);
        if (cycle.boundaryCrossed_) {
            ++boundaries;
            lastFrom = cycle.fromCycle_;
            lastTo = cycle.toCycle_;
        }
    }
    EXPECT_EQ(boundaries, 2); // crossings at sim tick 100 and 200
    EXPECT_EQ(lastFrom, 1u);
    EXPECT_EQ(lastTo, 2u);
}

TEST_F(CycleTest, NoSpuriousBoundaryBeforeFirstPeriod) {
    auto id = IRSim::createCycle("day", 100);
    for (std::uint64_t i = 0; i < 99; ++i) {
        m_system_manager.executePipeline(IRTime::Events::UPDATE);
        EXPECT_FALSE(IREntity::getComponent<C_Cycle>(id).boundaryCrossed_);
    }
}

TEST_F(CycleTest, FractionProgressesAndWraps) {
    IRSim::createCycle("day", 100);

    advance(25);
    EXPECT_NEAR(IRSim::cycleFraction("day"), 0.25f, 1e-5f);
    EXPECT_EQ(IRSim::cycleNumber("day"), 0u);
    EXPECT_EQ(IRSim::cycleTickWithin("day"), 25u);

    advance(75); // now at sim tick 100
    EXPECT_NEAR(IRSim::cycleFraction("day"), 0.0f, 1e-5f); // wraps
    EXPECT_EQ(IRSim::cycleNumber("day"), 1u);
    EXPECT_EQ(IRSim::cycleTickWithin("day"), 0u);
}

TEST_F(CycleTest, PhaseOffsetShiftsBoundary) {
    // A 100-tick cycle offset by 40 crosses its boundary at sim tick 60, 160…
    IRSim::createCycle("shifted", 100, 40);
    advance(60);
    EXPECT_EQ(IRSim::cycleNumber("shifted"), 1u); // (60 + 40) / 100 == 1
    EXPECT_EQ(IRSim::cycleTickWithin("shifted"), 0u);
}

TEST_F(CycleTest, MidSimCreationDoesNotFireSpuriousBoundary) {
    advance(150); // sim already past one period
    auto id = IRSim::createCycle("late", 100);

    m_system_manager.executePipeline(IRTime::Events::UPDATE); // sim tick 151: primes silently
    EXPECT_FALSE(IREntity::getComponent<C_Cycle>(id).boundaryCrossed_);

    int boundaries = 0;
    for (std::uint64_t i = 0; i < 60; ++i) { // ticks 152..211
        m_system_manager.executePipeline(IRTime::Events::UPDATE);
        if (IREntity::getComponent<C_Cycle>(id).boundaryCrossed_) {
            ++boundaries;
        }
    }
    EXPECT_EQ(boundaries, 1); // only the real tick-200 crossing
}

TEST_F(CycleTest, BreakpointsFireOnSegmentTransition) {
    // Period of 100 ticks, breakpoints at 25% and 75%: 3 segments.
    // Segment 0: [0,25), segment 1: [25,75), segment 2: [75,100).
    auto id = IRSim::createCycle("day", 100);
    IRSim::cycleAddBreakpoint("day", 0.25f); // offset 25
    IRSim::cycleAddBreakpoint("day", 0.75f); // offset 75

    // Tick 1: primes silently (segment 0, cycle 0).
    m_system_manager.executePipeline(IRTime::Events::UPDATE);
    EXPECT_FALSE(IREntity::getComponent<C_Cycle>(id).boundaryCrossed_);
    EXPECT_EQ(IREntity::getComponent<C_Cycle>(id).segmentIndex_, 0u);

    advance(24); // sim tick 25 — crosses into segment 1
    {
        const C_Cycle &c = IREntity::getComponent<C_Cycle>(id);
        EXPECT_TRUE(c.boundaryCrossed_);
        EXPECT_EQ(c.fromSegment_, 0u);
        EXPECT_EQ(c.toSegment_, 1u);
        EXPECT_EQ(c.segmentIndex_, 1u);
        EXPECT_EQ(c.fromCycle_, 0u);
        EXPECT_EQ(c.toCycle_, 0u); // same cycle number, different segment
    }

    advance(50); // sim tick 75 — crosses into segment 2
    {
        const C_Cycle &c = IREntity::getComponent<C_Cycle>(id);
        EXPECT_TRUE(c.boundaryCrossed_);
        EXPECT_EQ(c.fromSegment_, 1u);
        EXPECT_EQ(c.toSegment_, 2u);
        EXPECT_EQ(c.segmentIndex_, 2u);
    }

    advance(25); // sim tick 100 — period wrap, back to segment 0
    {
        const C_Cycle &c = IREntity::getComponent<C_Cycle>(id);
        EXPECT_TRUE(c.boundaryCrossed_);
        EXPECT_EQ(c.fromSegment_, 2u);
        EXPECT_EQ(c.toSegment_, 0u);
        EXPECT_EQ(c.segmentIndex_, 0u);
        EXPECT_EQ(c.fromCycle_, 0u);
        EXPECT_EQ(c.toCycle_, 1u);
    }
}

TEST_F(CycleTest, BreakpointNoBoundaryMidSegment) {
    // Confirm no spurious boundary fires while staying inside one segment.
    auto id = IRSim::createCycle("boss", 100);
    IRSim::cycleAddBreakpoint("boss", 0.5f); // segment boundary at tick 50

    m_system_manager.executePipeline(IRTime::Events::UPDATE); // prime at tick 1
    advance(23); // ticks 2..24: still in segment 0 (withinTick 2..24, breakpoint at 50)
    EXPECT_FALSE(IREntity::getComponent<C_Cycle>(id).boundaryCrossed_);
    EXPECT_EQ(IREntity::getComponent<C_Cycle>(id).segmentIndex_, 0u);
}

TEST_F(CycleTest, MidSimCreationWithBreakpointsNoBogusEvent) {
    advance(60); // sim tick 60, in the middle of a hypothetical period
    auto id = IRSim::createCycle("wave", 100);
    IRSim::cycleAddBreakpoint("wave", 0.5f); // segment boundary at tick 50 within period

    // First detect: tick 61 puts us within the period at position 61 % 100 = 61 → segment 1.
    // Should prime silently to segment 1, not fire.
    m_system_manager.executePipeline(IRTime::Events::UPDATE);
    EXPECT_FALSE(IREntity::getComponent<C_Cycle>(id).boundaryCrossed_);
    EXPECT_EQ(IREntity::getComponent<C_Cycle>(id).segmentIndex_, 1u);

    // Advance to tick 100: period wraps to segment 0.
    advance(39); // ticks 62..100
    {
        const C_Cycle &c = IREntity::getComponent<C_Cycle>(id);
        EXPECT_TRUE(c.boundaryCrossed_);
        EXPECT_EQ(c.segmentIndex_, 0u);
    }
}

TEST_F(CycleTest, NoBreakpointsSegmentAlwaysZero) {
    // Without breakpoints the existing period-wrap event still fires and
    // segment stays 0 throughout.
    auto id = IRSim::createCycle("plain", 50);
    m_system_manager.executePipeline(IRTime::Events::UPDATE); // prime

    advance(49); // tick 50: period wrap
    {
        const C_Cycle &c = IREntity::getComponent<C_Cycle>(id);
        EXPECT_TRUE(c.boundaryCrossed_);
        EXPECT_EQ(c.segmentIndex_, 0u);
        EXPECT_EQ(c.fromSegment_, 0u);
        EXPECT_EQ(c.toSegment_, 0u);
    }
}

TEST_F(CycleTest, CycleSegmentQueryMatchesComponent) {
    IRSim::createCycle("phase", 100);
    IRSim::cycleAddBreakpoint("phase", 0.25f);
    IRSim::cycleAddBreakpoint("phase", 0.75f);

    m_system_manager.executePipeline(IRTime::Events::UPDATE); // prime, tick 1
    EXPECT_EQ(IRSim::cycleSegment("phase"), 0u);

    advance(24); // tick 25
    EXPECT_EQ(IRSim::cycleSegment("phase"), 1u);

    advance(50); // tick 75
    EXPECT_EQ(IRSim::cycleSegment("phase"), 2u);

    advance(24); // tick 99
    EXPECT_EQ(IRSim::cycleSegment("phase"), 2u);

    advance(1); // tick 100: wrap
    EXPECT_EQ(IRSim::cycleSegment("phase"), 0u);
}

} // namespace

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

} // namespace

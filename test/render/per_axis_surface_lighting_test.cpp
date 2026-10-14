#include <gtest/gtest.h>
#include <irreden/ir_entity.hpp>

#include <irreden/render/per_axis_surface_lighting.hpp>

// The per-axis surface lighting switch lives on the C_PerAxisSurfaceLighting
// singleton: off until set, readable back, and kept across a scene
// transition like every singleton row.

namespace {

class PerAxisSurfaceLightingTest : public testing::Test {
  protected:
    IREntity::EntityManager m_entity_manager;
};

TEST_F(PerAxisSurfaceLightingTest, OffUntilEnabledAndReadsBack) {
    EXPECT_FALSE(IRPrefab::PerAxisSurfaceLighting::isEnabled());
    ASSERT_NE(IREntity::singletonOrNull<IRComponents::C_PerAxisSurfaceLighting>(), nullptr);

    IRPrefab::PerAxisSurfaceLighting::setEnabled(true);
    EXPECT_TRUE(IRPrefab::PerAxisSurfaceLighting::isEnabled());

    IRPrefab::PerAxisSurfaceLighting::setEnabled(false);
    EXPECT_FALSE(IRPrefab::PerAxisSurfaceLighting::isEnabled());
}

TEST_F(PerAxisSurfaceLightingTest, SurvivesSceneTransition) {
    IRPrefab::PerAxisSurfaceLighting::setEnabled(true);

    IREntity::resetGameplay();

    EXPECT_TRUE(IRPrefab::PerAxisSurfaceLighting::isEnabled());
}

} // namespace

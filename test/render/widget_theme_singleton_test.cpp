#include <gtest/gtest.h>
#include <irreden/ir_entity.hpp>

#include <irreden/render/widget_theme.hpp>

// Widget theme storage lives on the C_WidgetTheme singleton component
// (see #2527). These tests pin the two invariants of component-backed
// storage: the registration-time touch actually creates the row, and a
// creation-customized theme survives the scene-transition teardown
// (resetGameplay() preserves singleton rows).

namespace {

class WidgetThemeSingletonTest : public testing::Test {
  protected:
    IREntity::EntityManager m_entity_manager;
};

// Every WIDGET_RENDER_* system's create() calls ensureThemeSingleton() so the
// singleton exists (main-thread) before frame 1 instead of lazy-creating
// inside a first-frame beginTick.
TEST_F(WidgetThemeSingletonTest, EnsureThemeSingletonCreatesTheRow) {
    ASSERT_EQ(IREntity::singletonOrNull<IRComponents::C_WidgetTheme>(), nullptr);

    IRPrefab::Widget::ensureThemeSingleton();

    auto *row = IREntity::singletonOrNull<IRComponents::C_WidgetTheme>();
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->theme_.borderThickness_, IRPrefab::Widget::WidgetTheme{}.borderThickness_);
}

TEST_F(WidgetThemeSingletonTest, MutatedDefaultThemeSurvivesSceneTransition) {
    IRPrefab::Widget::ensureThemeSingleton();
    IRPrefab::Widget::defaultTheme().borderThickness_ = 9;
    IRPrefab::Widget::defaultTheme().textIdle_ = IRMath::Color{1, 2, 3, 255};

    IREntity::resetGameplay();

    EXPECT_EQ(IRPrefab::Widget::defaultTheme().borderThickness_, 9);
    EXPECT_EQ(
        IRPrefab::Widget::defaultTheme().textIdle_.toPackedRGBA(),
        (IRMath::Color{1, 2, 3, 255}).toPackedRGBA()
    );
}

} // namespace

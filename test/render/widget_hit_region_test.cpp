#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/systems/system_widget_lua_dispatch.hpp>
#include <irreden/render/widgets.hpp>

#include <sol/sol.hpp>

#include <string>

namespace {

using IRComponents::C_GuiPosition;
using IRComponents::C_HitBox2DGui;
using IRComponents::C_Widget;
using IRComponents::C_WidgetState;
using IRComponents::WidgetKind;
using IRSystem::WIDGET_LUA_DISPATCH;

// Hit-region builder, hover routing and WIDGET_LUA_DISPATCH's event order,
// headless: the tests set C_HitBox2DGui / C_WidgetState the way
// HITBOX_MOUSE_TEST_GUI and WIDGET_INPUT would, since those read the mouse.
//
// m_lua is declared FIRST so it is destroyed LAST: the dispatch system holds
// sol refs into it until the SystemManager goes.
class WidgetHitRegionTest : public testing::Test {
  protected:
    void SetUp() override {
        m_lua.open_libraries(sol::lib::base, sol::lib::table);
        m_lua.script("events = {}");
        m_dispatchId = IRSystem::createSystem<WIDGET_LUA_DISPATCH>();
        m_dispatch =
            m_systemManager.getSystemParams<IRSystem::System<WIDGET_LUA_DISPATCH>>(m_dispatchId);
        ASSERT_NE(m_dispatch, nullptr);
        m_systemManager.registerPipeline(IRTime::Events::INPUT, {m_dispatchId});
    }

    void TearDown() override {
        m_entityManager.destroyAllEntities();
    }

    // A Lua function appending "<tag><id>" to the global `events` list.
    sol::protected_function recorder(const std::string &tag) {
        return m_lua.script("return function(id) events[#events + 1] = '" + tag + "' .. id end");
    }

    std::string events() {
        return m_lua.script("return table.concat(events, ' ')").get<std::string>();
    }

    std::string tagged(const char *tag, IREntity::EntityId id) {
        return std::string(tag) + std::to_string(id);
    }

    C_WidgetState &state(IREntity::EntityId widget) {
        return IREntity::getComponent<C_WidgetState>(widget);
    }

    void dispatch() {
        m_systemManager.executePipeline(IRTime::Events::INPUT);
    }

    sol::state m_lua;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    IRSystem::SystemId m_dispatchId{};
    IRSystem::System<WIDGET_LUA_DISPATCH> *m_dispatch{nullptr};
};

TEST_F(WidgetHitRegionTest, MakeHitRegionBuildsAnInteractiveWidgetWithNoDrawData) {
    const IREntity::EntityId region =
        IRPrefab::Widget::makeHitRegion(IRMath::ivec2(12, 40), IRMath::ivec2(300, 9), 4);

    const C_Widget &widget = IREntity::getComponent<C_Widget>(region);
    EXPECT_EQ(widget.kind_, WidgetKind::HIT_REGION);
    EXPECT_EQ(widget.size_, IRMath::ivec2(300, 9));
    EXPECT_EQ(widget.zOrder_, 4);
    EXPECT_EQ(IREntity::getComponent<C_GuiPosition>(region).pos_, IRMath::ivec2(12, 40));
    EXPECT_EQ(IREntity::getComponent<C_HitBox2DGui>(region).size_, IRMath::ivec2(300, 9));
    EXPECT_TRUE(IREntity::getComponentOptional<C_WidgetState>(region).has_value());
    // No per-kind data component, so no widget renderer matches it.
    EXPECT_FALSE(IREntity::getComponentOptional<IRComponents::C_WidgetButton>(region).has_value());
    EXPECT_FALSE(IREntity::getComponentOptional<IRComponents::C_WidgetLabel>(region).has_value());
    EXPECT_FALSE(IREntity::getComponentOptional<IRComponents::C_WidgetPanel>(region).has_value());
}

TEST_F(WidgetHitRegionTest, HitRegionIsNotATabFocusCandidate) {
    EXPECT_FALSE(
        IRPrefab::Widget::isTabFocusCandidate(C_Widget{WidgetKind::HIT_REGION, IRMath::ivec2(8, 8)})
    );
}

// A region overlapping a button routes hover by z-order like any widget.
TEST_F(WidgetHitRegionTest, TopmostHoveredWidgetFollowsZOrder) {
    const IREntity::EntityId button =
        IRPrefab::Widget::makeButton(IRMath::ivec2(0, 0), IRMath::ivec2(50, 20), "UNDER");
    const IREntity::EntityId region =
        IRPrefab::Widget::makeHitRegion(IRMath::ivec2(0, 0), IRMath::ivec2(200, 20), 5);

    EXPECT_EQ(IRPrefab::Widget::topmostHoveredWidget(), IREntity::kNullEntity);

    IREntity::getComponent<C_HitBox2DGui>(button).hovered_ = true;
    IREntity::getComponent<C_HitBox2DGui>(region).hovered_ = true;
    EXPECT_EQ(IRPrefab::Widget::topmostHoveredWidget(), region);

    IRPrefab::Widget::setDisabled(region, true);
    EXPECT_EQ(IRPrefab::Widget::topmostHoveredWidget(), button);

    IRPrefab::Widget::setDisabled(region, false);
    IREntity::getComponent<C_Widget>(region).zOrder_ = -1;
    EXPECT_EQ(IRPrefab::Widget::topmostHoveredWidget(), button);
}

TEST_F(WidgetHitRegionTest, HoverEdgesFireOncePerTransition) {
    const IREntity::EntityId region =
        IRPrefab::Widget::makeHitRegion(IRMath::ivec2(0, 0), IRMath::ivec2(10, 10));
    m_dispatch->registerHoverHandlers(region, recorder("in"), recorder("out"));

    dispatch();
    EXPECT_EQ(events(), "");

    state(region).hovered_ = true;
    dispatch();
    dispatch();
    EXPECT_EQ(events(), tagged("in", region));

    state(region).hovered_ = false;
    dispatch();
    EXPECT_EQ(events(), tagged("in", region) + " " + tagged("out", region));
}

// Within one frame every unhover fires before any hover, and clicks last,
// whatever order the widgets were iterated in.
TEST_F(WidgetHitRegionTest, OneFrameFiresUnhoversThenHoversThenClicks) {
    const IREntity::EntityId later =
        IRPrefab::Widget::makeHitRegion(IRMath::ivec2(0, 20), IRMath::ivec2(10, 10));
    const IREntity::EntityId earlier =
        IRPrefab::Widget::makeHitRegion(IRMath::ivec2(0, 0), IRMath::ivec2(10, 10));
    m_dispatch->registerHoverHandlers(later, recorder("in"), recorder("out"));
    m_dispatch->registerHoverHandlers(earlier, recorder("in"), recorder("out"));
    m_dispatch->registerClickHandler(later, recorder("click"));

    state(earlier).hovered_ = true;
    dispatch();
    m_lua.script("events = {}");

    state(earlier).hovered_ = false;
    state(later).hovered_ = true;
    state(later).fireAction_ = true;
    dispatch();
    EXPECT_EQ(
        events(),
        tagged("out", earlier) + " " + tagged("in", later) + " " + tagged("click", later)
    );
}

// Either hover function may be absent; the edge is still tracked, so the
// other one fires on its own transition.
TEST_F(WidgetHitRegionTest, MissingHoverHandlerIsSkipped) {
    const IREntity::EntityId region =
        IRPrefab::Widget::makeHitRegion(IRMath::ivec2(0, 0), IRMath::ivec2(10, 10));
    m_dispatch->registerHoverHandlers(region, sol::protected_function{}, recorder("out"));

    state(region).hovered_ = true;
    dispatch();
    state(region).hovered_ = false;
    dispatch();
    EXPECT_EQ(events(), tagged("out", region));
}

// Destroying a widget drops its handlers, so a recycled entity id never
// inherits them.
TEST_F(WidgetHitRegionTest, DestroyDropsHandlers) {
    const IREntity::EntityId region =
        IRPrefab::Widget::makeHitRegion(IRMath::ivec2(0, 0), IRMath::ivec2(10, 10));
    m_dispatch->registerHoverHandlers(region, recorder("in"), recorder("out"));
    m_dispatch->registerClickHandler(region, recorder("click"));

    IREntity::destroyEntity(region);
    m_entityManager.destroyMarkedEntities();

    EXPECT_TRUE(m_dispatch->hoverHandlers_.empty());
    EXPECT_TRUE(m_dispatch->clickHandlers_.empty());
}

} // namespace

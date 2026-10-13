#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/systems/system_widget_lua_dispatch.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <string>

namespace {

using IRComponents::C_GuiPosition;
using IRComponents::C_HitBox2DGui;
using IRComponents::C_Widget;
using IRComponents::C_WidgetState;
using IRComponents::WidgetKind;
using IRSystem::WIDGET_LUA_DISPATCH;

// Drives `IRGui.makeHitRegion` / `IRGui.destroyWidget` from Lua and dispatches
// through WIDGET_LUA_DISPATCH. C_WidgetState is written the way WIDGET_INPUT
// would, since WIDGET_INPUT reads the mouse.
//
// m_lua is declared FIRST so it is destroyed LAST (test/CLAUDE.md, Lua seam
// tests): the dispatch system holds sol refs until the SystemManager goes.
class LuaWidgetHitRegion : public testing::Test {
  protected:
    LuaWidgetHitRegion() {
        m_lua.bindLuaDrivenEcs();
    }

    void TearDown() override {
        m_entityManager.destroyAllEntities();
    }

    // Registers WIDGET_LUA_DISPATCH where the binding resolves it and runs it
    // as the INPUT pipeline.
    void registerDispatch() {
        const IRSystem::SystemId id = m_lua.registerPrefabSystem<WIDGET_LUA_DISPATCH>();
        m_systemManager.registerPipeline(IRTime::Events::INPUT, {id});
    }

    void run(const char *script) {
        auto result = m_lua.lua().safe_script(script, sol::script_pass_on_error);
        ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    }

    std::string runExpectingError(const char *script) {
        auto result = m_lua.lua().safe_script(script, sol::script_pass_on_error);
        EXPECT_FALSE(result.valid());
        if (result.valid()) {
            return {};
        }
        sol::error err = result;
        return err.what();
    }

    IREntity::EntityId entity(const char *global) {
        return m_lua.lua()[global].get<IREntity::EntityId>();
    }

    std::string events() {
        return m_lua.lua().safe_script("return table.concat(events, ' ')").get<std::string>();
    }

    C_WidgetState &state(IREntity::EntityId widget) {
        return IREntity::getComponent<C_WidgetState>(widget);
    }

    void dispatch() {
        m_systemManager.executePipeline(IRTime::Events::INPUT);
    }

    std::size_t widgetCount() {
        std::size_t count = 0;
        IREntity::forEachComponent<C_Widget>([&count](IREntity::EntityId &, C_Widget &) {
            ++count;
        });
        return count;
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
};

// Every option is optional: a bare region is an invisible, hoverable widget
// that needs no dispatch system.
TEST_F(LuaWidgetHitRegion, BareRegionBuildsTheWidget) {
    run("region = IRGui.makeHitRegion(16, 42, 300, 9)");
    const IREntity::EntityId region = entity("region");

    const C_Widget &widget = IREntity::getComponent<C_Widget>(region);
    EXPECT_EQ(widget.kind_, WidgetKind::HIT_REGION);
    EXPECT_EQ(widget.size_, IRMath::ivec2(300, 9));
    EXPECT_EQ(widget.zOrder_, 0);
    EXPECT_EQ(IREntity::getComponent<C_GuiPosition>(region).pos_, IRMath::ivec2(16, 42));
    EXPECT_EQ(IREntity::getComponent<C_HitBox2DGui>(region).size_, IRMath::ivec2(300, 9));
}

TEST_F(LuaWidgetHitRegion, ZOrderOptionIsAppliedAndClampedBelowTheDropdownBias) {
    run(R"lua(
        raised = IRGui.makeHitRegion(0, 0, 10, 10, { zOrder = 7 })
        capped = IRGui.makeHitRegion(0, 0, 10, 10, { zOrder = 5000 })
    )lua");
    EXPECT_EQ(IREntity::getComponent<C_Widget>(entity("raised")).zOrder_, 7);
    EXPECT_EQ(
        IREntity::getComponent<C_Widget>(entity("capped")).zOrder_,
        IRComponents::kWidgetDropdownOpenZBias - 1
    );
}

TEST_F(LuaWidgetHitRegion, HoverUnhoverAndClickReachLua) {
    registerDispatch();
    run(R"lua(
        events = {}
        local function record(tag)
            return function(id) events[#events + 1] = tag .. ':' .. tostring(id == region) end
        end
        region = IRGui.makeHitRegion(0, 0, 100, 10, {
            onHover = record('hover'),
            onUnhover = record('unhover'),
            onClick = record('click'),
        })
    )lua");
    const IREntity::EntityId region = entity("region");

    state(region).hovered_ = true;
    dispatch();
    state(region).fireAction_ = true;
    dispatch();
    state(region).fireAction_ = false;
    state(region).hovered_ = false;
    dispatch();

    EXPECT_EQ(events(), "hover:true click:true unhover:true");
}

// Moving from one row to the next: the old row's unhover fires before the new
// row's hover, so a handler that paints only its own row never ends with two
// rows lit.
TEST_F(LuaWidgetHitRegion, MovingBetweenRegionsUnhoversFirst) {
    registerDispatch();
    run(R"lua(
        events = {}
        rows = {}
        for row = 1, 2 do
            rows[row] = IRGui.makeHitRegion(0, (row - 1) * 10, 100, 10, {
                onHover = function() events[#events + 1] = 'hover' .. row end,
                onUnhover = function() events[#events + 1] = 'unhover' .. row end,
            })
        end
        first, second = rows[1], rows[2]
    )lua");
    const IREntity::EntityId first = entity("first");
    const IREntity::EntityId second = entity("second");

    state(second).hovered_ = true;
    dispatch();
    state(second).hovered_ = false;
    state(first).hovered_ = true;
    dispatch();

    EXPECT_EQ(events(), "hover2 unhover2 hover1");
}

// A click handler may tear the whole menu down, its own region included.
TEST_F(LuaWidgetHitRegion, ClickHandlerMayDestroyItsOwnRegion) {
    registerDispatch();
    run(R"lua(
        clicks = 0
        region = IRGui.makeHitRegion(0, 0, 100, 10, {
            onClick = function(id)
                clicks = clicks + 1
                IRGui.destroyWidget(id)
            end,
        })
    )lua");
    const IREntity::EntityId region = entity("region");

    state(region).fireAction_ = true;
    dispatch();
    m_entityManager.destroyMarkedEntities();

    EXPECT_EQ(m_lua.lua()["clicks"].get<int>(), 1);
    EXPECT_FALSE(IREntity::entityExists(region));
}

// The old row's unhover tears down the row the cursor moved onto, whose hover
// is already queued behind it in the same frame.
TEST_F(LuaWidgetHitRegion, UnhoverHandlerDestroyingTheNextRegionDropsItsQueuedHover) {
    registerDispatch();
    run(R"lua(
        events = {}
        first = IRGui.makeHitRegion(0, 0, 100, 10, {
            onHover = function() events[#events + 1] = 'hover1' end,
            onUnhover = function()
                events[#events + 1] = 'unhover1'
                IRGui.destroyWidget(second)
            end,
        })
        second = IRGui.makeHitRegion(0, 10, 100, 10, {
            onHover = function() events[#events + 1] = 'hover2' end,
            onClick = function() events[#events + 1] = 'click2' end,
        })
    )lua");
    const IREntity::EntityId first = entity("first");
    const IREntity::EntityId second = entity("second");

    state(first).hovered_ = true;
    dispatch();
    state(first).hovered_ = false;
    state(second).hovered_ = true;
    state(second).fireAction_ = true;
    dispatch();

    EXPECT_EQ(events(), "hover1 unhover1");

    m_entityManager.destroyMarkedEntities();
    EXPECT_TRUE(IREntity::entityExists(first));
    EXPECT_FALSE(IREntity::entityExists(second));
}

TEST_F(LuaWidgetHitRegion, HoverHandlerDestroyingItsOwnRegionDropsItsQueuedClick) {
    registerDispatch();
    run(R"lua(
        events = {}
        region = IRGui.makeHitRegion(0, 0, 100, 10, {
            onHover = function(id)
                events[#events + 1] = 'hover'
                IRGui.destroyWidget(id)
            end,
            onClick = function() events[#events + 1] = 'click' end,
        })
    )lua");
    const IREntity::EntityId region = entity("region");

    state(region).hovered_ = true;
    state(region).fireAction_ = true;
    dispatch();

    EXPECT_EQ(events(), "hover");
}

// A tree mark names the widget the same as a plain one: a handler that tears
// down the panel a row is parented to silences that row's queued events.
TEST_F(LuaWidgetHitRegion, HandlerTreeDestroyingAParentDropsTheChildRegionsQueuedEvents) {
    registerDispatch();
    run(R"lua(
        events = {}
        panel = IRGui.makePanel(0, 0, 100, 40)
        closer = IRGui.makeHitRegion(0, 0, 100, 10, {
            onUnhover = function()
                events[#events + 1] = 'close'
                IREntity.deferredDestroyTree(panel)
            end,
        })
        row = IRGui.makeHitRegion(0, 10, 100, 10, {
            onHover = function() events[#events + 1] = 'hoverRow' end,
            onClick = function() events[#events + 1] = 'clickRow' end,
        })
        IREntity.setParent(row, panel)
    )lua");
    const IREntity::EntityId closer = entity("closer");
    const IREntity::EntityId row = entity("row");

    state(closer).hovered_ = true;
    dispatch();
    state(closer).hovered_ = false;
    state(row).hovered_ = true;
    state(row).fireAction_ = true;
    dispatch();

    EXPECT_EQ(events(), "close");

    m_entityManager.destroyMarkedEntities();
    EXPECT_FALSE(IREntity::entityExists(row));
    EXPECT_TRUE(IREntity::entityExists(closer));
}

// A widget destroyed outside a handler is still in its archetype until the
// drain, so the tick visits it; it reports nothing, including the unhover of
// a widget destroyed while hovered.
TEST_F(LuaWidgetHitRegion, RegionDestroyedBeforeTheDrainGetsNoFurtherEvents) {
    registerDispatch();
    run(R"lua(
        events = {}
        local function record(tag)
            return function() events[#events + 1] = tag end
        end
        region = IRGui.makeHitRegion(0, 0, 100, 10, {
            onHover = record('hover'),
            onUnhover = record('unhover'),
            onClick = record('click'),
        })
    )lua");
    const IREntity::EntityId region = entity("region");

    state(region).hovered_ = true;
    dispatch();
    run("IRGui.destroyWidget(region)");
    state(region).hovered_ = false;
    state(region).fireAction_ = true;
    dispatch();

    EXPECT_EQ(events(), "hover");
}

TEST_F(LuaWidgetHitRegion, DestroyWidgetRemovesAnyWidgetAndRejectsOtherIds) {
    run(R"lua(
        region = IRGui.makeHitRegion(0, 0, 10, 10)
        label = IRGui.makeLabel(0, 0, "ROW")
    )lua");
    const IREntity::EntityId notAWidget = IREntity::createEntity();
    m_lua.lua()["notAWidget"] = notAWidget;

    run("IRGui.destroyWidget(region) IRGui.destroyWidget(label)");
    m_entityManager.destroyMarkedEntities();
    EXPECT_EQ(widgetCount(), 0u);

    EXPECT_NE(
        runExpectingError("IRGui.destroyWidget(notAWidget)").find("is not a live widget"),
        std::string::npos
    );
    EXPECT_NE(
        runExpectingError("IRGui.destroyWidget(region)").find("is not a live widget"),
        std::string::npos
    );
    EXPECT_TRUE(IREntity::entityExists(notAWidget));
}

// A rejected call builds nothing.
TEST_F(LuaWidgetHitRegion, HandlersWithoutTheDispatchSystemRaise) {
    const std::string message =
        runExpectingError("IRGui.makeHitRegion(0, 0, 10, 10, { onClick = function() end })");
    EXPECT_NE(message.find("WIDGET_LUA_DISPATCH is not registered"), std::string::npos) << message;
    EXPECT_EQ(widgetCount(), 0u);
}

TEST_F(LuaWidgetHitRegion, BadOptionsRaise) {
    registerDispatch();
    EXPECT_NE(
        runExpectingError("IRGui.makeHitRegion(0, 0, 10, 10, { onclick = function() end })")
            .find("unknown option"),
        std::string::npos
    );
    EXPECT_NE(
        runExpectingError("IRGui.makeHitRegion(0, 0, 10, 10, { onHover = 3 })")
            .find("'onHover' must be a function"),
        std::string::npos
    );
    EXPECT_NE(
        runExpectingError("IRGui.makeHitRegion(0, 0, 10, 10, { zOrder = 'top' })")
            .find("'zOrder' must be a number"),
        std::string::npos
    );
    EXPECT_EQ(widgetCount(), 0u);
}

} // namespace

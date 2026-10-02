#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/input/components/component_entity_event_handlers.hpp>
#include <irreden/input/systems/system_entity_hover_detect.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <string>

namespace {

using IRComponents::C_EntityEventHandlers;
using IRComponents::EntityClickButton;
using IRSystem::ENTITY_HOVER_DETECT;

// Drives the `IRInput.onEntity*` / `onRightClick` / `removeEntityHandler`
// bindings from Lua and dispatches through the same registry
// ENTITY_HOVER_DETECT fires. Hover goes through `applyHoverTransition`; clicks
// fire the registry directly, since `dispatchClicks` polls the input manager
// (no GL-free seam) and only chooses which `fire*` to call.
//
// Member order is load-bearing (test/CLAUDE.md, Lua-seam pattern): m_lua is
// declared FIRST so it is destroyed LAST, after TearDown has dropped the
// registry row and every sol ref it held.
class LuaEntityEvents : public testing::Test {
  protected:
    LuaEntityEvents() {
        m_lua.bindLuaCommands();
    }

    void SetUp() override {
        m_systemId = IRSystem::createSystem<ENTITY_HOVER_DETECT>();
        m_hoverDetect =
            m_systemManager.getSystemParams<IRSystem::System<ENTITY_HOVER_DETECT>>(m_systemId);
        ASSERT_NE(m_hoverDetect, nullptr);
    }

    void TearDown() override {
        m_entityManager.destroyAllEntities();
    }

    void run(const char *script) {
        auto result = m_lua.lua().safe_script(script, sol::script_pass_on_error);
        ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    }

    template <typename T> T global(const char *name) {
        return m_lua.lua()[name].get<T>();
    }

    C_EntityEventHandlers &handlers() {
        return IREntity::singleton<C_EntityEventHandlers>();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    IRSystem::SystemId m_systemId{};
    IRSystem::System<ENTITY_HOVER_DETECT> *m_hoverDetect{nullptr};
};

// Hover-in then hover-out through the hover-detect state machine: each Lua
// handler runs exactly once, with the hovered entity's id.
TEST_F(LuaEntityEvents, HoverAndUnhoverFireOnce) {
    run(R"lua(
        hoveredCount, unhoveredCount = 0, 0
        hoveredId, unhoveredId = -1, -1
        IRInput.onEntityHovered(function(id)
            hoveredCount = hoveredCount + 1
            hoveredId = id
        end)
        IRInput.onEntityUnhovered(function(id)
            unhoveredCount = unhoveredCount + 1
            unhoveredId = id
        end)
    )lua");
    const IREntity::EntityId entity = IREntity::createEntity();
    ASSERT_EQ(global<int>("hoveredCount"), 0);

    m_hoverDetect->applyHoverTransition(entity);
    // The same entity again is not a transition.
    m_hoverDetect->applyHoverTransition(entity);

    EXPECT_EQ(global<int>("hoveredCount"), 1);
    EXPECT_EQ(global<int>("unhoveredCount"), 0);
    EXPECT_EQ(global<IREntity::EntityId>("hoveredId"), entity);

    m_hoverDetect->applyHoverTransition(IREntity::kNullEntity);

    EXPECT_EQ(global<int>("hoveredCount"), 1);
    EXPECT_EQ(global<int>("unhoveredCount"), 1);
    EXPECT_EQ(global<IREntity::EntityId>("unhoveredId"), entity);
}

// The click handler gets the entity id and a button that compares equal to
// the `IRInput.MouseButton` entry for the button the dispatcher passed.
TEST_F(LuaEntityEvents, ClickPassesButton) {
    run(R"lua(
        clicks = {}
        IRInput.onEntityClicked(function(id, button)
            clicks[#clicks + 1] = {
                id = id,
                left = button == IRInput.MouseButton.LEFT,
                right = button == IRInput.MouseButton.RIGHT,
            }
        end)
    )lua");

    handlers().fireClicked(4242u, EntityClickButton::LEFT);
    handlers().fireClicked(4343u, EntityClickButton::RIGHT);

    sol::table clicks = global<sol::table>("clicks");
    ASSERT_EQ(clicks.size(), 2u);
    EXPECT_EQ(clicks[1]["id"].get<IREntity::EntityId>(), 4242u);
    EXPECT_TRUE(clicks[1]["left"].get<bool>());
    EXPECT_FALSE(clicks[1]["right"].get<bool>());
    EXPECT_EQ(clicks[2]["id"].get<IREntity::EntityId>(), 4343u);
    EXPECT_TRUE(clicks[2]["right"].get<bool>());
    EXPECT_FALSE(clicks[2]["left"].get<bool>());
}

// `onRightClick` handlers take no arguments — they fire on every right press,
// hovered entity or not.
TEST_F(LuaEntityEvents, RightClickFiresWithNoArguments) {
    run(R"lua(
        rightClicks, rightClickArgc = 0, -1
        IRInput.onRightClick(function(...)
            rightClicks = rightClicks + 1
            rightClickArgc = select('#', ...)
        end)
    )lua");

    handlers().fireRightClick();

    EXPECT_EQ(global<int>("rightClicks"), 1);
    EXPECT_EQ(global<int>("rightClickArgc"), 0);
}

// `removeEntityHandler` takes the id a registrar returned and stops only that
// handler; a second handler on the same event keeps firing, so the dispatch
// provably ran.
TEST_F(LuaEntityEvents, RemoveHandlerStopsDispatch) {
    run(R"lua(
        removedCount, keptCount = 0, 0
        removedHandler = IRInput.onEntityHovered(function() removedCount = removedCount + 1 end)
        keptHandler = IRInput.onEntityHovered(function() keptCount = keptCount + 1 end)
        IRInput.removeEntityHandler(removedHandler)
    )lua");
    EXPECT_NE(global<int>("removedHandler"), global<int>("keptHandler"));

    m_hoverDetect->applyHoverTransition(IREntity::createEntity());

    EXPECT_EQ(global<int>("removedCount"), 0);
    EXPECT_EQ(global<int>("keptCount"), 1);
}

TEST_F(LuaEntityEvents, MouseButtonValuesMatchCpp) {
    auto result = m_lua.lua().safe_script(
        "return IRInput.MouseButton.LEFT, IRInput.MouseButton.RIGHT",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.get<lua_Integer>(0), static_cast<lua_Integer>(EntityClickButton::LEFT));
    EXPECT_EQ(result.get<lua_Integer>(1), static_cast<lua_Integer>(EntityClickButton::RIGHT));
}

// `bindLuaCommands` is idempotent: a creation re-running it after binding its
// own `IRInput.onEntityClicked` keeps its copy.
TEST_F(LuaEntityEvents, RebindKeepsACreationOverride) {
    run("IRInput.onEntityClicked = function() return 'creation' end");

    m_lua.bindLuaCommands();

    auto result =
        m_lua.lua().safe_script("return IRInput.onEntityClicked()", sol::script_pass_on_error);
    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.get<std::string>(), "creation");
}

// A creation that binds `IRInput.onEntityClicked` before its first
// `bindLuaCommands()` keeps its copy, and every key it did not set still binds.
// No handler is registered: the singleton would outlive this local state.
TEST_F(LuaEntityEvents, PreBindKeepsACreationOverride) {
    IRScript::LuaScript lua;
    auto setUp = lua.lua().safe_script(
        "IRInput = { onEntityClicked = function() return 'creation' end }",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(setUp.valid()) << setUp.get<sol::error>().what();

    lua.bindLuaCommands();

    auto result = lua.lua().safe_script(
        R"lua(
            return IRInput.onEntityClicked(),
                type(IRInput.onEntityHovered),
                type(IRInput.onEntityUnhovered),
                type(IRInput.onRightClick),
                type(IRInput.removeEntityHandler),
                IRInput.MouseButton.LEFT
        )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_EQ(result.get<std::string>(0), "creation");
    EXPECT_EQ(result.get<std::string>(1), "function");
    EXPECT_EQ(result.get<std::string>(2), "function");
    EXPECT_EQ(result.get<std::string>(3), "function");
    EXPECT_EQ(result.get<std::string>(4), "function");
    EXPECT_EQ(result.get<lua_Integer>(5), static_cast<lua_Integer>(EntityClickButton::LEFT));
}

} // namespace

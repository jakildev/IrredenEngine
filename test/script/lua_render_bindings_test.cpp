#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_render_bindings.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <string>

namespace {

using IRMath::Color;

// Owns the managers `bindLuaDrivenEcs()` touches. There is deliberately NO
// RenderManager: `bindRenderGlue` only registers lambdas at bind time, so
// table / function PRESENCE is verifiable headless. Actually INVOKING the
// setters or GUI draws calls into IRRender (needs a GPU context) and is
// covered end-to-end by the lua_pipeline_demo screenshot path instead.
class LuaRenderBindingsTest : public testing::Test {
  protected:
    LuaRenderBindingsTest()
        : m_lua{}
        , m_entity_manager{}
        , m_system_manager{} {
        m_lua.bindLuaDrivenEcs();
    }

    bool isFunction(const char *expr) {
        auto result = m_lua.lua().safe_script(
            std::string("return type(") + expr + ") == 'function'",
            sol::script_pass_on_error
        );
        return result.valid() && result.get<bool>();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

// ---- render-glue setter presence ------------------------------------------

TEST_F(LuaRenderBindingsTest, SunSettersBound) {
    EXPECT_TRUE(isFunction("IRRender.setSunDirection"));
    EXPECT_TRUE(isFunction("IRRender.setSunIntensity"));
    EXPECT_TRUE(isFunction("IRRender.setSunAmbient"));
}

TEST_F(LuaRenderBindingsTest, SkySettersBound) {
    EXPECT_TRUE(isFunction("IRRender.setSkyColor"));
    EXPECT_TRUE(isFunction("IRRender.setSkyIntensity"));
}

TEST_F(LuaRenderBindingsTest, GuiDrawPrimitivesBound) {
    EXPECT_TRUE(isFunction("IRGui.drawDisc"));
    EXPECT_TRUE(isFunction("IRGui.drawLine"));
}

// bindRenderGlue must EXTEND, not replace, an IRRender table a creation has
// already populated — otherwise wiring the shared bindings would wipe the
// creation's own IRRender entries (getGuiScale, measureText, ...).
TEST_F(LuaRenderBindingsTest, ExtendsExistingIRRenderTable) {
    auto &lua = m_lua.lua();
    lua["IRRender"]["creationOnly"] = 42;
    IRScript::detail::bindRenderGlue(m_lua); // re-run as a creation would after pre-populating
    EXPECT_EQ(lua["IRRender"]["creationOnly"].get<int>(), 42);
    EXPECT_TRUE(isFunction("IRRender.setSunDirection"));
}

// ---- colorFromLua (shared Lua → IRMath::Color helper) ---------------------

TEST_F(LuaRenderBindingsTest, ColorFromLuaIndexedTable) {
    auto &lua = m_lua.lua();
    sol::object obj = lua.create_table_with(1, 255, 2, 128, 3, 64, 4, 200);
    const Color color = IRScript::colorFromLua(obj);
    EXPECT_EQ(static_cast<int>(color.red_), 255);
    EXPECT_EQ(static_cast<int>(color.green_), 128);
    EXPECT_EQ(static_cast<int>(color.blue_), 64);
    EXPECT_EQ(static_cast<int>(color.alpha_), 200);
}

TEST_F(LuaRenderBindingsTest, ColorFromLuaKeyedTableAlphaDefaultsOpaque) {
    auto &lua = m_lua.lua();
    sol::object obj = lua.create_table_with("r", 10, "g", 20, "b", 30);
    const Color color = IRScript::colorFromLua(obj);
    EXPECT_EQ(static_cast<int>(color.red_), 10);
    EXPECT_EQ(static_cast<int>(color.green_), 20);
    EXPECT_EQ(static_cast<int>(color.blue_), 30);
    EXPECT_EQ(static_cast<int>(color.alpha_), 255); // omitted alpha = opaque
}

TEST_F(LuaRenderBindingsTest, ColorFromLuaNilIsOpaqueWhite) {
    sol::object nilObj = m_lua.lua()["__nonexistent__"];
    const Color color = IRScript::colorFromLua(nilObj);
    EXPECT_EQ(static_cast<int>(color.red_), 255);
    EXPECT_EQ(static_cast<int>(color.green_), 255);
    EXPECT_EQ(static_cast<int>(color.blue_), 255);
    EXPECT_EQ(static_cast<int>(color.alpha_), 255);
}

} // namespace

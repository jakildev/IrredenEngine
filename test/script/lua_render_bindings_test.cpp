#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_camera.hpp>
#include <irreden/render/components/component_camera_zoom_frame_state.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
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

TEST_F(LuaRenderBindingsTest, EntityCanvasCountBound) {
    EXPECT_TRUE(isFunction("IRRender.entityCanvasCount"));
    auto result = m_lua.lua().safe_script("return IRRender.entityCanvasCount()");
    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.get<int>(), 0);
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
    EXPECT_TRUE(isFunction("IRRender.setCameraZoom"));
}

// ---- main-camera zoom -------------------------------------------------------

TEST_F(LuaRenderBindingsTest, CameraZoomSurfaceBound) {
    EXPECT_TRUE(isFunction("IRRender.setCameraZoom"));
    EXPECT_TRUE(isFunction("IRRender.getCameraZoom"));
    EXPECT_TRUE(isFunction("IRRender.setCameraZoomContinuous"));
    EXPECT_TRUE(isFunction("IRRender.isCameraZoomContinuous"));
}

// The policy pair only touches the camera entity's components, so it is
// callable headless against a camera stood up the way the render manager
// builds it. The value pair reaches the render manager and is covered end to
// end by the lua_pipeline_demo run.
TEST_F(LuaRenderBindingsTest, CameraZoomPolicyRoundTripsThroughLua) {
    const IREntity::EntityId camera = IREntity::createEntity(
        IRComponents::C_Camera{},
        IRComponents::C_ZoomLevel{2.0f},
        IRComponents::C_CameraZoomFrameState{}
    );
    IREntity::setName(camera, "camera");

    auto result = m_lua.lua().safe_script(
        R"lua(
        local before = IRRender.isCameraZoomContinuous()
        IRRender.setCameraZoomContinuous(true)
        local during = IRRender.isCameraZoomContinuous()
        IRRender.setCameraZoomContinuous(false)
        return before, during, IRRender.isCameraZoomContinuous()
        )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_FALSE(result.get<bool>(0));
    EXPECT_TRUE(result.get<bool>(1));
    EXPECT_FALSE(result.get<bool>(2));
    EXPECT_FALSE(IREntity::getComponent<IRComponents::C_Camera>(camera).continuousZoom_);
}

// Arguments are checked before any write, so a rejected call leaves the zoom
// and the policy exactly as they were.
TEST_F(LuaRenderBindingsTest, CameraZoomRejectsBadArgumentsWithoutChangingState) {
    const IREntity::EntityId camera = IREntity::createEntity(
        IRComponents::C_Camera{},
        IRComponents::C_ZoomLevel{2.0f},
        IRComponents::C_CameraZoomFrameState{}
    );
    IREntity::setName(camera, "camera");
    IRPrefab::Camera::setZoomContinuous(true);

    for (const char *call : {
             "IRRender.setCameraZoom('fast')",
             "IRRender.setCameraZoom(nil)",
             "IRRender.setCameraZoom(0/0)",
             "IRRender.setCameraZoom(math.huge)",
             "IRRender.setCameraZoomContinuous(1)",
             "IRRender.setCameraZoomContinuous('on')",
         }) {
        auto result = m_lua.lua().safe_script(call, sol::script_pass_on_error);
        EXPECT_FALSE(result.valid()) << call << " did not raise";
    }

    EXPECT_TRUE(IREntity::getComponent<IRComponents::C_Camera>(camera).continuousZoom_);
    EXPECT_EQ(
        IREntity::getComponent<IRComponents::C_ZoomLevel>(camera).zoom_,
        IRMath::vec2(2.0f)
    );
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

// ---- IRGui.setLabelText -----------------------------------------------------

// makeLabel / setLabelText only create and write ECS rows, so unlike the draw
// primitives they are callable headless.
TEST_F(LuaRenderBindingsTest, SetLabelTextRewritesTheLabel) {
    auto result = m_lua.lua().safe_script(
        R"lua(
        local label = IRGui.makeLabel(0, 0, "")
        IRGui.setLabelText(label, "HOVERING")
        return label
        )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    const auto label = static_cast<IREntity::EntityId>(result.get<lua_Integer>());
    EXPECT_EQ(IREntity::getComponent<IRComponents::C_WidgetLabel>(label).text_, "HOVERING");
}

TEST_F(LuaRenderBindingsTest, SetLabelTextRaisesOnANonLabel) {
    const IREntity::EntityId notALabel = IREntity::createEntity();
    m_lua.lua()["notALabel"] = static_cast<lua_Integer>(notALabel);
    auto result =
        m_lua.lua().safe_script("IRGui.setLabelText(notALabel, 'X')", sol::script_pass_on_error);
    ASSERT_FALSE(result.valid());
    EXPECT_NE(
        std::string(result.get<sol::error>().what()).find("is not a label"),
        std::string::npos
    );
    EXPECT_FALSE(
        IREntity::getComponentOptional<IRComponents::C_WidgetLabel>(notALabel).has_value()
    );
}

} // namespace

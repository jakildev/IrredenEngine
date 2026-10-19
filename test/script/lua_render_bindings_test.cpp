#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_camera.hpp>
#include <irreden/render/components/component_camera_zoom_frame_state.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_glitch_state.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/glitch.hpp>
#include <irreden/render/glitch_subject.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_render_bindings.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/time/time_manager.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <sol/sol.hpp>

#include <stdexcept>
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
        , m_time_manager{}
        , m_entity_manager{}
        , m_system_manager{} {
        m_lua.bindLuaDrivenEcs();
    }

    ~LuaRenderBindingsTest() override {
        IRRender::setHeadlessActiveCanvasEntity(IREntity::kNullEntity);
    }

    bool isFunction(const char *expr) {
        auto result = m_lua.lua().safe_script(
            std::string("return type(") + expr + ") == 'function'",
            sol::script_pass_on_error
        );
        return result.valid() && result.get<bool>();
    }

    IRScript::LuaScript m_lua;
    // The glitch readback reads the engine clock, which asserts without a
    // TimeManager. A bare one constructs standalone with tick 0.
    IRTime::TimeManager m_time_manager;
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

TEST_F(LuaRenderBindingsTest, CanAllocateVoxelsReadsTheActiveCanvasPool) {
    const IREntity::EntityId canvas =
        IREntity::createEntity(IRComponents::C_VoxelPool{IRMath::ivec3(1, 1, 1)});
    IRRender::setHeadlessActiveCanvasEntity(canvas);

    auto before = m_lua.lua().safe_script("return IRRender.canAllocateVoxels(1)");
    ASSERT_TRUE(before.valid());
    EXPECT_TRUE(before.get<bool>());

    auto oversized = m_lua.lua().safe_script("return IRRender.canAllocateVoxels(4294967295)");
    ASSERT_TRUE(oversized.valid());
    EXPECT_FALSE(oversized.get<bool>());

    IRComponents::C_VoxelPool &pool = IREntity::getComponent<IRComponents::C_VoxelPool>(canvas);
    pool.allocateVoxels(1);

    auto after = m_lua.lua().safe_script("return IRRender.canAllocateVoxels(1)");
    ASSERT_TRUE(after.valid());
    EXPECT_FALSE(after.get<bool>());
}

TEST_F(LuaRenderBindingsTest, GuiDrawPrimitivesBound) {
    EXPECT_TRUE(isFunction("IRGui.drawDisc"));
    EXPECT_TRUE(isFunction("IRGui.drawLine"));
    EXPECT_TRUE(isFunction("IRGui.fillRect"));
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
    EXPECT_EQ(IREntity::getComponent<IRComponents::C_ZoomLevel>(camera).zoom_, IRMath::vec2(2.0f));
}

// ---- framebuffer glitch -------------------------------------------------------

TEST_F(LuaRenderBindingsTest, GlitchSurfaceBound) {
    EXPECT_TRUE(isFunction("IRRender.setGlitch"));
    EXPECT_TRUE(isFunction("IRRender.glitch"));
}

TEST_F(LuaRenderBindingsTest, GlitchReadsTheOffDefaultsWithoutCreatingState) {
    auto result = m_lua.lua().safe_script(
        R"lua(
        local g = IRRender.glitch()
        return g.intensity, g.period, g.phase, g.seed, g.tear, g.hold, g.split,
               g.bars, g.cells, g.depth, g.holdDivisor, g.timeSeconds, g.tick, g.holdTick
        )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_DOUBLE_EQ(result.get<double>(0), 0.0);
    EXPECT_DOUBLE_EQ(result.get<double>(1), 1.0);
    EXPECT_DOUBLE_EQ(result.get<double>(2), 0.0);
    EXPECT_DOUBLE_EQ(result.get<double>(3), 0.0);
    for (int weight = 4; weight <= 9; ++weight) {
        EXPECT_DOUBLE_EQ(result.get<double>(weight), 0.0) << weight;
    }
    EXPECT_EQ(result.get<int>(10), 1);
    EXPECT_DOUBLE_EQ(result.get<double>(11), 0.0);
    EXPECT_DOUBLE_EQ(result.get<double>(12), 0.0);
    EXPECT_DOUBLE_EQ(result.get<double>(13), 0.0);
    EXPECT_EQ(IRPrefab::Glitch::stateOrNull(), nullptr);
}

TEST_F(LuaRenderBindingsTest, GlitchPartialUpdateKeepsOmittedKeys) {
    auto result = m_lua.lua().safe_script(
        R"lua(
        IRRender.setGlitch{ intensity = 0.5, period = 0.25, phase = -0.125, seed = 4294967295,
                            tear = 1, hold = 0.5, split = 0.25, bars = 0.75, cells = 0.125,
                            depth = 1, holdDivisor = 2 }
        IRRender.setGlitch{ intensity = 1 }
        local g = IRRender.glitch()
        return g.intensity, g.period, g.phase, g.seed, g.tear, g.hold, g.split,
               g.bars, g.cells, g.depth, g.holdDivisor, g.tick, g.holdTick
        )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_DOUBLE_EQ(result.get<double>(0), 1.0);
    EXPECT_DOUBLE_EQ(result.get<double>(1), 0.25);
    EXPECT_DOUBLE_EQ(result.get<double>(2), -0.125);
    EXPECT_DOUBLE_EQ(result.get<double>(3), 4294967295.0);
    EXPECT_DOUBLE_EQ(result.get<double>(4), 1.0);
    EXPECT_DOUBLE_EQ(result.get<double>(5), 0.5);
    EXPECT_DOUBLE_EQ(result.get<double>(6), 0.25);
    EXPECT_DOUBLE_EQ(result.get<double>(7), 0.75);
    EXPECT_DOUBLE_EQ(result.get<double>(8), 0.125);
    EXPECT_DOUBLE_EQ(result.get<double>(9), 1.0);
    EXPECT_EQ(result.get<int>(10), 2);
    // Engine time 0 with phase -0.125: period ticks of 0.25 s give 0, hold
    // ticks of 0.125 s give 1.
    EXPECT_DOUBLE_EQ(result.get<double>(11), 0.0);
    EXPECT_DOUBLE_EQ(result.get<double>(12), 1.0);

    const IRComponents::C_GlitchState *state = IRPrefab::Glitch::stateOrNull();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->seed_, 0xFFFFFFFFu);
    EXPECT_EQ(state->holdDivisor_, 2);
    EXPECT_FLOAT_EQ(state->intensity_, 1.0f);
}

// Every rejected call raises before any field changes, including candidates
// whose scalars are each finite and in range but whose derived tick math is
// not.
TEST_F(LuaRenderBindingsTest, GlitchRejectsBadInputWithoutChangingState) {
    auto setup = m_lua.lua().safe_script(
        "IRRender.setGlitch{ intensity = 0.5, tear = 0.5, period = 2, seed = 7 }",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(setup.valid()) << setup.get<sol::error>().what();
    const IRComponents::C_GlitchState before = *IRPrefab::Glitch::stateOrNull();

    for (const char *call : {
             "IRRender.setGlitch()",
             "IRRender.setGlitch(1)",
             "IRRender.setGlitch{ intesity = 1 }",
             "IRRender.setGlitch{ [1] = 1 }",
             "IRRender.setGlitch{ intensity = 'high' }",
             "IRRender.setGlitch{ intensity = 0/0 }",
             "IRRender.setGlitch{ intensity = 2, tear = 1 }",
             "IRRender.setGlitch{ tear = 1, hold = -0.5 }",
             "IRRender.setGlitch{ tear = 1, period = 0 }",
             "IRRender.setGlitch{ tear = 1, period = math.huge }",
             "IRRender.setGlitch{ tear = 1, phase = -math.huge }",
             "IRRender.setGlitch{ tear = 1, seed = -1 }",
             "IRRender.setGlitch{ tear = 1, seed = 4294967296 }",
             "IRRender.setGlitch{ tear = 1, seed = 1.5 }",
             "IRRender.setGlitch{ tear = 1, holdDivisor = 0 }",
             "IRRender.setGlitch{ tear = 1, holdDivisor = 1.5 }",
             "IRRender.setGlitch{ tear = 1, period = 5e-324, holdDivisor = 2 }",
             "IRRender.setGlitch{ tear = 1, phase = 1e300 }",
             "IRRender.setGlitch{ tear = 1, period = 1e-300, phase = -1 }",
         }) {
        auto result = m_lua.lua().safe_script(call, sol::script_pass_on_error);
        EXPECT_FALSE(result.valid()) << call << " did not raise";
        const IRComponents::C_GlitchState after = *IRPrefab::Glitch::stateOrNull();
        EXPECT_FLOAT_EQ(after.intensity_, before.intensity_) << call;
        EXPECT_FLOAT_EQ(after.tearWeight_, before.tearWeight_) << call;
        EXPECT_FLOAT_EQ(after.holdWeight_, before.holdWeight_) << call;
        EXPECT_DOUBLE_EQ(after.periodSeconds_, before.periodSeconds_) << call;
        EXPECT_DOUBLE_EQ(after.phaseSeconds_, before.phaseSeconds_) << call;
        EXPECT_EQ(after.seed_, before.seed_) << call;
        EXPECT_EQ(after.holdDivisor_, before.holdDivisor_) << call;
    }
}

// ---- canvas glitch subjects --------------------------------------------------

TEST_F(LuaRenderBindingsTest, GlitchSubjectSurfaceBound) {
    EXPECT_TRUE(isFunction("IRRender.setGlitchSubject"));
    EXPECT_TRUE(isFunction("IRRender.clearGlitchSubject"));
    EXPECT_TRUE(isFunction("IRRender.glitchSubject"));
}

TEST_F(LuaRenderBindingsTest, GlitchSubjectSetReadsBackAndClears) {
    const IREntity::EntityId entity = IREntity::createEntity();
    m_lua.lua()["subjectId"] = static_cast<double>(entity);
    auto result = m_lua.lua().safe_script(
        R"lua(
        local before = IRRender.glitchSubject(subjectId)
        IRRender.setGlitchSubject(subjectId, { weight = 0.5, tear = 1, hold = 0.25, split = 0.75,
                                               echo = 0.125, dissolve = 1, spill = 4,
                                               depthNear = -12, depthFar = 30 })
        local g = IRRender.glitchSubject(subjectId)
        return before == nil, g.weight, g.tear, g.hold, g.split, g.echo, g.dissolve, g.spill,
               g.depthNear, g.depthFar
        )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_TRUE(result.get<bool>(0)) << "an unmarked entity reads nil";
    EXPECT_DOUBLE_EQ(result.get<double>(1), 0.5);
    EXPECT_DOUBLE_EQ(result.get<double>(2), 1.0);
    EXPECT_DOUBLE_EQ(result.get<double>(3), 0.25);
    EXPECT_DOUBLE_EQ(result.get<double>(4), 0.75);
    EXPECT_DOUBLE_EQ(result.get<double>(5), 0.125);
    EXPECT_DOUBLE_EQ(result.get<double>(6), 1.0);
    EXPECT_EQ(result.get<int>(7), 4);
    EXPECT_EQ(result.get<int>(8), -12);
    EXPECT_EQ(result.get<int>(9), 30);
    const IRComponents::C_GlitchSubject *stored = IRPrefab::Glitch::subjectOrNull(entity);
    ASSERT_NE(stored, nullptr);
    EXPECT_TRUE(stored->depthEnabled_);

    auto cleared = m_lua.lua().safe_script(
        R"lua(
        IRRender.clearGlitchSubject(subjectId)
        return IRRender.glitchSubject(subjectId) == nil
        )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(cleared.valid()) << cleared.get<sol::error>().what();
    EXPECT_TRUE(cleared.get<bool>());
    EXPECT_EQ(IRPrefab::Glitch::subjectOrNull(entity), nullptr);
}

TEST_F(LuaRenderBindingsTest, GlitchSubjectReplacesTheWholeConfiguration) {
    const IREntity::EntityId entity = IREntity::createEntity();
    m_lua.lua()["subject"] = IRScript::LuaEntity{entity};
    auto result = m_lua.lua().safe_script(
        R"lua(
        IRRender.setGlitchSubject(subject, { hold = 1, spill = 3, depthNear = 1, depthFar = 2 })
        IRRender.setGlitchSubject(subject, { tear = 0.5 })
        local g = IRRender.glitchSubject(subject)
        return g.weight, g.tear, g.hold, g.spill, g.depthNear == nil
        )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_DOUBLE_EQ(result.get<double>(0), 1.0) << "omitted keys take the defaults";
    EXPECT_DOUBLE_EQ(result.get<double>(1), 0.5);
    EXPECT_DOUBLE_EQ(result.get<double>(2), 0.0);
    EXPECT_EQ(result.get<int>(3), 0);
    EXPECT_TRUE(result.get<bool>(4)) << "the depth band is cleared too";
    EXPECT_FALSE(IRPrefab::Glitch::subjectOrNull(entity)->depthEnabled_);
}

TEST_F(LuaRenderBindingsTest, GlitchSubjectSetterAssertsOnAnInvalidSubject) {
    const IREntity::EntityId entity = IREntity::createEntity();
    IRComponents::C_GlitchSubject invalid;
    invalid.spill_ = IRPrefab::Glitch::kSubjectMaxSpill + 1;
    EXPECT_THROW(IRPrefab::Glitch::setSubject(entity, invalid), std::runtime_error);
    EXPECT_EQ(IRPrefab::Glitch::subjectOrNull(entity), nullptr);
}

// Every rejected call raises before the stored subject changes.
TEST_F(LuaRenderBindingsTest, GlitchSubjectRejectsBadInputWithoutChangingState) {
    const IREntity::EntityId entity = IREntity::createEntity();
    m_lua.lua()["subjectId"] = static_cast<double>(entity);
    auto setup = m_lua.lua().safe_script(
        "IRRender.setGlitchSubject(subjectId, { tear = 0.5, spill = 2 })",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(setup.valid()) << setup.get<sol::error>().what();

    for (const char *call : {
             "IRRender.setGlitchSubject(subjectId)",
             "IRRender.setGlitchSubject(subjectId, 1)",
             "IRRender.setGlitchSubject(subjectId, { teer = 1 })",
             "IRRender.setGlitchSubject(subjectId, { [1] = 1 })",
             "IRRender.setGlitchSubject(subjectId, { tear = 'all' })",
             "IRRender.setGlitchSubject(subjectId, { tear = 0/0 })",
             "IRRender.setGlitchSubject(subjectId, { weight = 1.5 })",
             "IRRender.setGlitchSubject(subjectId, { echo = -0.25 })",
             "IRRender.setGlitchSubject(subjectId, { spill = 5 })",
             "IRRender.setGlitchSubject(subjectId, { spill = 1.5 })",
             "IRRender.setGlitchSubject(subjectId, { spill = -1 })",
             "IRRender.setGlitchSubject(subjectId, { depthNear = 3 })",
             "IRRender.setGlitchSubject(subjectId, { depthFar = 3 })",
             "IRRender.setGlitchSubject(subjectId, { depthNear = 4, depthFar = 3 })",
             "IRRender.setGlitchSubject(subjectId, { depthNear = 0.5, depthFar = 3 })",
             "IRRender.setGlitchSubject(subjectId, { depthNear = -math.huge, depthFar = 3 })",
             "IRRender.setGlitchSubject(999999, { tear = 1 })",
             "IRRender.setGlitchSubject(0, { tear = 1 })",
             "IRRender.setGlitchSubject('entity', { tear = 1 })",
             "IRRender.clearGlitchSubject(999999)",
             "IRRender.glitchSubject(999999)",
         }) {
        auto result = m_lua.lua().safe_script(call, sol::script_pass_on_error);
        EXPECT_FALSE(result.valid()) << call << " did not raise";
        const IRComponents::C_GlitchSubject *after = IRPrefab::Glitch::subjectOrNull(entity);
        ASSERT_NE(after, nullptr) << call;
        EXPECT_FLOAT_EQ(after->tearWeight_, 0.5f) << call;
        EXPECT_FLOAT_EQ(after->weight_, 1.0f) << call;
        EXPECT_EQ(after->spill_, 2) << call;
        EXPECT_FALSE(after->depthEnabled_) << call;
    }
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

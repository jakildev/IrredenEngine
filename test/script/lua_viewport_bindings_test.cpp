#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_size_triangles.hpp>
#include <irreden/render/components/component_canvas_camera.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_viewport_camera.hpp>
#include <irreden/render/components/component_viewport_subject.hpp>
#include <irreden/render/components/component_viewport_subject_lua.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/viewport.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/script/lua_viewport_bindings.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

// The `IRRender` viewport service through the sol2 seam. The injected factory
// stands a viewport up on a canvas with no GPU textures, so the suite runs
// without a RenderManager; every other entry reaches the same
// `IRPrefab::Viewport` code a windowed creation does.

namespace {

using IRComponents::C_ViewportCamera;
using IRComponents::C_ViewportSubject;
using IRMath::ivec2;
using IRMath::vec2;
using IRMath::vec3;

IREntity::EntityId createHeadlessViewport(const IRPrefab::Viewport::Desc &desc) {
    const IREntity::EntityId canvas = IREntity::createEntity(
        IRComponents::C_VoxelPool{IRMath::ivec3(0)},
        IRComponents::C_CanvasLocalRotation{},
        IRComponents::C_CanvasCamera{},
        IRComponents::C_SizeTriangles{ivec2(1)}
    );
    return IRPrefab::Viewport::detail::createCamera(canvas, desc);
}

class LuaViewportBindingsTest : public testing::Test {
  protected:
    LuaViewportBindingsTest() {
        IRScript::detail::bindViewport(m_lua, createHeadlessViewport);
    }

    bool scriptSucceeds(const std::string &source) {
        return m_lua.lua().safe_script(source, sol::script_pass_on_error).valid();
    }

    std::string scriptError(const std::string &source) {
        sol::protected_function_result result =
            m_lua.lua().safe_script(source, sol::script_pass_on_error);
        EXPECT_FALSE(result.valid());
        if (result.valid()) {
            return {};
        }
        return sol::error(result).what();
    }

    IREntity::EntityId createViewport(const char *options) {
        sol::protected_function_result result = m_lua.lua().safe_script(
            std::string("return IRRender.createViewport(") + options + ")",
            sol::script_pass_on_error
        );
        EXPECT_TRUE(result.valid());
        return result.valid() ? static_cast<IREntity::EntityId>(result.get<lua_Integer>())
                              : IREntity::kNullEntity;
    }

    static bool isTagged(IREntity::EntityId entity, IREntity::EntityId viewport) {
        auto tag = IREntity::getComponentOptional<C_ViewportSubject>(entity);
        return tag.has_value() && tag.value()->viewport_ == viewport;
    }

    IRScript::LuaScript m_lua; // declared first: destroyed last
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(LuaViewportBindingsTest, ExposesTheCompleteSurface) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        local names = {
            'createViewport', 'setViewportRect', 'setViewportCamera', 'setViewportFocus',
            'setViewportVisible', 'setViewportSubject', 'getViewportSubject', 'destroyViewport'
        }
        for _, name in ipairs(names) do
            assert(type(IRRender[name]) == 'function', name)
        end
    )lua"));
}

TEST_F(LuaViewportBindingsTest, CreateViewportAppliesTheOptionsTable) {
    const IREntity::EntityId viewport = createViewport(
        "{ x = 50, y = 370, width = 180, height = 260, zoom = 16, yaw = math.pi / 2,"
        "  focus = { 0, 0, 1 } }"
    );
    ASSERT_TRUE(IRPrefab::Viewport::isViewport(viewport));
    const auto &camera = IREntity::getComponent<C_ViewportCamera>(viewport);
    EXPECT_EQ(camera.rectOrigin_, ivec2(50, 370));
    EXPECT_EQ(camera.rectSize_, ivec2(180, 260));
    EXPECT_EQ(camera.focus_, vec3(0.0f, 0.0f, 1.0f));
    EXPECT_EQ(IREntity::getComponent<IRComponents::C_ZoomLevel>(viewport).zoom_, vec2(16.0f));
    const IRMath::vec4 rotation =
        IREntity::getComponent<IRComponents::C_LocalTransform>(viewport).rotation_;
    EXPECT_NEAR(rotation.z, IRMath::sin(IRMath::kHalfPi * 0.5f), 1e-6f);
}

TEST_F(LuaViewportBindingsTest, CreateViewportDefaultsAnEmptyTable) {
    const IREntity::EntityId viewport = createViewport("{}");
    ASSERT_TRUE(IRPrefab::Viewport::isViewport(viewport));
    const IRPrefab::Viewport::Desc defaults{};
    const auto &camera = IREntity::getComponent<C_ViewportCamera>(viewport);
    EXPECT_EQ(camera.rectOrigin_, defaults.rectOrigin_);
    EXPECT_EQ(camera.rectSize_, defaults.rectSize_);
    EXPECT_EQ(IREntity::getComponent<IRComponents::C_ZoomLevel>(viewport).zoom_, vec2(1.0f));
}

TEST_F(LuaViewportBindingsTest, SettersMoveResizeRecameraAndHide) {
    const IREntity::EntityId viewport = createViewport("{ width = 100, height = 100 }");
    m_lua.lua()["viewport"] = static_cast<lua_Integer>(viewport);
    EXPECT_TRUE(scriptSucceeds(R"lua(
        IRRender.setViewportRect(viewport, 60, 380, 160, 240)
        IRRender.setViewportCamera(viewport, 8, 0)
        IRRender.setViewportFocus(viewport, 1, 2, 3)
        IRRender.setViewportVisible(viewport, false)
    )lua"));
    const auto &camera = IREntity::getComponent<C_ViewportCamera>(viewport);
    EXPECT_EQ(camera.rectOrigin_, ivec2(60, 380));
    EXPECT_EQ(camera.rectSize_, ivec2(160, 240));
    EXPECT_EQ(camera.focus_, vec3(1.0f, 2.0f, 3.0f));
    EXPECT_FALSE(camera.visible_);
    EXPECT_EQ(IREntity::getComponent<IRComponents::C_ZoomLevel>(viewport).zoom_, vec2(8.0f));
}

TEST_F(LuaViewportBindingsTest, SetViewportSubjectRetargetsAndNilClears) {
    const IREntity::EntityId viewport = createViewport("{}");
    const IREntity::EntityId first = IREntity::createEntity();
    const IREntity::EntityId second = IREntity::createEntity();
    m_lua.lua()["viewport"] = static_cast<lua_Integer>(viewport);
    m_lua.lua()["first"] = static_cast<lua_Integer>(first);
    m_lua.lua()["second"] = IRScript::LuaEntity{second};

    EXPECT_TRUE(scriptSucceeds("IRRender.setViewportSubject(viewport, first)"));
    IREntity::flushStructuralChanges();
    EXPECT_TRUE(isTagged(first, viewport));

    // A LuaEntity is accepted as well as a raw id.
    EXPECT_TRUE(scriptSucceeds("IRRender.setViewportSubject(viewport, second)"));
    IREntity::flushStructuralChanges();
    EXPECT_FALSE(isTagged(first, viewport));
    EXPECT_TRUE(isTagged(second, viewport));

    EXPECT_TRUE(scriptSucceeds("IRRender.setViewportSubject(viewport, nil)"));
    IREntity::flushStructuralChanges();
    EXPECT_FALSE(isTagged(second, viewport));
}

TEST_F(LuaViewportBindingsTest, GetViewportSubjectReportsTheDrawnEntityOrNil) {
    const IREntity::EntityId viewport = createViewport("{}");
    m_lua.lua()["viewport"] = static_cast<lua_Integer>(viewport);
    EXPECT_TRUE(scriptSucceeds("assert(IRRender.getViewportSubject(viewport) == nil)"));

    const IREntity::EntityId subject = IREntity::createEntity();
    IREntity::getComponent<C_ViewportCamera>(viewport).drawnSubject_ = subject;
    m_lua.lua()["subject"] = static_cast<lua_Integer>(subject);
    EXPECT_TRUE(scriptSucceeds("assert(IRRender.getViewportSubject(viewport) == subject)"));
}

TEST_F(LuaViewportBindingsTest, DestroyViewportRemovesItAndLaterCallsRaise) {
    const IREntity::EntityId viewport = createViewport("{}");
    const IREntity::EntityId canvas = IRPrefab::Viewport::canvasOf(viewport);
    m_lua.lua()["viewport"] = static_cast<lua_Integer>(viewport);

    EXPECT_TRUE(scriptSucceeds("IRRender.destroyViewport(viewport)"));
    m_entity_manager.destroyMarkedEntities();
    EXPECT_FALSE(IREntity::entityExists(viewport));
    EXPECT_FALSE(IREntity::entityExists(canvas));

    const std::string error = scriptError("IRRender.setViewportRect(viewport, 0, 0, 1, 1)");
    EXPECT_NE(error.find("not a live viewport id"), std::string::npos);
}

TEST_F(LuaViewportBindingsTest, InvalidIdsAndSubjectsRaiseNamedErrors) {
    const IREntity::EntityId viewport = createViewport("{}");
    m_lua.lua()["viewport"] = static_cast<lua_Integer>(viewport);
    // A live entity that is not a viewport.
    m_lua.lua()["plain"] = static_cast<lua_Integer>(IREntity::createEntity());

    for (const char *source : {
             "IRRender.setViewportRect(0, 0, 0, 1, 1)",
             "IRRender.setViewportCamera(plain, 2, 0)",
             "IRRender.setViewportFocus(-3, 0, 0, 0)",
             "IRRender.setViewportVisible(plain, true)",
             "IRRender.getViewportSubject(plain)",
             "IRRender.destroyViewport(plain)",
         }) {
        SCOPED_TRACE(source);
        EXPECT_NE(scriptError(source).find("not a live viewport id"), std::string::npos);
    }
    for (const char *source : {
             "IRRender.setViewportSubject(viewport, 999999)",
             "IRRender.setViewportSubject(viewport, 'hero')",
             "IRRender.setViewportSubject(viewport, 0)",
         }) {
        SCOPED_TRACE(source);
        EXPECT_NE(scriptError(source).find("must be a live entity or nil"), std::string::npos);
    }
}

TEST_F(LuaViewportBindingsTest, ViewportSubjectComponentIsConstructibleFromLua) {
    m_lua.registerTypeFromTraits<C_ViewportSubject>();
    const IREntity::EntityId viewport = createViewport("{}");
    m_lua.lua()["viewport"] = static_cast<lua_Integer>(viewport);
    EXPECT_TRUE(scriptSucceeds(R"lua(
        local tag = C_ViewportSubject.new(viewport)
        assert(tag.viewport == viewport)
        assert(C_ViewportSubject.new().viewport == 0)
    )lua"));
}

TEST(LuaViewportBindingRegistrationTest, ExtendsAnExistingRenderTableAndIsOptIn) {
    IRScript::LuaScript lua;
    lua.bindLuaDrivenEcs();
    EXPECT_TRUE(lua.lua().safe_script("assert(IRRender.createViewport == nil)").valid());
    lua.lua().safe_script("IRRender.custom = 17");

    lua.bindLuaViewport();
    lua.bindLuaViewport();
    EXPECT_TRUE(lua.lua()
                    .safe_script(R"lua(
        assert(type(IRRender.createViewport) == 'function')
        assert(type(IRRender.setSunDirection) == 'function')
        assert(IRRender.custom == 17)
    )lua")
                    .valid());
}

TEST(LuaViewportBindingRegistrationTest, NonTableCollisionRaisesWithoutReplacingGlobal) {
    IRScript::LuaScript lua;
    lua.lua()["IRRender"] = 41;
    EXPECT_THROW(lua.bindLuaViewport(), std::invalid_argument);
    EXPECT_EQ(lua.lua()["IRRender"].get<int>(), 41);
}

} // namespace

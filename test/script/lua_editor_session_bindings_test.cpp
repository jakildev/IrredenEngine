#include <gtest/gtest.h>

#include "../../creations/editors/voxel_editor/editor_lua_host.hpp"

#include <tuple>

namespace IRVoxelEditor::Session {

#define IR_SESSION_PREDICATE_STUB(name)                                                            \
    bool name(const void *, std::string &) {                                                       \
        return true;                                                                               \
    }
IR_SESSION_PREDICATE_STUB(evaluateOccupancyCheck)
IR_SESSION_PREDICATE_STUB(evaluatePartTransformCheck)
IR_SESSION_PREDICATE_STUB(evaluatePartCountCheck)
IR_SESSION_PREDICATE_STUB(evaluatePartIdCheck)
IR_SESSION_PREDICATE_STUB(evaluatePartAuthoredCountCheck)
IR_SESSION_PREDICATE_STUB(evaluatePartEditableCheck)
IR_SESSION_PREDICATE_STUB(evaluateCanvasCountCheck)
IR_SESSION_PREDICATE_STUB(evaluateRotationModeCheck)
IR_SESSION_PREDICATE_STUB(evaluatePartYawCheck)
IR_SESSION_PREDICATE_STUB(evaluatePartTurnAxisCheck)
IR_SESSION_PREDICATE_STUB(evaluatePartSpinCheck)
IR_SESSION_PREDICATE_STUB(evaluatePartAnchorCheck)
IR_SESSION_PREDICATE_STUB(evaluatePartModeDropdownCheck)
IR_SESSION_PREDICATE_STUB(evaluateEditorInputCheck)
IR_SESSION_PREDICATE_STUB(evaluatePartsListCheck)
IR_SESSION_PREDICATE_STUB(evaluateCameraZoomCheck)
IR_SESSION_PREDICATE_STUB(evaluatePickCheck)
IR_SESSION_PREDICATE_STUB(evaluatePickMatchesRender)
IR_SESSION_PREDICATE_STUB(evaluateFillGhostCheck)
IR_SESSION_PREDICATE_STUB(evaluateSliderCheck)
IR_SESSION_PREDICATE_STUB(evaluateComponentCheck)
IR_SESSION_PREDICATE_STUB(evaluatePanelLabelCheck)
IR_SESSION_PREDICATE_STUB(evaluatePartGateCheck)
IR_SESSION_PREDICATE_STUB(evaluateManifestCheck)
IR_SESSION_PREDICATE_STUB(evaluateComponentValueCheck)
IR_SESSION_PREDICATE_STUB(evaluatePartExtentCheck)
IR_SESSION_PREDICATE_STUB(evaluateMirrorCheck)
IR_SESSION_PREDICATE_STUB(evaluateLoftMaskCheck)
#undef IR_SESSION_PREDICATE_STUB

} // namespace IRVoxelEditor::Session

namespace {

using IRVoxelEditor::LuaSession::Handle;
using IRVoxelEditor::LuaSession::Lifetime;
using IRVoxelEditor::Session::Builder;

void expectOneErrorAndNoEvents(
    const IRVoxelEditor::Session::Recipe &recipe, std::string_view token
) {
    ASSERT_EQ(recipe.errors_.size(), 1u);
    EXPECT_NE(recipe.errors_.front().find(token), std::string::npos);
    EXPECT_TRUE(recipe.segments_.empty());
}

class LuaEditorSessionBindingsTest : public testing::Test {
  protected:
    LuaEditorSessionBindingsTest() {
        m_lua.bindLuaDrivenEcs();
        m_host.bind(m_lua);
        const sol::protected_function_result component = m_lua.lua().safe_script(R"lua(
            IRComponent.register("MatrixComponent", {
                count = 1,
                ratio = { type = "float", default = 1 },
                enabled = true,
                title = "value",
                point = { type = "vec3" },
                cell = { type = "ivec3" },
                rotation = { type = "vec4" },
                callback = function() end,
                opaque = { type = "table", default = {} },
            })
            IREditor.registerRecipe("matrix_recipe", {
                { name = "count", type = IREditor.ParamType.INT, default = 1, min = 0, max = 28 },
            }, function(values, size)
                return { { 1, 1, 1 } }
            end)
            IREditor.registerRecipe("interior_float", {
                { name = "value", type = IREditor.ParamType.FLOAT, default = 0, min = -1, max = 1 },
            }, function(values, size)
                if values.value > 0.004 and values.value < 0.006 then
                    return { { 2, 2, 2 } }
                end
                return {}
            end)
            IREditor.registerRecipe("joint_float", {
                { name = "a", type = IREditor.ParamType.FLOAT, default = 0, min = -1, max = 1 },
                { name = "b", type = IREditor.ParamType.FLOAT, default = 0, min = -1, max = 1 },
            }, function(values, size)
                if values.a > 0.004 and values.b > 0.004 then
                    return { { 3, 3, 3 } }
                end
                return {}
            end)
            IREditor.registerRecipe("wide_int", {
                { name = "value", type = IREditor.ParamType.INT, default = 1, min = 0, max = 29 },
            }, function(values, size) return {} end)
        )lua");
        if (!component.valid()) {
            const sol::error error = component;
            throw std::runtime_error(error.what());
        }
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    IRVoxelEditor::ModuleHost m_host;
};

TEST_F(LuaEditorSessionBindingsTest, EveryBuilderMethodForwardsThroughLua) {
    IRVoxelEditor::ModuleSessionView view = m_host.sessionView();
    view.components_.push_back(
        IRVoxelEditor::ComponentRecord{"EngineComponent", IRVoxelEditor::ComponentSource::ENGINE}
    );
    Builder builder("binding_matrix", IRMath::ivec3(16), IRMath::vec3(-8.0f), &view);
    const std::shared_ptr<Lifetime> lifetime =
        std::make_shared<Lifetime>(Lifetime{&builder, &m_lua});
    m_lua.lua()["session"] = Handle{lifetime};

    const sol::protected_function_result result = m_lua.lua().safe_script(R"lua(
        local s = session
        assert(s:sceneSize().x == 16)
        assert(s:sceneOrigin().x == -8)
        s:withReferenceFurniture()
        s:segment("matrix", 4)
        s:click({8, 8, 14})
        s:clickExpectingNoEdit({9, 8, 14})
        s:clickFace({6, 6, 15}, {0, 0, -1})
        s:click({8, 8, 13})
        s:click({8, 8, 12})
        s:click({7, 8, 12})
        s:click({6, 8, 12})
        s:clickBelow({6, 8, 13})
        s:hover({5, 8, 12})
        s:dragBox({10, 8, 14}, {13, 8, 14})
        s:holdBox({10, 6, 14}, {13, 6, 14})
        s:releaseBox()
        s:tapKey(IRInput.Key.SPACE)
        s:chordKey(IRInput.Key.LEFT_CONTROL, IRInput.Key.S)
        s:toggleEraseMode()
        s:toggleEraseMode()
        s:enableSymmetry(false, false, false)
        s:addLayer()
        s:selectPrevLayer()
        s:selectNextLayer()
        s:toggleActiveLayerVisibility()
        s:toggleActiveLayerVisibility()
        s:selectPaletteSwatch(1)
        s:clickGui({x = 20, y = 20})
        s:wheelGui({20, 20}, 1)
        s:wheelScene({5, 8, 12}, -1)
        s:typeText({20, 20}, "Matrix 1")
        s:dragGuiSlider({pos = {10, 10}, size = {100, 14}}, 0, 10, 5)
        s:applyRecipe("matrix_recipe", { count = 2 })
        s:attachComponent("MatrixComponent")
        s:setComponentField("MatrixComponent", "count", "7")
        s:attachComponent("EngineComponent")
        s:setComponentOverrides("EngineComponent", "{}")
        s:duplicateFrame()
        s:prevFrame()
        s:nextFrame()
        s:save()
        s:reload()
        s:setPartSize({8, 8, 8})
        s:addVoxelPart()
        s:dragPartGizmo({0, 0, 0}, {1, 0, 0}, {1, 0, 0})
        s:selectPartMode(0)
        s:bakeSphere(2)
        s:nextPart()
        s:toggleLoftMode()
        s:clickLoftCell(IREditor.LoftMask.XZ, {0, 0})
        s:stampLoft()
        s:toggleLoftMode()
        s:removeSelectedPart()
        s:undoRemovedPart()
        s:removeSelectedPartWithButton()
        s:undoRemovedPart()
        s:dragLodSlider(IREditor.SliderTarget.LOD_FINE, 2)
        s:toggleLodFollowZoom()
        s:dragWorld({0, 0, 0}, {1, 0, 0})
        s:applyRadialArray(2)
        s:applyRadialArray(2, 8)
        s:applyRadialArrayToLiveScene(2)
        s:applyRadialArrayToLiveScene(2, -8)
        s:toggleRotationalSymmetry()
        s:selectPartThroughList(0, 0)

        s:expectOccupancy({8, 8, 14}, true, "occupancy")
        s:expectPartOccupancy(0, {4, 4, 2}, true, "part_occupancy")
        s:expectPartTransform(0, {0, 0, 0}, 1, false, "part_transform")
        s:expectPartCount(1, "part_count")
        s:expectPartId(0, "part_1", "part_id")
        s:expectPartAuthoredCount(0, 1, "authored_count")
        s:expectPartEditable(0, true, "editable")
        s:expectSelectedPartVisible(0, "selected")
        s:expectPartsListTop(0, "list_top")
        s:expectCameraZoom(4, true, "camera_zoom")
        s:expectCanvasCount(0, "canvas_count")
        s:expectRotationMode(0, 0, "rotation_mode")
        s:expectPartYaw(0, 60, 1, "part_yaw")
        s:expectPartTurnAxis(0, {0, 1, 0}, 10, "part_turn_axis")
        s:expectPartSpinRate(0, 0.02, true, "part_spin_rate")
        s:expectPartAnchor(0, IRComponent.EntityAnchor.CENTER, "part_anchor")
        s:expectPartModeDropdown(0, "part_mode_dropdown")
        s:expectEditorInput(IREditor.EditorInputCheckKind.SELECTED_PART_NO_TEXT_FOCUS,
                            0, "editor_input")
        s:expectPoolActive({8, 8, 14}, true, "pool_active")
        s:expectVoxelColor({8, 8, 14}, 1, "voxel_color")
        s:expectPick({8, 8, 14}, "pick")
        s:expectPickMatchesRender("pick_render")
        s:expectFillGhost({10, 6, 14}, {13, 6, 14}, "fill_ghost")
        s:expectNoFillGhost("no_fill_ghost")
        s:expectSliderValue(IREditor.SliderTarget.FPS, 12, 1, "slider")
        s:expectPartGated(0, false, -1, "part_gated")
        s:expectPreviewGated(false, -1, "preview_gated")
        s:expectPartExtent(0, {8, 8, 8}, "extent")
        s:expectMirrorOffsets({0, 0, 0}, "mirrors")
        s:expectLoftMask(IREditor.LoftMask.XZ, {0, 0}, true, "loft")
        s:expectManifestContains("parts", "manifest_has")
        s:expectManifestOmits("missing", "manifest_omits")
        s:expectComponentRegistered("MatrixComponent", 9, "component_registered")
        s:expectPanelLabel("Panel", "Label", "panel_label")
        s:expectComponentValue(0, "MatrixComponent", "count", 1, true, "component_value")
        s:clearEntityScene()
        s:discardRemovedPart()
    )lua");
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    const IRVoxelEditor::Session::Recipe recipe = builder.finish();
    EXPECT_TRUE(recipe.ok()) << (recipe.errors_.empty() ? "" : recipe.errors_.front());
}

TEST_F(LuaEditorSessionBindingsTest, HeldBoxNeedsExactlyOneRelease) {
    struct Case {
        const char *script_;
        const char *error_;
    };
    const Case cases[] = {
        {"session:holdBox({10, 6, 14}, {13, 6, 14})", "ends with a box held"},
        {"session:releaseBox()", "releaseBox with no box held"},
        {"session:holdBox({10, 6, 14}, {13, 6, 14}); session:holdBox({10, 4, 14}, {13, 4, 14})",
         "holdBox while a box is held"},
        {"session:holdBox({10, 6, 14}, {13, 6, 14}); session:dragBox({10, 4, 14}, {13, 4, 14})",
         "dragBox while a box is held"},
    };
    for (const Case &testCase : cases) {
        Builder builder("held", IRMath::ivec3(16), IRMath::vec3(-8.0f));
        const std::shared_ptr<Lifetime> lifetime =
            std::make_shared<Lifetime>(Lifetime{&builder, &m_lua});
        m_lua.lua()["session"] = Handle{lifetime};
        ASSERT_TRUE(m_lua.lua().safe_script(testCase.script_).valid()) << testCase.script_;
        const IRVoxelEditor::Session::Recipe recipe = builder.finish();
        ASSERT_FALSE(recipe.ok()) << testCase.script_;
        EXPECT_NE(recipe.errors_.front().find(testCase.error_), std::string::npos)
            << testCase.script_ << " -> " << recipe.errors_.front();
    }
}

// dragBox is holdBox then releaseBox. Split across a segment boundary, the
// release is the later segment's only event, on its first frame.
TEST_F(LuaEditorSessionBindingsTest, HoldThenReleaseEmitsTheDragBoxStream) {
    using IRVideo::GuiInputEvent;
    const auto build = [this](const char *script) {
        Builder builder("stream", IRMath::ivec3(16), IRMath::vec3(-8.0f));
        const std::shared_ptr<Lifetime> lifetime =
            std::make_shared<Lifetime>(Lifetime{&builder, &m_lua});
        m_lua.lua()["session"] = Handle{lifetime};
        EXPECT_TRUE(m_lua.lua().safe_script(script).valid()) << script;
        return builder.finish();
    };
    const IRVoxelEditor::Session::Recipe dragged =
        build("session:dragBox({10, 6, 14}, {13, 6, 14})");
    const IRVoxelEditor::Session::Recipe held =
        build("session:holdBox({10, 6, 14}, {13, 6, 14}); session:releaseBox()");
    const IRVoxelEditor::Session::Recipe split = build(
        "session:holdBox({10, 6, 14}, {13, 6, 14}); session:segment('release'); "
        "session:releaseBox()"
    );
    ASSERT_TRUE(dragged.ok());
    ASSERT_TRUE(held.ok());
    ASSERT_TRUE(split.ok());
    ASSERT_EQ(dragged.segments_.size(), 1u);
    ASSERT_EQ(held.segments_.size(), 1u);
    ASSERT_EQ(split.segments_.size(), 2u);

    const std::vector<GuiInputEvent> &dragEvents = dragged.segments_[0].events_;
    const std::vector<GuiInputEvent> &heldEvents = held.segments_[0].events_;
    ASSERT_EQ(dragEvents.size(), 4u);
    ASSERT_EQ(heldEvents.size(), dragEvents.size());
    for (std::size_t i = 0; i < dragEvents.size(); ++i) {
        EXPECT_EQ(heldEvents[i].type_, dragEvents[i].type_) << i;
        EXPECT_EQ(heldEvents[i].frameOffset_, dragEvents[i].frameOffset_) << i;
        EXPECT_EQ(heldEvents[i].button_, dragEvents[i].button_) << i;
    }
    EXPECT_EQ(dragEvents.back().type_, GuiInputEvent::Type::RELEASE);

    const std::vector<GuiInputEvent> &pressEvents = split.segments_[0].events_;
    ASSERT_EQ(pressEvents.size(), 3u);
    EXPECT_EQ(pressEvents[1].type_, GuiInputEvent::Type::PRESS);
    EXPECT_EQ(pressEvents.back().type_, GuiInputEvent::Type::MOVE);
    const std::vector<GuiInputEvent> &releaseEvents = split.segments_[1].events_;
    ASSERT_EQ(releaseEvents.size(), 1u);
    EXPECT_EQ(releaseEvents[0].type_, GuiInputEvent::Type::RELEASE);
    EXPECT_EQ(releaseEvents[0].frameOffset_, 0);
    EXPECT_EQ(releaseEvents[0].button_, IRInput::kMouseButtonLeft);
}

TEST_F(LuaEditorSessionBindingsTest, InvalidatesRetainedHandleAfterReturnAndRaise) {
    for (const char *body : {"retained = session", "retained = session; error('raised')"}) {
        Builder builder("escape", IRMath::ivec3(16), IRMath::vec3(-8.0f));
        const std::shared_ptr<Lifetime> lifetime =
            std::make_shared<Lifetime>(Lifetime{&builder, &m_lua});
        m_lua.lua()["session"] = Handle{lifetime};
        const sol::protected_function_result call =
            m_lua.lua().safe_script(body, sol::script_pass_on_error);
        lifetime->builder_ = nullptr;
        const sol::protected_function_result escaped =
            m_lua.lua().safe_script("return retained:sceneSize()", sol::script_pass_on_error);
        EXPECT_FALSE(escaped.valid());
        const sol::error error = escaped;
        EXPECT_NE(std::string(error.what()).find("session handle used outside"), std::string::npos);
        if (std::string(body).find("error") == std::string::npos)
            EXPECT_TRUE(call.valid());
        else
            EXPECT_FALSE(call.valid());
    }
}

TEST_F(LuaEditorSessionBindingsTest, RejectsEveryArgumentFamilyAndNonComparableFields) {
    Builder builder("negative", IRMath::ivec3(16), IRMath::vec3(-8.0f));
    const std::shared_ptr<Lifetime> lifetime =
        std::make_shared<Lifetime>(Lifetime{&builder, &m_lua});
    m_lua.lua()["session"] = Handle{lifetime};
    const char *calls[] = {
        "session:segment({}, 4)",
        "session:segment('Bad Label', 4)",
        "session:segment('x', {})",
        "session:click({x=1,y=2,z='bad'})",
        "session:holdBox({1,2,3}, {1,2})",
        "session:expectFillGhost({1,2,'bad'}, {1,2,3}, 'bad')",
        "session:clickLoftCell(99, {0,0})",
        "session:dragGuiSlider({pos={0,0},size='bad'}, 0, 1, 0)",
        "session:dragWorld({0,0}, {1,1,1})",
        "session:applyRadialArray(2, 8.5)",
        "session:applyRadialArray(2, 13)",
        "session:applyRadialArrayToLiveScene(2, 8.5)",
        "session:applyRadialArrayToLiveScene(2, 13)",
        "session:applyRecipe({}, {})",
        "session:applyRecipe('matrix_recipe', 1)",
        "session:applyRecipe('matrix_recipe', { count = 'bad' })",
        "session:attachComponent({})",
        "session:setComponentField('MatrixComponent', {}, '1')",
        "session:setComponentOverrides('EngineComponent', {})",
        "session:clickGui({0})",
        "session:tapKey(9999)",
        "session:expectRotationMode(0, 9999, 'bad')",
        "session:expectPartTurnAxis(0, {0,1}, 10, 'bad')",
        "session:expectPartAnchor(0, 9999, 'bad')",
        "session:selectPartMode(9999)",
        "session:expectPartModeDropdown(9999, 'bad')",
        "session:expectComponentValue(0, 'MatrixComponent', 'point', {x=1}, true, 'bad')",
        "session:expectComponentValue(0, 'MatrixComponent', 'callback', nil, true, 'bad')",
        "session:expectComponentValue(0, 'MatrixComponent', 'opaque', {}, true, 'bad')",
    };
    for (const char *call : calls) {
        const sol::protected_function_result result =
            m_lua.lua().safe_script(call, sol::script_pass_on_error);
        EXPECT_FALSE(result.valid()) << call;
    }
}

TEST_F(LuaEditorSessionBindingsTest, RadialArrayRejectsOutOfRangeRadiusBeforeEvents) {
    Builder radial("radial_radius", IRMath::ivec3(16), IRMath::vec3(-8.0f));
    radial.applyRadialArray(2, 13);
    expectOneErrorAndNoEvents(radial.finish(), "radius");

    Builder live("live_radius", IRMath::ivec3(16), IRMath::vec3(-8.0f));
    live.applyRadialArrayToLiveScene(2, -13);
    expectOneErrorAndNoEvents(live.finish(), "radius");
}

TEST_F(LuaEditorSessionBindingsTest, RadialRadiusSliderRoundsWholeTargetsInsideTwoTrixels) {
    constexpr int kSixteenthsPerTrixel = 16;
    for (int target = static_cast<int>(IRVoxelEditor::kArrayRadiusSliderMin);
         target <= static_cast<int>(IRVoxelEditor::kArrayRadiusSliderMax);
         ++target) {
        const IRMath::vec2 aim = IRVoxelEditor::sliderValueGuiTrixel(
            IRVoxelEditor::kArrayRadiusSliderGeometry,
            IRVoxelEditor::kArrayRadiusSliderMin,
            IRVoxelEditor::kArrayRadiusSliderMax,
            static_cast<float>(target)
        );
        for (int offset = -2 * kSixteenthsPerTrixel + 1; offset < 2 * kSixteenthsPerTrixel;
             ++offset) {
            const float cursor = aim.x + static_cast<float>(offset) / kSixteenthsPerTrixel;
            const float held =
                IRVoxelEditor::kArrayRadiusSliderMin +
                (cursor - static_cast<float>(IRVoxelEditor::kArrayRadiusSliderGeometry.pos_.x)) /
                    static_cast<float>(IRVoxelEditor::kArrayRadiusSliderGeometry.size_.x) *
                    (IRVoxelEditor::kArrayRadiusSliderMax - IRVoxelEditor::kArrayRadiusSliderMin);
            EXPECT_EQ(IRMath::round(held), target) << "target=" << target << " offset=" << offset;
        }
    }
}

TEST_F(LuaEditorSessionBindingsTest, RecordErrorForwardsAsRecipeFailure) {
    Builder builder("record_error", IRMath::ivec3(16), IRMath::vec3(-8.0f));
    const std::shared_ptr<Lifetime> lifetime =
        std::make_shared<Lifetime>(Lifetime{&builder, &m_lua});
    m_lua.lua()["session"] = Handle{lifetime};
    ASSERT_TRUE(m_lua.lua().safe_script("session:recordError('matrix error')").valid());
    const IRVoxelEditor::Session::Recipe recipe = builder.finish();
    ASSERT_FALSE(recipe.ok());
    EXPECT_EQ(recipe.errors_.front(), "matrix error");
}

TEST_F(LuaEditorSessionBindingsTest, ApplyRecipeUpdatesTheModelBeforeLaterClicks) {
    const IRVoxelEditor::ModuleSessionView view = m_host.sessionView();
    Builder applied("applied", IRMath::ivec3(16), IRMath::vec3(-8.0f), &view);
    applied.applyRecipe("matrix_recipe", {{"count", 2.0f}});
    applied.click(IRMath::ivec3(0, 1, 1));
    EXPECT_TRUE(applied.finish().ok());

    Builder missing("missing", IRMath::ivec3(16), IRMath::vec3(-8.0f), &view);
    missing.click(IRMath::ivec3(0, 1, 1));
    expectOneErrorAndNoEvents(missing.finish(), "no camera-facing face");
}

TEST_F(LuaEditorSessionBindingsTest, ApplyRecipeRejectsUnprovableSliderValues) {
    const IRVoxelEditor::ModuleSessionView view = m_host.sessionView();
    auto recipeIndex = [&view](const std::string &name) {
        return static_cast<std::size_t>(
            std::find_if(
                view.recipes_.begin(),
                view.recipes_.end(),
                [&name](const IRVoxelEditor::ModuleRecipe &recipe) { return recipe.name_ == name; }
            ) -
            view.recipes_.begin()
        );
    };
    auto evaluate = [&view, &recipeIndex](const std::string &name, std::vector<float> values) {
        std::vector<IRVoxelEditor::RecipeCell> cells;
        std::string error;
        EXPECT_TRUE(view.evaluate_(recipeIndex(name), values, IRMath::ivec3(16), cells, error));
        return cells;
    };

    EXPECT_TRUE(evaluate("interior_float", {0.0f}).empty());
    EXPECT_TRUE(evaluate("interior_float", {-2.0f / 112.0f}).empty());
    EXPECT_TRUE(evaluate("interior_float", {2.0f / 112.0f}).empty());
    EXPECT_EQ(evaluate("interior_float", {0.005f}).size(), 1u);
    EXPECT_TRUE(evaluate("joint_float", {2.0f / 112.0f, 0.0f}).empty());
    EXPECT_TRUE(evaluate("joint_float", {0.0f, 2.0f / 112.0f}).empty());
    EXPECT_EQ(evaluate("joint_float", {2.0f / 112.0f, 2.0f / 112.0f}).size(), 1u);

    for (const auto &[name, values, token, neighbour] : std::vector<std::tuple<
             std::string,
             std::vector<std::pair<std::string, float>>,
             std::string,
             IRMath::ivec3>>{
             {"interior_float", {{"value", 0.0f}}, "'value'", IRMath::ivec3(1, 2, 2)},
             {"joint_float", {{"a", 0.0f}, {"b", 0.0f}}, "'a'", IRMath::ivec3(2, 3, 3)},
         }) {
        Builder builder("reject", IRMath::ivec3(16), IRMath::vec3(-8.0f), &view);
        builder.applyRecipe(name, values);
        builder.click(neighbour);
        const IRVoxelEditor::Session::Recipe result = builder.finish();
        ASSERT_EQ(result.errors_.size(), 2u) << name;
        EXPECT_NE(result.errors_.front().find(token), std::string::npos) << name;
        EXPECT_NE(result.errors_.back().find("no camera-facing face"), std::string::npos) << name;
        EXPECT_TRUE(result.segments_.empty()) << name;
    }

    for (const auto &[name, values, token] : std::vector<
             std::tuple<std::string, std::vector<std::pair<std::string, float>>, std::string>>{
             {"wide_int", {{"value", 1.0f}}, "'value'"},
             {"matrix_recipe", {{"count", 1.5f}}, "'count'"},
             {"matrix_recipe", {{"count", 1.0e20f}}, "outside its range"},
         }) {
        Builder builder("reject", IRMath::ivec3(16), IRMath::vec3(-8.0f), &view);
        builder.applyRecipe(name, values);
        expectOneErrorAndNoEvents(builder.finish(), token);
    }

    IRVoxelEditor::ModuleSessionView fractionalDefault = view;
    fractionalDefault.recipes_.front().params_.front().default_ = 1.5f;
    Builder
        builder("fractional_default", IRMath::ivec3(16), IRMath::vec3(-8.0f), &fractionalDefault);
    builder.applyRecipe("matrix_recipe", {});
    expectOneErrorAndNoEvents(builder.finish(), "'count'");
}

TEST_F(LuaEditorSessionBindingsTest, BuilderOperationsRejectEveryUnsupportedRequest) {
    const IRVoxelEditor::ModuleSessionView view = m_host.sessionView();
    auto expectRejected = [&view](auto operation, std::string_view token) {
        Builder builder("reject", IRMath::ivec3(16), IRMath::vec3(-8.0f), &view);
        operation(builder);
        expectOneErrorAndNoEvents(builder.finish(), token);
    };

    expectRejected([](Builder &builder) { builder.applyRecipe("missing", {}); }, "'missing'");
    expectRejected([](Builder &builder) { builder.attachComponent("missing"); }, "'missing'");
    expectRejected(
        [](Builder &builder) { builder.setComponentField("MatrixComponent", "missing", "1"); },
        "'missing'"
    );
    expectRejected(
        [](Builder &builder) { builder.applyRecipe("matrix_recipe", {{"missing", 1.0f}}); },
        "'missing'"
    );
    expectRejected(
        [](Builder &builder) { builder.applyRecipe("matrix_recipe", {{"count", 29.0f}}); },
        "outside its range"
    );

    IRVoxelEditor::ModuleSessionView components = view;
    components.components_.push_back(
        IRVoxelEditor::ComponentRecord{"EngineComponent", IRVoxelEditor::ComponentSource::ENGINE}
    );
    Builder engineField("engine_field", IRMath::ivec3(16), IRMath::vec3(-8.0f), &components);
    engineField.setComponentField("EngineComponent", "field", "1");
    expectOneErrorAndNoEvents(engineField.finish(), "not a module component");
    expectRejected(
        [](Builder &builder) { builder.setComponentField("MatrixComponent", "enabled", "true"); },
        "'enabled'"
    );
    expectRejected(
        [](Builder &builder) {
            builder.setComponentField("MatrixComponent", "callback", "function() end");
        },
        "'callback'"
    );
    expectRejected(
        [](Builder &builder) { builder.setComponentField("MatrixComponent", "opaque", "{}"); },
        "'opaque'"
    );
    expectRejected(
        [](Builder &builder) { builder.setComponentField("MatrixComponent", "count", "bad"); },
        "cannot hold"
    );
    expectRejected(
        [](Builder &builder) { builder.setComponentOverrides("MatrixComponent", "{}"); },
        "not an engine component"
    );

    for (const auto &operation : std::vector<std::function<void(Builder &)>>{
             [](Builder &builder) { builder.applyRecipe("matrix_recipe", {}); },
             [](Builder &builder) { builder.attachComponent("MatrixComponent"); },
             [](Builder &builder) { builder.setComponentField("MatrixComponent", "count", "1"); },
             [](Builder &builder) { builder.setComponentOverrides("EngineComponent", "{}"); },
         }) {
        Builder noView("no_view", IRMath::ivec3(16), IRMath::vec3(-8.0f));
        operation(noView);
        expectOneErrorAndNoEvents(noView.finish(), "needs a loaded module");
    }
}

// The wheel a segment turns, in rows toward the list's end, and the GUI point
// its first click aims at.
struct ListGesture {
    int wheelRows_ = 0;
    IRMath::vec2 firstClick_ = IRMath::vec2(-1.0f);
};

ListGesture listGesture(const IRVoxelEditor::Session::Segment &segment) {
    using IRVideo::GuiInputEvent;
    ListGesture gesture;
    int press = -1;
    for (std::size_t i = 0; i < segment.events_.size(); ++i) {
        const GuiInputEvent &event = segment.events_[i];
        if (event.type_ == GuiInputEvent::Type::SCROLL)
            gesture.wheelRows_ += event.scroll_.y < 0.0f ? 1 : -1;
        if (event.type_ == GuiInputEvent::Type::PRESS && press < 0)
            press = static_cast<int>(i);
    }
    for (const IRVoxelEditor::Session::GuiAimFixup &aim : segment.guiAims_) {
        if (aim.eventIndex_ == press - 1)
            gesture.firstClick_ = aim.guiTrixel_;
    }
    return gesture;
}

TEST_F(LuaEditorSessionBindingsTest, NamedListOperationsScrollTheirLists) {
    using IRVoxelEditor::componentListRowCenterGuiTrixel;
    using IRVoxelEditor::ComponentRecord;
    using IRVoxelEditor::ComponentSource;
    using IRVoxelEditor::recipeListRowCenterGuiTrixel;
    const int recipeRows = IRVoxelEditor::recipeListVisibleRows();
    const int componentRows = IRVoxelEditor::componentListVisibleRows();

    IRVoxelEditor::ModuleSessionView view;
    for (int i = 0; i < recipeRows + 2; ++i)
        view.recipes_.push_back(IRVoxelEditor::ModuleRecipe{"r" + std::to_string(i), {}});
    view.evaluate_ = [](std::size_t,
                        const std::vector<float> &,
                        IRMath::ivec3,
                        std::vector<IRVoxelEditor::RecipeCell> &cells,
                        std::string &) {
        cells.push_back(IRVoxelEditor::RecipeCell{IRMath::ivec3(1)});
        return true;
    };
    for (int i = 0; i <= componentRows; ++i) {
        view.components_.push_back(
            ComponentRecord{
                "M" + std::to_string(i),
                ComponentSource::MODULE,
                {IRVoxelEditor::ComponentField{"count", IRScript::LuaFieldType::INT32, 1}}
            }
        );
    }
    for (int i = componentRows + 1; i <= componentRows + 2; ++i)
        view.components_.push_back(
            ComponentRecord{"E" + std::to_string(i), ComponentSource::ENGINE}
        );
    const std::string lastModule = "M" + std::to_string(componentRows);
    const std::string firstEngine = "E" + std::to_string(componentRows + 1);
    const int componentMaxTop = static_cast<int>(view.components_.size()) - componentRows;

    Builder builder("list_scroll", IRMath::ivec3(16), IRMath::vec3(-8.0f), &view);
    builder.segment("recipe_below");
    builder.applyRecipe("r" + std::to_string(recipeRows), {});
    builder.segment("recipe_again");
    builder.applyRecipe("r" + std::to_string(recipeRows), {});
    builder.segment("recipe_last");
    builder.applyRecipe("r" + std::to_string(recipeRows + 1), {});
    builder.segment("recipe_first");
    builder.applyRecipe("r0", {});
    builder.segment("module_below");
    builder.attachComponent(lastModule);
    builder.segment("module_field");
    builder.setComponentField(lastModule, "count", "3");
    builder.segment("engine_below");
    builder.setComponentOverrides(firstEngine, "{}");
    builder.segment("component_first");
    builder.attachComponent("M0");
    builder.segment("raw_wheel_past_end");
    builder.wheelGui(componentListRowCenterGuiTrixel(0), componentMaxTop + 5);
    builder.segment("component_at_clamped_top");
    builder.attachComponent("M" + std::to_string(componentMaxTop));
    builder.segment("component_first_from_clamp");
    builder.attachComponent("M0");
    const IRVoxelEditor::Session::Recipe recipe = builder.finish();
    ASSERT_TRUE(recipe.ok()) << recipe.errors_.front();

    struct Expected {
        const char *segment_;
        int wheelRows_;
        IRMath::vec2 click_;
    };
    const IRMath::vec2 recipeBottom = recipeListRowCenterGuiTrixel(recipeRows - 1);
    const IRMath::vec2 componentBottom = componentListRowCenterGuiTrixel(componentRows - 1);
    for (const Expected &expected : std::vector<Expected>{
             {"recipe_below", 1, recipeBottom},
             {"recipe_again", 0, recipeBottom},
             {"recipe_last", 1, recipeBottom},
             {"recipe_first", -2, recipeListRowCenterGuiTrixel(0)},
             {"module_below", 1, componentBottom},
             {"module_field", 0, componentBottom},
             {"engine_below", 1, componentBottom},
             {"component_first", -2, componentListRowCenterGuiTrixel(0)},
             {"component_at_clamped_top", 0, componentListRowCenterGuiTrixel(0)},
             {"component_first_from_clamp", -componentMaxTop, componentListRowCenterGuiTrixel(0)},
         }) {
        const auto segment = std::find_if(
            recipe.segments_.begin(),
            recipe.segments_.end(),
            [&expected](const auto &candidate) {
                return candidate.label_ == std::string("list_scroll_") + expected.segment_;
            }
        );
        ASSERT_NE(segment, recipe.segments_.end()) << expected.segment_;
        const ListGesture gesture = listGesture(*segment);
        EXPECT_EQ(gesture.wheelRows_, expected.wheelRows_) << expected.segment_;
        EXPECT_EQ(gesture.firstClick_, expected.click_) << expected.segment_;
    }
}

TEST_F(LuaEditorSessionBindingsTest, FourTrixelsPerUnitIsTheExactIntegerBoundary) {
    const float trackWidth = static_cast<float>(IRVoxelEditor::kRecipeSliderSize.x);
    for (int target = 0; target <= 28; ++target) {
        for (int step = -31; step <= 31; ++step) {
            const float cursor =
                static_cast<float>(target) * 4.0f + static_cast<float>(step) / 16.0f;
            const float held = cursor / trackWidth * 28.0f;
            EXPECT_EQ(IRMath::roundHalfUp(held), target);
        }
    }
    bool missed = false;
    for (int target = 0; target <= 29; ++target) {
        for (int step = -31; step <= 31; ++step) {
            const float aim = static_cast<float>(target) / 29.0f * trackWidth;
            const float held = (aim + static_cast<float>(step) / 16.0f) / trackWidth * 29.0f;
            missed = missed || IRMath::roundHalfUp(held) != target;
        }
    }
    EXPECT_TRUE(missed);
}

} // namespace

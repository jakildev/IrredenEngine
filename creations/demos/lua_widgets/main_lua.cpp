// lua_widgets — proves the widget→Lua binding surface end to
// end. A panel + labels + two buttons are built ENTIRELY from `main.lua` via
// `IRGui.makePanel/makeLabel/makeButton`; one button carries a Lua `onClick`
// (dispatched by the new WIDGET_LUA_DISPATCH system), the other is polled with
// `IRGui.wasClicked` from a Lua system, and a hover label is driven by the
// engine's `IRInput.onEntityHovered` / `onEntityUnhovered` handlers (dispatched
// by ENTITY_HOVER_DETECT) — no per-creation C++ widget or input binding.
//
// The C++ side only: composes the standard render + INPUT/UPDATE pipelines
// (WIDGET_LUA_DISPATCH inserted right after WIDGET_INPUT so `fireAction_` is
// fresh), registers WIDGET_LUA_DISPATCH as a prefab system so the binding can
// resolve it, binds a tiny `IRTest` instrumentation table (test-only — NOT a
// widget binding), runs `main.lua`, and — under `--auto-screenshot` — drives a
// scripted click via the GUI-test harness so the proof is headless and
// machine-checkable.
//
// Headless proof (grep `GUI-ASSERT ... result=`, the gui-verify contract):
//   * shot 0 moves the cursor over the onClick button → LUA_HOVER_LABEL PASS
//     (the hover label was empty before the move and names the button after).
//   * shot 1 clicks the onClick button → CLICK_FIRES(button) PASS (click
//     reached the widget) + LUA_ONCLICK PASS (the Lua onClick handler ran).
//   * shot 2 clicks the poll button → POLL_WASCLICKED PASS (a Lua system
//     polling IRGui.wasClicked observed the click).
//   * shot 3 (no input) → the portrait `main.lua` built with
//     `IRRender.createViewport` draws its first subject: VIEWPORT_SUBJECT +
//     VIEWPORT_DRAWN_ID name that entity.
//   * shot 4 clicks the retarget button, whose Lua onClick moves, resizes,
//     re-zooms and retargets the portrait → VIEWPORT_RECT / VIEWPORT_ZOOM hold
//     the new values and VIEWPORT_SUBJECT / VIEWPORT_DRAWN_ID name the second
//     entity.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/ir_video.hpp>

// Systems composed into the pipelines (visible so createSystem<N> can
// instantiate at the call sites).
#include <irreden/input/systems/system_entity_hover_detect.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/input/systems/system_hitbox_mouse_test_gui.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/render/camera_controls.hpp>
#include <irreden/render/systems/system_sync_viewport_subjects.hpp>
#include <irreden/render/systems/system_viewport_to_framebuffer.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_text_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_widget_input.hpp>
#include <irreden/render/systems/system_widget_lua_dispatch.hpp>
#include <irreden/render/systems/system_widget_render_panel.hpp>
#include <irreden/render/systems/system_widget_render_label.hpp>
#include <irreden/render/systems/system_widget_render_button.hpp>

#include <irreden/render/gui_test_assertions.hpp>
#include <irreden/render/viewport.hpp>
#include <irreden/render/widgets.hpp>

#include <irreden/ir_profile.hpp>

#include <list>
#include <string>

namespace IRLuaWidgets {

// The single WIDGET_LUA_DISPATCH instance, registered as a prefab system in
// the Lua-binding callback (so the binding resolves the SAME instance the
// INPUT pipeline ticks) and inserted into the pipeline in initSystems().
IRSystem::SystemId g_dispatchId = IRSystem::kNullSystemId;

// Widget ids, published from main.lua via IRTest.setButtons once built.
IREntity::EntityId g_onClickButton = IREntity::kNullEntity;
IREntity::EntityId g_pollButton = IREntity::kNullEntity;
IREntity::EntityId g_hoverLabel = IREntity::kNullEntity;

// Test-only signals, flipped by the IRTest instrumentation hooks main.lua
// calls. Latched across a shot window (the onClick fires the frame fireAction_
// pulses, which is gone by the post-settle capture frame).
bool g_luaOnClickFired = false;
bool g_pollWasClickedSeen = false;
// The hover label's text on the hover shot's first frame, before its MOVE.
std::string g_hoverLabelBefore;

// The portrait viewport and its two candidate subjects, published from
// main.lua via IRTest.setPortrait, plus the rectangle and zoom its retarget
// handler reports applying.
IREntity::EntityId g_portrait = IREntity::kNullEntity;
IREntity::EntityId g_portraitFirstSubject = IREntity::kNullEntity;
IREntity::EntityId g_portraitSecondSubject = IREntity::kNullEntity;
IRMath::ivec2 g_portraitRetargetOrigin{0};
IRMath::ivec2 g_portraitRetargetSize{0};
float g_portraitRetargetZoom = 0.0f;

namespace {

// Click coords are in screen px; the GUI canvas is
// 640×720 trixels at the 1280×720 / gui_scale=1 config, so a gui-trixel maps
// to ~2 px in x and ~1 px in y (see layout.hpp mousePositionInGuiTrixels).
// Buttons are sized generously so the click lands well inside the hitbox.
constexpr IRVideo::GuiInputEvent kHoverEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(400, 190)},
};

constexpr IRVideo::GuiInputEvent kOnClickEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(400, 190)},
    {1,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(400, 190),
     IRMath::vec2(0.0f),
     IRInput::KeyMouseButtons::kMouseButtonLeft},
    {2,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(400, 190),
     IRMath::vec2(0.0f),
     IRInput::KeyMouseButtons::kMouseButtonLeft},
};

constexpr IRVideo::GuiInputEvent kPollEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(880, 190)},
    {1,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(880, 190),
     IRMath::vec2(0.0f),
     IRInput::KeyMouseButtons::kMouseButtonLeft},
    {2,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(880, 190),
     IRMath::vec2(0.0f),
     IRInput::KeyMouseButtons::kMouseButtonLeft},
};

// Must match the text main.lua's onEntityHovered handler writes for the
// onClick button.
constexpr const char *kExpectedHoverText = "HOVERING CLICK ME";

constexpr IRVideo::GuiInputEvent kRetargetEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(760, 550)},
    {1,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(760, 550),
     IRMath::vec2(0.0f),
     IRInput::KeyMouseButtons::kMouseButtonLeft},
    {2,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(760, 550),
     IRMath::vec2(0.0f),
     IRInput::KeyMouseButtons::kMouseButtonLeft},
};

enum GuiTestShotIndex { kHoverShot, kOnClickShot, kPollShot, kPortraitShot, kPortraitRetargetShot };

constexpr IRVideo::GuiTestShot kGuiTestShots[] = {
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_hover"},
     kHoverEvents,
     static_cast<int>(sizeof(kHoverEvents) / sizeof(kHoverEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_onclick"},
     kOnClickEvents,
     static_cast<int>(sizeof(kOnClickEvents) / sizeof(kOnClickEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_poll"},
     kPollEvents,
     static_cast<int>(sizeof(kPollEvents) / sizeof(kPollEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_portrait"}, nullptr, 0},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_portrait_retarget"},
     kRetargetEvents,
     static_cast<int>(sizeof(kRetargetEvents) / sizeof(kRetargetEvents[0]))},
};
constexpr int kNumGuiTestShots = static_cast<int>(sizeof(kGuiTestShots) / sizeof(kGuiTestShots[0]));

// Latches fireAction_ pulses across each shot for the engine CLICK_FIRES
// assertion (caller-owned; never a function-local static — cpp-systems.md).
IRPrefab::GuiTest::LatchState g_clickLatch;
int g_lastAssertShot = -1;

int g_autoWarmupFrames = 0;

const std::string &hoverLabelText() {
    return IREntity::getComponent<IRComponents::C_WidgetLabel>(g_hoverLabel).text_;
}

void logGuiAssert(
    int shotIndex,
    const char *kind,
    IREntity::EntityId target,
    const char *name,
    bool pass,
    const std::string &actual
) {
    IR_LOG_INFO(
        "GUI-ASSERT shot={} label={} kind={} target={} name={} result={} actual={}",
        shotIndex,
        kGuiTestShots[shotIndex].render_.label_,
        kind,
        target,
        name,
        pass ? "PASS" : "FAIL",
        actual
    );
}

// The entity the portrait's canvas carries at its center texel — what the
// viewport actually rastered, read back from the GPU id texture.
IREntity::EntityId portraitDrawnEntity() {
    const IREntity::EntityId canvas = IRPrefab::Viewport::canvasOf(g_portrait);
    if (canvas == IREntity::kNullEntity) {
        return IREntity::kNullEntity;
    }
    const auto &textures = IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(canvas);
    return textures.readEntityIdAt(textures.size_ / IRMath::ivec2(2));
}

void assertPortraitSubject(int shotIndex, IREntity::EntityId expected) {
    const IREntity::EntityId subject = IRPrefab::Viewport::drawnSubject(g_portrait);
    logGuiAssert(
        shotIndex,
        "VIEWPORT_SUBJECT",
        expected,
        "portrait_subject",
        subject == expected && expected != IREntity::kNullEntity,
        std::to_string(subject)
    );
    const IREntity::EntityId drawn = portraitDrawnEntity();
    logGuiAssert(
        shotIndex,
        "VIEWPORT_DRAWN_ID",
        expected,
        "portrait_drawn_id",
        drawn == expected && expected != IREntity::kNullEntity,
        std::to_string(drawn)
    );
}

void assertPortraitRetargetCamera(int shotIndex) {
    const auto &camera = IREntity::getComponent<IRComponents::C_ViewportCamera>(g_portrait);
    const bool rectMatches = camera.rectOrigin_ == g_portraitRetargetOrigin &&
                             camera.rectSize_ == g_portraitRetargetSize &&
                             g_portraitRetargetSize != IRMath::ivec2(0);
    logGuiAssert(
        shotIndex,
        "VIEWPORT_RECT",
        g_portrait,
        "portrait_rect",
        rectMatches,
        std::to_string(camera.rectOrigin_.x) + "," + std::to_string(camera.rectOrigin_.y) + "," +
            std::to_string(camera.rectSize_.x) + "," + std::to_string(camera.rectSize_.y)
    );
    const float zoom = IREntity::getComponent<IRComponents::C_ZoomLevel>(g_portrait).zoom_.x;
    logGuiAssert(
        shotIndex,
        "VIEWPORT_ZOOM",
        g_portrait,
        "portrait_zoom",
        zoom == g_portraitRetargetZoom && g_portraitRetargetZoom != 0.0f,
        std::to_string(zoom)
    );
}

// Forwarder wired to IRVideo::GuiTestConfig::onAssertFrame_. Latches the
// engine click pulse every frame; on the capture frame, evaluates the engine
// CLICK_FIRES assertion AND emits the demo's own LUA_HOVER_LABEL / LUA_ONCLICK /
// POLL_WASCLICKED lines in the same `GUI-ASSERT ... result=PASS|FAIL` shape
// gui-verify greps. The harness calls this before the frame's scripted events,
// so a shot's first call sees the state before its first MOVE.
void onGuiAssertFrame(int shotIndex, bool isCaptureFrame) {
    if (shotIndex < 0 || shotIndex >= kNumGuiTestShots) {
        return;
    }
    // Reset per-shot latch + signals when a new shot begins.
    if (shotIndex != g_lastAssertShot) {
        g_lastAssertShot = shotIndex;
        g_clickLatch.firedWidgets_.clear();
        g_luaOnClickFired = false;
        g_pollWasClickedSeen = false;
        if (shotIndex == kHoverShot) {
            g_hoverLabelBefore = hoverLabelText();
        }
    }

    IRPrefab::GuiTest::detail::latchFires(g_clickLatch);
    if (!isCaptureFrame) {
        return;
    }

    if (shotIndex == kHoverShot) {
        const std::string &after = hoverLabelText();
        const bool hoverLabelSet = g_hoverLabelBefore.empty() && after == kExpectedHoverText;
        logGuiAssert(
            shotIndex,
            "LUA_HOVER_LABEL",
            g_hoverLabel,
            "hover_label",
            hoverLabelSet,
            "before='" + g_hoverLabelBefore + "' after='" + after + "'"
        );
    } else if (shotIndex == kPortraitShot) {
        assertPortraitSubject(shotIndex, g_portraitFirstSubject);
    } else if (shotIndex == kPortraitRetargetShot) {
        assertPortraitRetargetCamera(shotIndex);
        assertPortraitSubject(shotIndex, g_portraitSecondSubject);
    } else if (shotIndex == kOnClickShot) {
        const bool clickFired =
            IRPrefab::GuiTest::detail::firedThisShot(g_clickLatch, g_onClickButton);
        logGuiAssert(
            shotIndex,
            "CLICK_FIRES",
            g_onClickButton,
            "onclick_button",
            clickFired,
            clickFired ? "fired" : "no-fire"
        );
        logGuiAssert(
            shotIndex,
            "LUA_ONCLICK",
            g_onClickButton,
            "lua_onclick_ran",
            g_luaOnClickFired,
            g_luaOnClickFired ? "ran" : "did-not-run"
        );
    } else {
        logGuiAssert(
            shotIndex,
            "POLL_WASCLICKED",
            g_pollButton,
            "poll_wasclicked",
            g_pollWasClickedSeen,
            g_pollWasClickedSeen ? "polled-true" : "never"
        );
    }
    g_clickLatch.firedWidgets_.clear();
}

} // namespace

} // namespace IRLuaWidgets

void registerLuaBindings();
void initSystems();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: lua_widgets");
    registerLuaBindings();
    IREngine::init(argc, argv, "config.lua");
    IRLuaWidgets::g_autoWarmupFrames = IREngine::args().autoScreenshotWarmupFrames();
    initSystems();
    IREngine::runScript("main.lua"); // builds widgets + registers onClick / poll system
    IREngine::gameLoop();
    return 0;
}

void registerLuaBindings() {
    IREngine::registerLuaBindings([](IRScript::LuaScript &script) {
        // Wires IRGui.make*/wasClicked, IRRender.getGuiCanvasSize, IRSystem.*,
        // IRComponent.*, etc. — the whole Lua-driven authoring surface.
        script.bindLuaDrivenEcs();
        // IRInput.onEntityHovered / onEntityUnhovered for the hover label.
        script.bindLuaCommands();
        // IRRender.createViewport / setViewport* — the portrait main.lua builds.
        script.bindLuaViewport();

        // Register the dispatch system ONCE here so its single instance is in
        // the prefab-system-id map (the binding resolves THIS instance) and so
        // initSystems() can insert the same id into the INPUT pipeline.
        IRLuaWidgets::g_dispatchId = script.registerPrefabSystem<IRSystem::WIDGET_LUA_DISPATCH>();

        // Test-only instrumentation (NOT a widget binding): lets main.lua hand
        // the widget ids back to C++ for the GUI-test assertions and signal
        // that its onClick / poll callbacks actually ran.
        sol::state &lua = script.lua();
        lua["IRTest"] = lua.create_table();
        lua["IRTest"]["setButtons"] =
            [](lua_Integer onClickButton, lua_Integer pollButton, lua_Integer hoverLabel) {
                IRLuaWidgets::g_onClickButton = static_cast<IREntity::EntityId>(onClickButton);
                IRLuaWidgets::g_pollButton = static_cast<IREntity::EntityId>(pollButton);
                IRLuaWidgets::g_hoverLabel = static_cast<IREntity::EntityId>(hoverLabel);
            };
        lua["IRTest"]["onClickFired"] = []() { IRLuaWidgets::g_luaOnClickFired = true; };
        lua["IRTest"]["onPollFired"] = []() { IRLuaWidgets::g_pollWasClickedSeen = true; };
        // A world voxel cube for the portrait to view; the demo binds no
        // component pack, so this stands in for a creation's own spawn path.
        lua["IRTest"]["spawnCube"] =
            [](float x, float y, float z, int size, int r, int g, int b) -> lua_Integer {
            return static_cast<lua_Integer>(IREntity::createEntity(
                IRComponents::C_LocalTransform{IRMath::vec3(x, y, z)},
                IRComponents::C_VoxelSetNew{
                    IRMath::ivec3(size),
                    IRMath::Color{
                        static_cast<std::uint8_t>(r),
                        static_cast<std::uint8_t>(g),
                        static_cast<std::uint8_t>(b),
                        255
                    },
                    true
                }
            ));
        };
        lua["IRTest"]["setPortrait"] = [](lua_Integer portrait,
                                          lua_Integer firstSubject,
                                          lua_Integer secondSubject) {
            IRLuaWidgets::g_portrait = static_cast<IREntity::EntityId>(portrait);
            IRLuaWidgets::g_portraitFirstSubject = static_cast<IREntity::EntityId>(firstSubject);
            IRLuaWidgets::g_portraitSecondSubject = static_cast<IREntity::EntityId>(secondSubject);
        };
        lua["IRTest"]["onPortraitRetargeted"] =
            [](int x, int y, int width, int height, float zoom) {
                IRLuaWidgets::g_portraitRetargetOrigin = IRMath::ivec2(x, y);
                IRLuaWidgets::g_portraitRetargetSize = IRMath::ivec2(width, height);
                IRLuaWidgets::g_portraitRetargetZoom = zoom;
            };
    });
}

void initSystems() {
    // INPUT — hover test → Lua hover handlers → widget state machine → Lua
    // click dispatch. The dispatch id is the prefab instance registered in the
    // binding callback, placed immediately after WIDGET_INPUT (so fireAction_
    // is fresh).
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {
            IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>(),
            IRSystem::createSystem<IRSystem::HITBOX_MOUSE_TEST_GUI>(),
            IRSystem::createSystem<IRSystem::ENTITY_HOVER_DETECT>(),
            IRSystem::createSystem<IRSystem::WIDGET_INPUT>(),
            IRLuaWidgets::g_dispatchId,
        }
    );

    // UPDATE — main.lua appends its wasClicked-poll system here via
    // IRSystem.appendSystem(IRTime.UPDATE, ...), so the pipeline must exist.
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {
            IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
            IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
        }
    );

    std::list<IRSystem::SystemId> renderPipeline = IRPrefab::Camera::standardControlSystems();
    renderPipeline.insert(
        renderPipeline.end(),
        {
            IRSystem::createSystem<IRSystem::SYNC_VIEWPORT_SUBJECTS>(),
            IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
            IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_PANEL>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_LABEL>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_BUTTON>(),
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::VIEWPORT_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
        }
    );

    if (IRLuaWidgets::g_autoWarmupFrames > 0) {
        IRVideo::GuiTestConfig cfg{};
        cfg.warmupFrames_ = IRLuaWidgets::g_autoWarmupFrames;
        cfg.settleFrames_ = 3;
        cfg.shots_ = IRLuaWidgets::kGuiTestShots;
        cfg.numShots_ = IRLuaWidgets::kNumGuiTestShots;
        cfg.onAssertFrame_ = &IRLuaWidgets::onGuiAssertFrame;
        renderPipeline.push_back(IRVideo::createGuiTestSystem(cfg));
    }

    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}

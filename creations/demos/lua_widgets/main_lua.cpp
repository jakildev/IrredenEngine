// lua_widgets (engine #1975) — proves the widget→Lua binding surface end to
// end. A panel + label + two buttons are built ENTIRELY from `main.lua` via
// `IRGui.makePanel/makeLabel/makeButton`; one button carries a Lua `onClick`
// (dispatched by the new WIDGET_LUA_DISPATCH system), the other is polled with
// `IRGui.wasClicked` from a Lua system — no per-creation C++ widget binding.
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
//   * shot 0 clicks the onClick button → CLICK_FIRES(button) PASS (click
//     reached the widget) + LUA_ONCLICK PASS (the Lua onClick handler ran).
//   * shot 1 clicks the poll button → POLL_WASCLICKED PASS (a Lua system
//     polling IRGui.wasClicked observed the click).

#include <irreden/ir_engine.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/ir_video.hpp>

// Systems composed into the pipelines (visible so createSystem<N> can
// instantiate at the call sites).
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/input/systems/system_hitbox_mouse_test_gui.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/render/camera_controls.hpp>
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
#include <irreden/render/widgets.hpp>

#include <irreden/ir_profile.hpp>

#include <list>

namespace IRLuaWidgets {

// The single WIDGET_LUA_DISPATCH instance, registered as a prefab system in
// the Lua-binding callback (so the binding resolves the SAME instance the
// INPUT pipeline ticks) and inserted into the pipeline in initSystems().
IRSystem::SystemId g_dispatchId = IREntity::kNullEntity;

// Widget ids, published from main.lua via IRTest.setButtons once built.
IREntity::EntityId g_onClickButton = IREntity::kNullEntity;
IREntity::EntityId g_pollButton = IREntity::kNullEntity;

// Test-only signals, flipped by the IRTest instrumentation hooks main.lua
// calls. Latched across a shot window (the onClick fires the frame fireAction_
// pulses, which is gone by the post-settle capture frame).
bool g_luaOnClickFired = false;
bool g_pollWasClickedSeen = false;

namespace {

// Two assertion shots. Click coords are in screen px; the GUI canvas is
// 640×720 trixels at the 1280×720 / gui_scale=1 config, so a gui-trixel maps
// to ~2 px in x and ~1 px in y (see layout.hpp mousePositionInGuiTrixels).
// Buttons are sized generously so the click lands well inside the hitbox.
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

constexpr IRVideo::GuiTestShot kGuiTestShots[] = {
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_onclick"},
     kOnClickEvents,
     static_cast<int>(sizeof(kOnClickEvents) / sizeof(kOnClickEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_poll"},
     kPollEvents,
     static_cast<int>(sizeof(kPollEvents) / sizeof(kPollEvents[0]))},
};
constexpr int kNumGuiTestShots = static_cast<int>(sizeof(kGuiTestShots) / sizeof(kGuiTestShots[0]));

// Latches fireAction_ pulses across each shot for the engine CLICK_FIRES
// assertion (caller-owned; never a function-local static — cpp-systems.md).
IRPrefab::GuiTest::LatchState g_clickLatch;
int g_lastAssertShot = -1;

int g_autoWarmupFrames = 0;

// Forwarder wired to IRVideo::GuiTestConfig::onAssertFrame_. Latches the
// engine click pulse every frame; on the capture frame, evaluates the engine
// CLICK_FIRES assertion AND emits the demo's own LUA_ONCLICK / POLL_WASCLICKED
// lines in the same `GUI-ASSERT ... result=PASS|FAIL` shape gui-verify greps.
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
    }

    IRPrefab::GuiTest::detail::latchFires(g_clickLatch);
    if (!isCaptureFrame) {
        return;
    }

    const char *label = kGuiTestShots[shotIndex].render_.label_;
    if (shotIndex == 0) {
        const bool clickFired =
            IRPrefab::GuiTest::detail::firedThisShot(g_clickLatch, g_onClickButton);
        IR_LOG_INFO(
            "GUI-ASSERT shot={} label={} kind=CLICK_FIRES target={} name=onclick_button "
            "result={} actual={}",
            shotIndex,
            label,
            g_onClickButton,
            clickFired ? "PASS" : "FAIL",
            clickFired ? "fired" : "no-fire"
        );
        IR_LOG_INFO(
            "GUI-ASSERT shot={} label={} kind=LUA_ONCLICK target={} name=lua_onclick_ran "
            "result={} actual={}",
            shotIndex,
            label,
            g_onClickButton,
            g_luaOnClickFired ? "PASS" : "FAIL",
            g_luaOnClickFired ? "ran" : "did-not-run"
        );
    } else {
        IR_LOG_INFO(
            "GUI-ASSERT shot={} label={} kind=POLL_WASCLICKED target={} name=poll_wasclicked "
            "result={} actual={}",
            shotIndex,
            label,
            g_pollButton,
            g_pollWasClickedSeen ? "PASS" : "FAIL",
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
    IRVideo::parseAutoScreenshotArgv(argc, argv, &IRLuaWidgets::g_autoWarmupFrames);

    registerLuaBindings();
    IREngine::init(argv[0], "config.lua");
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

        // Register the dispatch system ONCE here so its single instance is in
        // the prefab-system-id map (the binding resolves THIS instance) and so
        // initSystems() can insert the same id into the INPUT pipeline.
        IRLuaWidgets::g_dispatchId = script.registerPrefabSystem<IRSystem::WIDGET_LUA_DISPATCH>();

        // Test-only instrumentation (NOT a widget binding): lets main.lua hand
        // the widget ids back to C++ for the GUI-test assertions and signal
        // that its onClick / poll callbacks actually ran.
        sol::state &lua = script.lua();
        lua["IRTest"] = lua.create_table();
        lua["IRTest"]["setButtons"] = [](lua_Integer onClickButton, lua_Integer pollButton) {
            IRLuaWidgets::g_onClickButton = static_cast<IREntity::EntityId>(onClickButton);
            IRLuaWidgets::g_pollButton = static_cast<IREntity::EntityId>(pollButton);
        };
        lua["IRTest"]["onClickFired"] = []() { IRLuaWidgets::g_luaOnClickFired = true; };
        lua["IRTest"]["onPollFired"] = []() { IRLuaWidgets::g_pollWasClickedSeen = true; };
    });
}

void initSystems() {
    // INPUT — hover test → widget state machine → Lua click dispatch. The
    // dispatch id is the prefab instance registered in the binding callback,
    // placed immediately after WIDGET_INPUT (so fireAction_ is fresh).
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {
            IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>(),
            IRSystem::createSystem<IRSystem::HITBOX_MOUSE_TEST_GUI>(),
            IRSystem::createSystem<IRSystem::WIDGET_INPUT>(),
            IRLuaWidgets::g_dispatchId,
        }
    );

    // UPDATE — main.lua appends its wasClicked-poll system here via
    // IRSystem.appendSystem(IRTime.UPDATE, ...), so the pipeline must exist.
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>()}
    );

    std::list<IRSystem::SystemId> renderPipeline = IRPrefab::Camera::standardControlSystems();
    renderPipeline.insert(
        renderPipeline.end(),
        {
            IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
            IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_PANEL>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_LABEL>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_BUTTON>(),
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
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

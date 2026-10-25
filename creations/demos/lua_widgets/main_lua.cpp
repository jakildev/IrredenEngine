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
//   * shot 5 moves onto the `IRGui.makeHitRegion` row, clicks it and moves
//     off → LUA_ROW_REGION PASS (the region's onHover, onClick and onUnhover
//     ran once each, in that order, with the region's id).
//   * shot 6 double-clicks the paired slider's track → PREDICATE PASS (the
//     first click's value mutation was restored and the number field received
//     keyboard focus on the following INPUT frame).
//   * shot 7 scrolls the wheel with the pointer off the scroll list →
//     LUA_SCROLL_OFF_PANEL PASS (the first visible row did not move).
//   * shot 8 scrolls the wheel with the pointer over the list →
//     LUA_SCROLL_ON_PANEL PASS (the first visible row advanced by one).
//   * shots 9-11 drag the undoable slider, then press Ctrl+Z and Ctrl+Y →
//     LUA_UNDO_SLIDER PASS (the drag, its undo and its redo).
//   * shot 12 opens the 20-item dropdown, scrolls the wheel three times over its
//     panel → DROPDOWN_WINDOW PASS (the item window moved exactly three rows),
//     DROPDOWN_HOVERED PASS (the hovered widget is the dropdown, not only its
//     header) and DROPDOWN_ZOOM_HELD PASS (the camera zoom did not change).
//   * shot 13 scrolls the wheel over empty canvas → WHEEL_ZOOMS_CAMERA PASS
//     (positive control: the zoom does change when no widget is hovered).
//   * shots 14-19 drive the widget hotkeys and their held-Alt hint badges.
//   * shot 20 opens the 40-item dropdown near the canvas bottom with Alt+P and
//     presses Down ten times → DROPDOWN_LONG_PLACED PASS (the strip opens
//     upward, whole, inside the canvas) and DROPDOWN_LONG_KEYBOARD PASS (the
//     keyboard row reached item 11 and the window scrolled to keep it shown).
//   * shot 21 scrolls the wheel once over that upward strip →
//     DROPDOWN_LONG_WHEEL PASS (the window moved exactly one row).
//   * shot 22 scrolls the wheel once over the 60-row `IRGui.makeList` list →
//     LUA_LIST_WHEEL PASS (the window moved exactly one row and
//     `IRGui.listSelected` still names row 40).
//   * shots 23-24 type LO into the long dropdown and pick its first match →
//     DROPDOWN_TYPEAHEAD_NARROWED / DROPDOWN_TYPEAHEAD_PICKED PASS.
//
// Hand-off fixture (opt-in; absent, the demo is unchanged):
// `--hand-off-fixture button` adds a HAND OFF button whose onClick calls
// `IREngine.handOff` to run this demo's GUI-test take as a child, and logs
// `LUA_WIDGETS HANDOFF before` / `resumed` with the render frame and engine
// tick (equal across the pair: the loop did not iterate while handed off).
// `auto` also fires it 30 ticks in and closes the window 30 ticks after it
// resumes, so `fleet-run IRLuaWidgetsDemo --hand-off-fixture auto` proves the
// round trip unattended. Outside ir-run the call logs one notice and returns
// ok=false, supported=false.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/common/undo.hpp>

// Systems composed into the pipelines (visible so createSystem<N> can
// instantiate at the call sites).
#include <irreden/input/systems/system_entity_hover_detect.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/input/systems/system_hitbox_mouse_test_gui.hpp>
#include <irreden/update/systems/system_lifetime.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/render/camera_controls.hpp>
#include <irreden/render/systems/system_sync_viewport_subjects.hpp>
#include <irreden/render/systems/system_viewport_to_framebuffer.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_text_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_gui_canvas_to_screen.hpp>
#include <irreden/render/systems/system_camera_scroll_zoom.hpp>
#include <irreden/render/systems/system_widget_input.hpp>
#include <irreden/render/systems/system_widget_apply_number_field.hpp>
#include <irreden/render/systems/system_widget_apply_slider.hpp>
#include <irreden/render/systems/system_widget_apply_dropdown.hpp>
#include <irreden/render/systems/system_widget_apply_list.hpp>
#include <irreden/render/systems/system_widget_render_dropdown.hpp>
#include <irreden/render/systems/system_widget_render_list.hpp>
#include <irreden/render/systems/system_widget_apply_checkbox.hpp>
#include <irreden/render/systems/system_widget_apply_radio.hpp>
#include <irreden/render/systems/system_widget_lua_dispatch.hpp>
#include <irreden/render/systems/system_widget_render_panel.hpp>
#include <irreden/render/systems/system_widget_render_label.hpp>
#include <irreden/render/systems/system_widget_render_button.hpp>
#include <irreden/render/systems/system_widget_render_number_field.hpp>
#include <irreden/render/systems/system_widget_render_slider.hpp>
#include <irreden/render/systems/system_widget_render_checkbox.hpp>
#include <irreden/render/systems/system_widget_render_dropdown.hpp>
#include <irreden/render/systems/system_widget_render_radio.hpp>
#include <irreden/render/systems/system_widget_render_hints.hpp>
#include <irreden/render/widget_hints.hpp>

#include <irreden/render/gui_test_assertions.hpp>
#include <irreden/render/viewport.hpp>
#include <irreden/render/widgets.hpp>

#include <irreden/ir_profile.hpp>

#include <array>
#include <cstddef>
#include <list>
#include <string>
#include <fmt/format.h>

namespace IRLuaWidgets {

// The single WIDGET_LUA_DISPATCH instance, registered as a prefab system in
// the Lua-binding callback (so the binding resolves the SAME instance the
// INPUT pipeline ticks) and inserted into the pipeline in initSystems().
IRSystem::SystemId g_dispatchId = IRSystem::kNullSystemId;

// Widget ids, published from main.lua via IRTest.setButtons once built.
IREntity::EntityId g_onClickButton = IREntity::kNullEntity;
IREntity::EntityId g_pollButton = IREntity::kNullEntity;
IREntity::EntityId g_hoverLabel = IREntity::kNullEntity;
IREntity::EntityId g_pairedSlider = IREntity::kNullEntity;
IREntity::EntityId g_pairedField = IREntity::kNullEntity;
float g_pairedSliderBefore = 0.0f;
float g_pairedFieldBefore = 0.0f;
bool g_pairedFieldStartedUnfocused = false;

// Test-only signals, flipped by the IRTest instrumentation hooks main.lua
// calls. Latched across a shot window (the onClick fires the frame fireAction_
// pulses, which is gone by the post-settle capture frame).
bool g_luaOnClickFired = false;
bool g_pollWasClickedSeen = false;
bool g_cursorPositionHookRan = false;
bool g_cursorWorldMatches = false;
bool g_cursorScreenMatches = false;
bool g_cursorPlaneMatches = false;
IRMath::vec3 g_luaCursorWorld{0.0f};
IRMath::vec3 g_cppCursorWorld{0.0f};
IRMath::vec2 g_luaCursorScreen{0.0f};
IRMath::vec2 g_cppCursorScreen{0.0f};
IRMath::vec2 g_cursorScreenBefore{0.0f};
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

// The row hit region main.lua built, and the handler calls it reported, one
// letter each (H = hover, C = click, U = unhover) in call order.
IREntity::EntityId g_rowRegion = IREntity::kNullEntity;
std::string g_rowEvents;

// The first visible label of main.lua's wheel-scrolled list, published via
// IRTest.setScrollFirstRow, and its text when the scroll shot began.
IREntity::EntityId g_scrollFirstRow = IREntity::kNullEntity;
std::string g_scrollFirstRowBefore;
bool g_rowEventIdsMatch = true;
IREntity::EntityId g_undoSlider = IREntity::kNullEntity;
float g_undoSliderStart = 0.0f;
float g_undoSliderFinal = 0.0f;

// The 20-item dropdown main.lua built, published via IRTest.setDropdown, and
// the world camera zoom when the shot being evaluated began.
IREntity::EntityId g_dropdown = IREntity::kNullEntity;
float g_zoomBefore = 0.0f;

// The 40-item dropdown and 60-row list main.lua built, the list row it
// selected (1-based), and each one's first visible item when its wheel shot
// began.
IREntity::EntityId g_longDropdown = IREntity::kNullEntity;
IREntity::EntityId g_longList = IREntity::kNullEntity;
int g_longListSelected = 0;
int g_longDropdownTopBefore = -1;
int g_longListTopBefore = -1;

void recordRowEvent(char event, lua_Integer id) {
    g_rowEvents.push_back(event);
    g_rowEventIdsMatch = g_rowEventIdsMatch && static_cast<IREntity::EntityId>(id) == g_rowRegion;
}

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
     IRInput::kMouseButtonLeft},
    {2,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(400, 190),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
};

constexpr IRVideo::GuiInputEvent kPollEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(880, 190)},
    {1,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(880, 190),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
    {2,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(880, 190),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
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
     IRInput::kMouseButtonLeft},
    {2,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(760, 550),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
};

// The row region spans GUI x 260-590, y 410-440: screen x 520-1180, y 410-440.
constexpr IRVideo::GuiInputEvent kRowEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(1000, 425)},
    {1,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(1000, 425),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
    {2,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(1000, 425),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
    {3, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(1000, 475)},
};

// The scroll list spans GUI x 260-590, y 335-405: screen x 520-1180, y 335-405.
constexpr IRVideo::GuiInputEvent kScrollOffPanelEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(1000, 475)},
    {1, IRVideo::GuiInputEvent::Type::SCROLL, IRMath::ivec2(1000, 475), IRMath::vec2(0.0f, -1.0f)},
};

constexpr IRVideo::GuiInputEvent kScrollOnPanelEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(1000, 370)},
    {2, IRVideo::GuiInputEvent::Type::SCROLL, IRMath::ivec2(1000, 370), IRMath::vec2(0.0f, -1.0f)},
};

// Slider spans GUI x 340-480, y 296-330. Its initial thumb is near x 406;
// GUI x 470 (screen x 940) is unambiguously track body.
constexpr IRVideo::GuiInputEvent kPairedSliderEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(940, 320)},
    {1,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(940, 320),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
    {2,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(940, 320),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
    {3,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(940, 320),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
    {4,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(940, 320),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
};

// Must match the text main.lua writes for rows 1 and 2.
constexpr const char *kScrollFirstRowText = "SCROLL ROW 01";
constexpr const char *kScrollAdvancedRowText = "SCROLL ROW 02";

constexpr IRVideo::GuiInputEvent kSliderDragEvents[] = {
    // Begin on the initial thumb so this fixture records one drag gesture.
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(276, 320)},
    {1,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(276, 320),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
    {2, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(380, 320)},
    {3, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(560, 320)},
    {4,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(560, 320),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
};

constexpr IRVideo::GuiInputEvent kSliderUndoEvents[] = {
    {0,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonLeftControl},
    {2,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonZ},
    {3,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonZ},
    {4,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonLeftControl},
};

constexpr IRVideo::GuiInputEvent kSliderRedoEvents[] = {
    {0,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonLeftControl},
    {2,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonY},
    {3,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonY},
    {4,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonLeftControl},
};

// The dropdown header spans GUI x 510-630, y 450-470; its 8-row panel spans
// y 470-630 (screen x 1020-1260). The click opens it, the move puts the cursor
// over a panel row, and three wheel steps toward the end follow on their own
// frames: the panel's item window must move exactly three rows.
constexpr int kDropdownScrollSteps = 3;
constexpr IRVideo::GuiInputEvent kDropdownScrollEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(1140, 460)},
    {1,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(1140, 460),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
    {2,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(1140, 460),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
    {4, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(1140, 550)},
    {6, IRVideo::GuiInputEvent::Type::SCROLL, IRMath::ivec2(1140, 550), IRMath::vec2(0.0f, -1.0f)},
    {7, IRVideo::GuiInputEvent::Type::SCROLL, IRMath::ivec2(1140, 550), IRMath::vec2(0.0f, -1.0f)},
    {8, IRVideo::GuiInputEvent::Type::SCROLL, IRMath::ivec2(1140, 550), IRMath::vec2(0.0f, -1.0f)},
};

// Alt+P opens the 40-item dropdown (header GUI x 20-200, y 690-710) and
// focuses it; ten Down presses on their own frames move the keyboard row from
// item 1 to item 11. The pointer waits on empty canvas, off every row.
constexpr int kLongDropdownDownPresses = 10;
constexpr std::array<IRVideo::GuiInputEvent, 5 + 2 * kLongDropdownDownPresses>
longDropdownKeyboardEvents() {
    using Type = IRVideo::GuiInputEvent::Type;
    std::array<IRVideo::GuiInputEvent, 5 + 2 * kLongDropdownDownPresses> events{{
        {0, Type::MOVE, IRMath::ivec2(800, 710)},
        {1, Type::PRESS, IRMath::ivec2(0), IRMath::vec2(0.0f), IRInput::kKeyButtonLeftAlt},
        {2, Type::PRESS, IRMath::ivec2(0), IRMath::vec2(0.0f), IRInput::kKeyButtonP},
        {3, Type::RELEASE, IRMath::ivec2(0), IRMath::vec2(0.0f), IRInput::kKeyButtonP},
        {4, Type::RELEASE, IRMath::ivec2(0), IRMath::vec2(0.0f), IRInput::kKeyButtonLeftAlt},
    }};
    for (int press = 0; press < kLongDropdownDownPresses; ++press) {
        const auto slot = static_cast<std::size_t>(5 + 2 * press);
        events[slot] = {
            6 + 2 * press,
            Type::PRESS,
            IRMath::ivec2(0),
            IRMath::vec2(0.0f),
            IRInput::kKeyButtonDown
        };
        events[slot + 1] = {
            7 + 2 * press,
            Type::RELEASE,
            IRMath::ivec2(0),
            IRMath::vec2(0.0f),
            IRInput::kKeyButtonDown
        };
    }
    return events;
}
constexpr auto kLongDropdownKeyboardEvents = longDropdownKeyboardEvents();

// The upward strip spans GUI y 530-690 over x 20-200; GUI (110, 600) is on one
// of its rows. One wheel step toward the end.
constexpr IRVideo::GuiInputEvent kLongDropdownWheelEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(220, 600)},
    {2, IRVideo::GuiInputEvent::Type::SCROLL, IRMath::ivec2(220, 600), IRMath::vec2(0.0f, -1.0f)},
};

// The list spans GUI x 510-630, y 642-714.
constexpr IRVideo::GuiInputEvent kLongListWheelEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(1140, 678)},
    {2, IRVideo::GuiInputEvent::Type::SCROLL, IRMath::ivec2(1140, 678), IRMath::vec2(0.0f, -1.0f)},
};

constexpr IRVideo::GuiInputEvent kDropdownTypeaheadEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::PRESS, {}, {}, IRInput::kKeyButtonEscape},
    {1, IRVideo::GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kKeyButtonEscape},
    {2, IRVideo::GuiInputEvent::Type::PRESS, {}, {}, IRInput::kKeyButtonLeftAlt},
    {3, IRVideo::GuiInputEvent::Type::PRESS, {}, {}, IRInput::kKeyButtonP},
    {4, IRVideo::GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kKeyButtonP},
    {5, IRVideo::GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kKeyButtonLeftAlt},
    {6, IRVideo::GuiInputEvent::Type::PRESS, {}, {}, IRInput::kKeyButtonL},
    {7, IRVideo::GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kKeyButtonL},
    {8, IRVideo::GuiInputEvent::Type::PRESS, {}, {}, IRInput::kKeyButtonO},
    {9, IRVideo::GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kKeyButtonO},
};

constexpr IRVideo::GuiInputEvent kDropdownTypeaheadPickEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::PRESS, {}, {}, IRInput::kKeyButtonEnter},
    {1, IRVideo::GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kKeyButtonEnter},
};

// Empty canvas: no widget sits at GUI (400, 710).
constexpr IRVideo::GuiInputEvent kWheelZoomControlEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(800, 710)},
    {2, IRVideo::GuiInputEvent::Type::SCROLL, IRMath::ivec2(800, 710), IRMath::vec2(0.0f, 1.0f)},
};

constexpr IRVideo::GuiInputEvent kHotkeyHintsHeldEvents[] = {
    {0,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonLeftAlt},
};

constexpr IRVideo::GuiInputEvent kHotkeyHintsReleasedEvents[] = {
    {0,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonLeftAlt},
};

constexpr IRVideo::GuiInputEvent kHotkeyCheckboxEvents[] = {
    {0,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonLeftAlt},
    {2,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonC},
    {3,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonC},
    {4,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonLeftAlt},
};

constexpr IRVideo::GuiInputEvent kHotkeySliderEvents[] = {
    {0,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonLeftAlt},
    {2,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonS},
    {3,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonS},
    {4,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonLeftAlt},
    {5,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonUp},
    {6,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonUp},
};

constexpr IRVideo::GuiInputEvent kHotkeySliderResetEvents[] = {
    {0,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonDelete},
    {1,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonDelete},
};

constexpr IRVideo::GuiInputEvent kHotkeyPointerEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(100, 444)},
    {1,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(100, 444),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
    {2,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(100, 444),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
};

enum GuiTestShotIndex {
    kHoverShot,
    kOnClickShot,
    kPollShot,
    kPortraitShot,
    kPortraitRetargetShot,
    kRowShot,
    kPairedSliderShot,
    kScrollOffPanelShot,
    kScrollOnPanelShot,
    kSliderDragShot,
    kSliderUndoShot,
    kSliderRedoShot,
    kDropdownScrollShot,
    kWheelZoomControlShot,
    kHotkeyHintsHeldShot,
    kHotkeyHintsReleasedShot,
    kHotkeyCheckboxShot,
    kHotkeySliderShot,
    kHotkeySliderResetShot,
    kHotkeyPointerShot,
    kLongDropdownKeyboardShot,
    kLongDropdownWheelShot,
    kLongListWheelShot,
    kDropdownTypeaheadShot,
    kDropdownTypeaheadPickShot
};

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
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_row_region"},
     kRowEvents,
     static_cast<int>(sizeof(kRowEvents) / sizeof(kRowEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_paired_slider"},
     kPairedSliderEvents,
     static_cast<int>(sizeof(kPairedSliderEvents) / sizeof(kPairedSliderEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_scroll_off_panel"},
     kScrollOffPanelEvents,
     static_cast<int>(sizeof(kScrollOffPanelEvents) / sizeof(kScrollOffPanelEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_scroll_on_panel"},
     kScrollOnPanelEvents,
     static_cast<int>(sizeof(kScrollOnPanelEvents) / sizeof(kScrollOnPanelEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_slider_drag"},
     kSliderDragEvents,
     static_cast<int>(sizeof(kSliderDragEvents) / sizeof(kSliderDragEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_slider_undo"},
     kSliderUndoEvents,
     static_cast<int>(sizeof(kSliderUndoEvents) / sizeof(kSliderUndoEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_slider_redo"},
     kSliderRedoEvents,
     static_cast<int>(sizeof(kSliderRedoEvents) / sizeof(kSliderRedoEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_dropdown_scroll"},
     kDropdownScrollEvents,
     static_cast<int>(sizeof(kDropdownScrollEvents) / sizeof(kDropdownScrollEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_wheel_zoom_control"},
     kWheelZoomControlEvents,
     static_cast<int>(sizeof(kWheelZoomControlEvents) / sizeof(kWheelZoomControlEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_hotkey_hints"},
     kHotkeyHintsHeldEvents,
     static_cast<int>(sizeof(kHotkeyHintsHeldEvents) / sizeof(kHotkeyHintsHeldEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_hotkey_hints_released"},
     kHotkeyHintsReleasedEvents,
     static_cast<int>(sizeof(kHotkeyHintsReleasedEvents) / sizeof(kHotkeyHintsReleasedEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_hotkey_checkbox"},
     kHotkeyCheckboxEvents,
     static_cast<int>(sizeof(kHotkeyCheckboxEvents) / sizeof(kHotkeyCheckboxEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_hotkey_slider"},
     kHotkeySliderEvents,
     static_cast<int>(sizeof(kHotkeySliderEvents) / sizeof(kHotkeySliderEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_hotkey_slider_reset"},
     kHotkeySliderResetEvents,
     static_cast<int>(sizeof(kHotkeySliderResetEvents) / sizeof(kHotkeySliderResetEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_hotkey_pointer"},
     kHotkeyPointerEvents,
     static_cast<int>(sizeof(kHotkeyPointerEvents) / sizeof(kHotkeyPointerEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_dropdown_long_keyboard"},
     kLongDropdownKeyboardEvents.data(),
     static_cast<int>(kLongDropdownKeyboardEvents.size())},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_dropdown_long_wheel"},
     kLongDropdownWheelEvents,
     static_cast<int>(sizeof(kLongDropdownWheelEvents) / sizeof(kLongDropdownWheelEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_list_wheel"},
     kLongListWheelEvents,
     static_cast<int>(sizeof(kLongListWheelEvents) / sizeof(kLongListWheelEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_dropdown_typeahead"},
     kDropdownTypeaheadEvents,
     static_cast<int>(sizeof(kDropdownTypeaheadEvents) / sizeof(kDropdownTypeaheadEvents[0]))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "lua_widgets_dropdown_typeahead_pick"},
     kDropdownTypeaheadPickEvents,
     static_cast<int>(
         sizeof(kDropdownTypeaheadPickEvents) / sizeof(kDropdownTypeaheadPickEvents[0])
     )},
};
constexpr int kNumGuiTestShots = static_cast<int>(sizeof(kGuiTestShots) / sizeof(kGuiTestShots[0]));

// Latches fireAction_ pulses across each shot for the engine CLICK_FIRES
// assertion (caller-owned; never a function-local static — cpp-systems.md).
IRPrefab::GuiTest::LatchState g_clickLatch;
IRPrefab::GuiTest::LatchState g_pairedLatch;
int g_lastAssertShot = -1;

int g_autoWarmupFrames = 0;

const std::string &hoverLabelText() {
    return IREntity::getComponent<IRComponents::C_WidgetLabel>(g_hoverLabel).text_;
}

const std::string &scrollFirstRowText() {
    return IREntity::getComponent<IRComponents::C_WidgetLabel>(g_scrollFirstRow).text_;
}

IREntity::EntityId hotkeyWidget(IRInput::KeyMouseButtons key) {
    IREntity::EntityId result = IREntity::kNullEntity;
    IREntity::forEachComponent<IRComponents::C_WidgetHotkey>(
        [&result, key](IREntity::EntityId &id, IRComponents::C_WidgetHotkey &hotkey) {
            if (hotkey.key_ == key) {
                result = id;
            }
        }
    );
    return result;
}

bool pairedSliderPredicate(const void *, std::string &actual) {
    const float sliderValue = IRPrefab::Widget::sliderValue(g_pairedSlider);
    const float fieldValue = IRPrefab::Widget::numberFieldValue(g_pairedField);
    const auto &sliderState = IREntity::getComponent<IRComponents::C_WidgetState>(g_pairedSlider);
    const bool focused =
        IREntity::getComponent<IRComponents::C_WidgetState>(g_pairedField).focused_;
    actual = fmt::format(
        "startedUnfocused={} slider={}->{} first={} press={} age={} region={} field={}->{} "
        "focused={}",
        g_pairedFieldStartedUnfocused,
        g_pairedSliderBefore,
        sliderValue,
        sliderState.sliderFirstClickValue_,
        sliderState.sliderPressValue_,
        sliderState.sliderClickAge_,
        static_cast<int>(sliderState.sliderFirstClickRegion_),
        g_pairedFieldBefore,
        fieldValue,
        focused
    );
    return g_pairedFieldStartedUnfocused && focused && sliderValue == g_pairedSliderBefore &&
           fieldValue == g_pairedFieldBefore;
}

IRPrefab::GuiTest::Assertion g_pairedAssertion =
    IRPrefab::GuiTest::predicate(&pairedSliderPredicate, nullptr, "paired_track_focus");

float cameraZoom() {
    return IREntity::getComponent<IRComponents::C_ZoomLevel>(IREntity::getEntity("camera")).zoom_.x;
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

int dropdownTopIndex() {
    return IREntity::getComponent<IRComponents::C_WidgetDropdown>(g_dropdown)
        .topIndex(IRPrefab::Widget::dropdownStripLayout(g_dropdown));
}

int longDropdownTopIndex() {
    if (g_longDropdown == IREntity::kNullEntity) {
        return -1;
    }
    return IREntity::getComponent<IRComponents::C_WidgetDropdown>(g_longDropdown)
        .topIndex(IRPrefab::Widget::dropdownStripLayout(g_longDropdown));
}

int longListVisibleRows() {
    const auto &list = IREntity::getComponent<IRComponents::C_WidgetList>(g_longList);
    return list.visibleRows(IREntity::getComponent<IRComponents::C_Widget>(g_longList).size_.y);
}

int longListTopIndex() {
    if (g_longList == IREntity::kNullEntity) {
        return -1;
    }
    return IREntity::getComponent<IRComponents::C_WidgetList>(g_longList)
        .topIndex(IREntity::getComponent<IRComponents::C_Widget>(g_longList).size_.y);
}

// The open strip sits whole above the header and inside the GUI canvas.
void assertLongDropdownPlaced(int shotIndex) {
    const auto &dropdown = IREntity::getComponent<IRComponents::C_WidgetDropdown>(g_longDropdown);
    const auto &widget = IREntity::getComponent<IRComponents::C_Widget>(g_longDropdown);
    const IRMath::ivec2 pos =
        IREntity::getComponent<IRComponents::C_GuiPosition>(g_longDropdown).pos_;
    const IRComponents::C_WidgetDropdown::StripLayout layout =
        IRPrefab::Widget::dropdownStripLayout(g_longDropdown);
    const IRPrefab::GuiRect strip =
        IRPrefab::WidgetText::dropdownStripRect(widget, dropdown, pos, layout);
    const int canvasHeight = IRPrefab::Widget::guiCanvasSize().y;
    logGuiAssert(
        shotIndex,
        "DROPDOWN_LONG_PLACED",
        g_longDropdown,
        "strip_inside_canvas",
        dropdown.isOpen_ && layout.rows_ == IRComponents::kDropdownMaxVisibleRows &&
            strip.pos_.y + strip.size_.y == pos.y && strip.pos_.y >= 0 &&
            strip.pos_.y + strip.size_.y <= canvasHeight,
        fmt::format(
            "open={} rows={} strip=({},{} {}x{}) header.y={} canvas.h={}",
            dropdown.isOpen_,
            layout.rows_,
            strip.pos_.x,
            strip.pos_.y,
            strip.size_.x,
            strip.size_.y,
            pos.y,
            canvasHeight
        )
    );
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
        if (shotIndex == kRowShot) {
            g_rowEvents.clear();
            g_rowEventIdsMatch = true;
        }
        if (shotIndex == kScrollOffPanelShot || shotIndex == kScrollOnPanelShot) {
            g_scrollFirstRowBefore = scrollFirstRowText();
        }
        if (shotIndex == kPairedSliderShot) {
            g_pairedSliderBefore = IRPrefab::Widget::sliderValue(g_pairedSlider);
            g_pairedFieldBefore = IRPrefab::Widget::numberFieldValue(g_pairedField);
            g_pairedFieldStartedUnfocused =
                !IREntity::getComponent<IRComponents::C_WidgetState>(g_pairedField).focused_;
        }
        if (shotIndex == kDropdownScrollShot || shotIndex == kWheelZoomControlShot) {
            g_zoomBefore = cameraZoom();
        }
        if (shotIndex == kLongDropdownWheelShot) {
            g_longDropdownTopBefore = longDropdownTopIndex();
        }
        if (shotIndex == kLongListWheelShot) {
            g_longListTopBefore = longListTopIndex();
        }
        if (shotIndex == kHoverShot) {
            g_hoverLabelBefore = hoverLabelText();
            g_cursorPositionHookRan = false;
            g_cursorWorldMatches = false;
            g_cursorScreenMatches = false;
            g_cursorPlaneMatches = false;
            g_cursorScreenBefore = IRRender::mousePosition2DIsoScreenRender();
        }
    }

    IRPrefab::GuiTest::detail::latchFires(g_clickLatch);
    if (shotIndex == kPairedSliderShot) {
        IRPrefab::GuiTest::onFrame(
            g_pairedLatch,
            shotIndex,
            isCaptureFrame,
            kGuiTestShots[shotIndex].render_.label_,
            &g_pairedAssertion,
            1
        );
        return;
    }
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
        const bool cursorMoved = g_luaCursorScreen.x != g_cursorScreenBefore.x ||
                                 g_luaCursorScreen.y != g_cursorScreenBefore.y;
        const bool cursorPositionPass = g_cursorPositionHookRan && g_cursorWorldMatches &&
                                        g_cursorScreenMatches && g_cursorPlaneMatches &&
                                        cursorMoved;
        logGuiAssert(
            shotIndex,
            "LUA_CURSOR_POS",
            g_onClickButton,
            "cursor_position",
            cursorPositionPass,
            fmt::format(
                "luaWorld=({},{},{}) cppWorld=({},{},{}) luaScreen=({},{}) cppScreen=({},{}) "
                "before=({},{})",
                g_luaCursorWorld.x,
                g_luaCursorWorld.y,
                g_luaCursorWorld.z,
                g_cppCursorWorld.x,
                g_cppCursorWorld.y,
                g_cppCursorWorld.z,
                g_luaCursorScreen.x,
                g_luaCursorScreen.y,
                g_cppCursorScreen.x,
                g_cppCursorScreen.y,
                g_cursorScreenBefore.x,
                g_cursorScreenBefore.y
            )
        );
    } else if (shotIndex == kPortraitShot) {
        assertPortraitSubject(shotIndex, g_portraitFirstSubject);
    } else if (shotIndex == kPortraitRetargetShot) {
        assertPortraitRetargetCamera(shotIndex);
        assertPortraitSubject(shotIndex, g_portraitSecondSubject);
    } else if (shotIndex == kRowShot) {
        logGuiAssert(
            shotIndex,
            "LUA_ROW_REGION",
            g_rowRegion,
            "row_region",
            g_rowEvents == "HCU" && g_rowEventIdsMatch && g_rowRegion != IREntity::kNullEntity,
            "events=" + g_rowEvents + (g_rowEventIdsMatch ? "" : " (wrong id)")
        );
    } else if (shotIndex == kScrollOffPanelShot || shotIndex == kScrollOnPanelShot) {
        const bool onPanel = shotIndex == kScrollOnPanelShot;
        const std::string &after = scrollFirstRowText();
        const char *expectedAfter = onPanel ? kScrollAdvancedRowText : kScrollFirstRowText;
        logGuiAssert(
            shotIndex,
            onPanel ? "LUA_SCROLL_ON_PANEL" : "LUA_SCROLL_OFF_PANEL",
            g_scrollFirstRow,
            "scroll_first_row",
            g_scrollFirstRowBefore == kScrollFirstRowText && after == expectedAfter,
            "before='" + g_scrollFirstRowBefore + "' after='" + after + "'"
        );
    } else if (
        shotIndex == kSliderDragShot || shotIndex == kSliderUndoShot || shotIndex == kSliderRedoShot
    ) {
        const float value = IRPrefab::Widget::sliderValue(g_undoSlider);
        const float expected = shotIndex == kSliderUndoShot ? g_undoSliderStart : g_undoSliderFinal;
        const bool pass = g_undoSlider != IREntity::kNullEntity &&
                          IRMath::abs(value - expected) <= 0.001f &&
                          g_undoSliderFinal != g_undoSliderStart;
        logGuiAssert(
            shotIndex,
            "LUA_UNDO_SLIDER",
            g_undoSlider,
            shotIndex == kSliderDragShot
                ? "slider_dragged"
                : (shotIndex == kSliderUndoShot ? "slider_undone" : "slider_redone"),
            pass,
            fmt::format("value={} expected={}", value, expected)
        );
    } else if (shotIndex == kDropdownScrollShot) {
        const auto &dropdown = IREntity::getComponent<IRComponents::C_WidgetDropdown>(g_dropdown);
        logGuiAssert(
            shotIndex,
            "DROPDOWN_WINDOW",
            g_dropdown,
            "dropdown_top_index",
            dropdown.isOpen_ && dropdownTopIndex() == kDropdownScrollSteps,
            fmt::format("open={} topIndex={}", dropdown.isOpen_, dropdownTopIndex())
        );
        const IREntity::EntityId hovered = IRPrefab::Widget::hoveredWidget();
        logGuiAssert(
            shotIndex,
            "DROPDOWN_HOVERED",
            g_dropdown,
            "dropdown_hovered",
            hovered == g_dropdown && g_dropdown != IREntity::kNullEntity,
            std::to_string(hovered)
        );
        logGuiAssert(
            shotIndex,
            "DROPDOWN_ZOOM_HELD",
            g_dropdown,
            "dropdown_zoom_held",
            cameraZoom() == g_zoomBefore,
            fmt::format("before={} after={}", g_zoomBefore, cameraZoom())
        );
    } else if (shotIndex == kWheelZoomControlShot) {
        logGuiAssert(
            shotIndex,
            "WHEEL_ZOOMS_CAMERA",
            IREntity::kNullEntity,
            "wheel_zoom_control",
            cameraZoom() != g_zoomBefore,
            fmt::format("before={} after={}", g_zoomBefore, cameraZoom())
        );
    } else if (shotIndex == kHotkeyHintsHeldShot || shotIndex == kHotkeyHintsReleasedShot) {
        const bool held = shotIndex == kHotkeyHintsHeldShot;
        const std::size_t glyphs = IRPrefab::Widget::lastHintGlyphCommandCount();
        const bool visible = IRPrefab::Widget::hintsVisible();
        logGuiAssert(
            shotIndex,
            "HOTKEY_HINTS",
            IREntity::kNullEntity,
            held ? "hints_visible" : "hints_hidden",
            glyphs == (held ? 3u : 0u) && visible == held,
            fmt::format("glyphs={} visible={}", glyphs, visible)
        );
    } else if (shotIndex == kHotkeyCheckboxShot) {
        const IREntity::EntityId checkbox = hotkeyWidget(IRInput::kKeyButtonC);
        const bool checked = IRPrefab::Widget::checkboxState(checkbox);
        const bool focused = IRPrefab::Widget::focusedWidget() == checkbox &&
                             IRPrefab::Widget::focusVisible() &&
                             IRPrefab::Widget::lastFocusRingWidget() == checkbox;
        logGuiAssert(
            shotIndex,
            "HOTKEY_CHECKBOX",
            checkbox,
            "checkbox_focus",
            checked && focused,
            fmt::format("checked={} focused={}", checked, focused)
        );
    } else if (shotIndex == kHotkeySliderShot || shotIndex == kHotkeySliderResetShot) {
        const IREntity::EntityId slider = hotkeyWidget(IRInput::kKeyButtonS);
        const float value = IRPrefab::Widget::sliderValue(slider);
        const float expected = shotIndex == kHotkeySliderShot ? 6.0f : 5.0f;
        logGuiAssert(
            shotIndex,
            "HOTKEY_SLIDER",
            slider,
            shotIndex == kHotkeySliderShot ? "slider_nudged" : "slider_reset",
            value == expected,
            fmt::format("value={} expected={}", value, expected)
        );
    } else if (shotIndex == kHotkeyPointerShot) {
        logGuiAssert(
            shotIndex,
            "POINTER_FOCUS",
            hotkeyWidget(IRInput::kKeyButtonB),
            "focus_ring_hidden",
            IRPrefab::Widget::lastFocusRingWidget() == IREntity::kNullEntity,
            fmt::format("ring={}", IRPrefab::Widget::lastFocusRingWidget())
        );
    } else if (shotIndex == kLongDropdownKeyboardShot) {
        assertLongDropdownPlaced(shotIndex);
        const auto &dropdown =
            IREntity::getComponent<IRComponents::C_WidgetDropdown>(g_longDropdown);
        const int highlighted = static_cast<int>(
            IREntity::getComponent<IRComponents::C_WidgetState>(g_longDropdown).dragValue_
        );
        const int top = longDropdownTopIndex();
        logGuiAssert(
            shotIndex,
            "DROPDOWN_LONG_KEYBOARD",
            g_longDropdown,
            "keyboard_row_in_window",
            dropdown.isOpen_ && highlighted == kLongDropdownDownPresses &&
                top == kLongDropdownDownPresses - IRComponents::kDropdownMaxVisibleRows + 1,
            fmt::format("open={} highlighted={} topIndex={}", dropdown.isOpen_, highlighted, top)
        );
    } else if (shotIndex == kLongDropdownWheelShot) {
        assertLongDropdownPlaced(shotIndex);
        const int top = longDropdownTopIndex();
        logGuiAssert(
            shotIndex,
            "DROPDOWN_LONG_WHEEL",
            g_longDropdown,
            "upward_window_scrolled",
            IRPrefab::Widget::hoveredWidget() == g_longDropdown && g_longDropdownTopBefore >= 0 &&
                top == g_longDropdownTopBefore + 1,
            fmt::format(
                "hovered={} before={} after={}",
                IRPrefab::Widget::hoveredWidget(),
                g_longDropdownTopBefore,
                top
            )
        );
    } else if (shotIndex == kLongListWheelShot) {
        const int top = longListTopIndex();
        const int selected = IRPrefab::Widget::listSelectedIndex(g_longList) + 1;
        const int revealedTop = g_longListSelected - longListVisibleRows();
        logGuiAssert(
            shotIndex,
            "LUA_LIST_WHEEL",
            g_longList,
            "list_window_scrolled",
            g_longListTopBefore == revealedTop && top == revealedTop + 1 &&
                selected == g_longListSelected,
            fmt::format(
                "before={} after={} expectedBefore={} selected={}",
                g_longListTopBefore,
                top,
                revealedTop,
                selected
            )
        );
    } else if (shotIndex == kDropdownTypeaheadShot) {
        const auto &dropdown =
            IREntity::getComponent<IRComponents::C_WidgetDropdown>(g_longDropdown);
        const auto &position =
            IREntity::getComponent<IRComponents::C_GuiPosition>(g_longDropdown).pos_;
        const auto layout = IRPrefab::Widget::dropdownStripLayout(g_longDropdown);
        logGuiAssert(
            shotIndex,
            "DROPDOWN_TYPEAHEAD_NARROWED",
            g_longDropdown,
            "typeahead_rows",
            dropdown.isOpen_ && dropdown.typeahead_ == "lo" && dropdown.shownItemCount() == 4 &&
                layout.rows_ == 4 &&
                position.y + layout.offsetY_ + dropdown.stripHeight(layout) == position.y &&
                IRPrefab::WidgetText::dropdownHeaderLabel(dropdown) == "lo",
            fmt::format(
                "open={} query={} shown={} rows={} offset={}",
                dropdown.isOpen_,
                dropdown.typeahead_,
                dropdown.shownItemCount(),
                layout.rows_,
                layout.offsetY_
            )
        );
    } else if (shotIndex == kDropdownTypeaheadPickShot) {
        const auto &dropdown =
            IREntity::getComponent<IRComponents::C_WidgetDropdown>(g_longDropdown);
        logGuiAssert(
            shotIndex,
            "DROPDOWN_TYPEAHEAD_PICKED",
            g_longDropdown,
            "typeahead_pick",
            !dropdown.isOpen_ && dropdown.typeahead_.empty() && dropdown.selectedIndex_ == 1,
            fmt::format(
                "open={} query={} selected={}",
                dropdown.isOpen_,
                dropdown.typeahead_,
                dropdown.selectedIndex_
            )
        );
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
    IREngine::args().enumValue(
        "--hand-off-fixture",
        "Add the IREngine.handOff fixture: off | button | auto",
        {"off", "button", "auto"},
        "off"
    );
    registerLuaBindings();
    IREngine::init(argc, argv, "config.lua");
    if (IREngine::args().getEnum("--hand-off-fixture") != "off") {
        IREngine::setToolRoot(IR_LUA_WIDGETS_TOOL_ROOT);
    }
    IRLuaWidgets::g_autoWarmupFrames = IREngine::args().autoScreenshotWarmupFrames();
    IRPrefab::Undo::registerUndoCommands();
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
        script.bindLuaUndo();

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
        lua["IRTest"]["setPairedValueRow"] = [](lua_Integer slider, lua_Integer field) {
            IRLuaWidgets::g_pairedSlider = static_cast<IREntity::EntityId>(slider);
            IRLuaWidgets::g_pairedField = static_cast<IREntity::EntityId>(field);
        };
        lua["IRTest"]["onClickFired"] = []() { IRLuaWidgets::g_luaOnClickFired = true; };
        lua["IRTest"]["onPollFired"] = []() { IRLuaWidgets::g_pollWasClickedSeen = true; };
        lua["IRTest"]["handOffFixture"] = []() {
            return IREngine::args().getEnum("--hand-off-fixture");
        };
        lua["IRTest"]["engineTick"] = []() { return static_cast<lua_Integer>(IRTime::tick()); };
        lua["IRTest"]["closeWindow"] = []() { IRWindow::closeWindow(); };
        lua["IRTest"]["setRowRegion"] = [](lua_Integer region) {
            IRLuaWidgets::g_rowRegion = static_cast<IREntity::EntityId>(region);
        };
        lua["IRTest"]["setScrollFirstRow"] = [](lua_Integer label) {
            IRLuaWidgets::g_scrollFirstRow = static_cast<IREntity::EntityId>(label);
        };
        lua["IRTest"]["setUndoSlider"] = [](lua_Integer slider, float start) {
            IRLuaWidgets::g_undoSlider = static_cast<IREntity::EntityId>(slider);
            IRLuaWidgets::g_undoSliderStart = start;
            IRLuaWidgets::g_undoSliderFinal = start;
        };
        lua["IRTest"]["undoSliderChanged"] = [](float value) {
            IRLuaWidgets::g_undoSliderFinal = value;
        };
        lua["IRTest"]["setDropdown"] = [](lua_Integer dropdown) {
            IRLuaWidgets::g_dropdown = static_cast<IREntity::EntityId>(dropdown);
        };
        lua["IRTest"]["setLongDropdown"] = [](lua_Integer dropdown) {
            IRLuaWidgets::g_longDropdown = static_cast<IREntity::EntityId>(dropdown);
        };
        lua["IRTest"]["setLongList"] = [](lua_Integer list, int selected) {
            IRLuaWidgets::g_longList = static_cast<IREntity::EntityId>(list);
            IRLuaWidgets::g_longListSelected = selected;
        };
        lua["IRTest"]["rowHovered"] = [](lua_Integer id) { IRLuaWidgets::recordRowEvent('H', id); };
        lua["IRTest"]["rowClicked"] = [](lua_Integer id) { IRLuaWidgets::recordRowEvent('C', id); };
        lua["IRTest"]["rowUnhovered"] = [](lua_Integer id) {
            IRLuaWidgets::recordRowEvent('U', id);
        };
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
        lua["IRTest"]["cursorPosition"] = [](float luaWorldX,
                                             float luaWorldY,
                                             float luaWorldZ,
                                             float luaScreenX,
                                             float luaScreenY,
                                             float refX,
                                             float refY,
                                             float refZ) {
            const IRMath::vec3 reference(refX, refY, refZ);
            const float canvasIsoDepth = IRRender::canvasIsoDepthOfWorldPos(reference);
            IRLuaWidgets::g_luaCursorWorld = IRMath::vec3(luaWorldX, luaWorldY, luaWorldZ);
            IRLuaWidgets::g_cppCursorWorld = IRRender::mouseWorldPos3DAtIsoDepth(canvasIsoDepth);
            IRLuaWidgets::g_luaCursorScreen = IRMath::vec2(luaScreenX, luaScreenY);
            IRLuaWidgets::g_cppCursorScreen = IRRender::mousePosition2DIsoScreenRender();
            IRLuaWidgets::g_cursorWorldMatches =
                IRLuaWidgets::g_luaCursorWorld.x == IRLuaWidgets::g_cppCursorWorld.x &&
                IRLuaWidgets::g_luaCursorWorld.y == IRLuaWidgets::g_cppCursorWorld.y &&
                IRLuaWidgets::g_luaCursorWorld.z == IRLuaWidgets::g_cppCursorWorld.z;
            IRLuaWidgets::g_cursorScreenMatches =
                IRLuaWidgets::g_luaCursorScreen.x == IRLuaWidgets::g_cppCursorScreen.x &&
                IRLuaWidgets::g_luaCursorScreen.y == IRLuaWidgets::g_cppCursorScreen.y;
            const IRMath::vec3 rotated = IRMath::rotateCardinalZ(
                IRLuaWidgets::g_luaCursorWorld,
                IRMath::rasterYawCardinalIndex(IRPrefab::Camera::getRasterYaw())
            );
            IRLuaWidgets::g_cursorPlaneMatches =
                IRMath::abs(rotated.x + rotated.y + rotated.z - canvasIsoDepth) <= 0.001f;
            IRLuaWidgets::g_cursorPositionHookRan = true;
        };
    });
}

void initSystems() {
    // The wheel yields to a hovered widget only through this singleton.
    IRPrefab::Widget::makeGuiHoverState();

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
            IRSystem::createSystem<IRSystem::WIDGET_APPLY_LIST>(),
            IRSystem::createSystem<IRSystem::WIDGET_APPLY_SLIDER>(),
            IRSystem::createSystem<IRSystem::WIDGET_APPLY_CHECKBOX>(),
            IRSystem::createSystem<IRSystem::WIDGET_APPLY_DROPDOWN>(),
            IRSystem::createSystem<IRSystem::WIDGET_APPLY_RADIO>(),
            IRSystem::createSystem<IRSystem::WIDGET_APPLY_NUMBER_FIELD>(),
            IRLuaWidgets::g_dispatchId,
            IRSystem::System<IRSystem::CAMERA_SCROLL_ZOOM>::create(),
        }
    );

    // UPDATE — main.lua appends its wasClicked-poll system here via
    // IRSystem.appendSystem(IRTime.UPDATE, ...), so the pipeline must exist.
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {
            IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
            IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
            IRSystem::createSystem<IRSystem::LIFETIME>(),
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
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_SLIDER>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_CHECKBOX>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_RADIO>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_NUMBER_FIELD>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_LIST>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_HINTS>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_DROPDOWN>(),
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::VIEWPORT_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
            IRSystem::createSystem<IRSystem::GUI_CANVAS_TO_SCREEN>(),
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

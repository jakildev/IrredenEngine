#ifndef COMPONENT_WIDGET_H
#define COMPONENT_WIDGET_H

#include <irreden/ir_math.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace IRComponents {

// Closed set of foundation primitives. Phase 0 F-0.1 (T-145) shipped PANEL,
// LABEL, BUTTON, SLIDER, CHECKBOX; the follow-up (T-177) adds LIST, DROPDOWN,
// RADIO, TEXT_INPUT, SCROLL. Existing enum values must not be renumbered —
// the int values are referenced by widget z-order heuristics and (eventually)
// by serialized layouts.
enum class WidgetKind : int {
    PANEL = 0,
    LABEL = 1,
    BUTTON = 2,
    SLIDER = 3,
    CHECKBOX = 4,
    LIST = 5,
    DROPDOWN = 6,
    RADIO = 7,
    TEXT_INPUT = 8,
    SCROLL = 9,
};

// Common widget header. Every widget entity carries this alongside its
// per-kind data component (C_WidgetButton, C_WidgetSlider, etc.). Pos
// + size are in GUI-canvas-trixel coordinates (top-left origin); the
// system reads them via the entity's sibling C_GuiPosition / its own
// `size_` field rather than re-deriving from C_HitBox2DGui so a panel
// (no hitbox) and a button (hitbox) share the same draw path.
struct C_Widget {
    WidgetKind kind_ = WidgetKind::PANEL;
    IRMath::ivec2 size_ = IRMath::ivec2(0, 0);
    bool disabled_ = false;
    int zOrder_ = 0; // higher = drawn on top; wins hover when widgets overlap

    C_Widget() = default;
    C_Widget(WidgetKind kind, IRMath::ivec2 size, int zOrder = 0)
        : kind_{kind}
        , size_{size}
        , zOrder_{zOrder} {}
};

// Interactive state — written by the WIDGET_INPUT system, read by the
// WIDGET_TO_TRIXEL system (for hover/pressed coloring) and by consumer
// systems (for `fireAction_`). Non-interactive widgets (PANEL, LABEL)
// keep this at default — the input system iterates `C_Widget +
// C_HitBox2DGui + C_WidgetState` so the non-hitbox kinds are skipped.
struct C_WidgetState {
    bool hovered_ = false;    // mouse currently inside hitbox
    bool pressed_ = false;    // left button held since press-on-hover
    bool focused_ = false;    // keyboard focus; set by WIDGET_INPUT (Tab/click)
    bool fireAction_ = false; // true for one frame on click-released-over-widget
    float dragValue_ = 0.0f;  // slider's normalized drag value while pressed
};

// PANEL — bounded rectangle container with an optional title bar.
struct C_WidgetPanel {
    std::string title_; // empty = no title bar
    bool drawBorder_ = true;
};

// LABEL — text-only widget. The string is stored here rather than
// using a sibling C_TextSegment so the widget render system controls
// the draw distance + color state itself (and we don't need to
// register the label against TEXT_TO_TRIXEL's archetype).
struct C_WidgetLabel {
    std::string text_;
    IRMath::Color colorOverride_ = IRMath::Color{0, 0, 0, 0}; // alpha=0 means "use theme"
};

// BUTTON — clickable rect with a label. The click signal lives on
// C_WidgetState::fireAction_; consumers poll via
// `IRPrefab::Widget::wasClicked(eid)`.
struct C_WidgetButton {
    std::string label_;
};

// SLIDER — horizontal numeric slider. minValue_ / maxValue_ define
// the value range; currentValue_ is updated by the input system on
// drag and read by `IRPrefab::Widget::sliderValue(eid)`. Visual track
// runs the full widget width; thumb position is interpolated.
struct C_WidgetSlider {
    std::string label_;
    float minValue_ = 0.0f;
    float maxValue_ = 1.0f;
    float currentValue_ = 0.5f;
};

// CHECKBOX — toggle. checked_ flips on click-release inside the
// hitbox. Read via `IRPrefab::Widget::checkboxState(eid)`.
struct C_WidgetCheckbox {
    std::string label_;
    bool checked_ = false;
};

// LIST — vertical selectable item list with an optional scroll offset.
// itemHeight_ is the per-row pixel height; visible rows = size_.y /
// itemHeight_. `scrollOffset_` is the index of the topmost visible
// item; clamped by WIDGET_APPLY_LIST so a partial-content list never
// scrolls off the bottom.
struct C_WidgetList {
    std::vector<std::string> items_;
    int selectedIndex_ = -1; // -1 = nothing selected
    int scrollOffset_ = 0;
    int itemHeight_ = 18;
};

// DROPDOWN — collapsed combo expanding to a vertical list. When
// closed, only the selected label is visible. When `isOpen_` is true,
// WIDGET_RENDER_DROPDOWN renders the expanded item list below the
// header and the dropdown's effective hit region grows to cover both
// (kept in sync by WIDGET_APPLY_DROPDOWN).
struct C_WidgetDropdown {
    std::vector<std::string> items_;
    int selectedIndex_ = -1;
    bool isOpen_ = false;
    int itemHeight_ = 18;
};

// RADIO — exclusive group of toggles. `groupId_` ties radios to one
// another within a creation; clicking one sets `selected_` on it and
// clears every sibling that shares the same group id (handled by
// WIDGET_APPLY_RADIO, which iterates all radios per fire).
struct C_WidgetRadio {
    std::string label_;
    std::uint32_t groupId_ = 0;
    int value_ = 0; // application-visible value emitted when selected
    bool selected_ = false;
};

// TEXT_INPUT — single-line editable text. WIDGET_APPLY_TEXT_INPUT
// edits the buffer only when the widget owns keyboard focus (set by
// the generic WIDGET_INPUT click-to-focus / Tab-cycle paths). Cursor
// is a logical character index 0..text.size(); blink phase is driven
// by a shared frame counter held on WIDGET_APPLY_TEXT_INPUT itself.
// `maxLength_ = 0` disables the length cap.
struct C_WidgetTextInput {
    std::string text_;
    int cursorPos_ = 0;
    int maxLength_ = 0;
};

// SCROLL — bounded viewport that paints a scroll track + thumb along
// its right edge (vertical) or bottom edge (horizontal). The widget is
// a primitive in its own right — it doesn't reparent children. The
// owning creation positions content relative to `scrollPos_` itself
// (the dock-preview demo extension uses the same pattern). Drag on the
// track jumps the thumb; drag on the thumb scrubs proportionally.
struct C_WidgetScroll {
    enum class Axis : int { VERTICAL = 0, HORIZONTAL = 1 };

    Axis axis_ = Axis::VERTICAL;
    int contentSize_ = 0; // total pixels of scrollable content along axis_
    int scrollPos_ = 0;   // current scroll offset, clamped to [0, max(0, contentSize_ - viewSize)]
};

} // namespace IRComponents

#endif /* COMPONENT_WIDGET_H */

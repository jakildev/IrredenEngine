#ifndef COMPONENT_WIDGET_H
#define COMPONENT_WIDGET_H

#include <irreden/ir_math.hpp>

#include <string>

namespace IRComponents {

// Closed set of foundation primitives. Phase 0 F-0.1 ships PANEL, LABEL,
// BUTTON, SLIDER, CHECKBOX; the remaining five (LIST, DROPDOWN, RADIO,
// TEXT_INPUT, SCROLL) follow once a downstream phase needs them.
enum class WidgetKind : int {
    PANEL = 0,
    LABEL = 1,
    BUTTON = 2,
    SLIDER = 3,
    CHECKBOX = 4,
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

    C_Widget() = default;
    C_Widget(WidgetKind kind, IRMath::ivec2 size)
        : kind_{kind}
        , size_{size} {}
};

// Interactive state — written by the WIDGET_INPUT system, read by the
// WIDGET_TO_TRIXEL system (for hover/pressed coloring) and by consumer
// systems (for `fireAction_`). Non-interactive widgets (PANEL, LABEL)
// keep this at default — the input system iterates `C_Widget +
// C_HitBox2DGui + C_WidgetState` so the non-hitbox kinds are skipped.
struct C_WidgetState {
    bool hovered_ = false;        // mouse currently inside hitbox
    bool pressed_ = false;        // left button held since press-on-hover
    bool focused_ = false;        // keyboard focus (reserved for Phase 0 follow-up)
    bool fireAction_ = false;     // true for one frame on click-released-over-widget
    float dragValue_ = 0.0f;      // slider's normalized drag value while pressed
};

// PANEL — bounded rectangle container with an optional title bar.
struct C_WidgetPanel {
    std::string title_;           // empty = no title bar
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

} // namespace IRComponents

#endif /* COMPONENT_WIDGET_H */

#ifndef COMPONENT_WIDGET_H
#define COMPONENT_WIDGET_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/render/trixel_font.hpp>

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
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
    COLOR_SWATCH = 10,
    // Invisible interactive rectangle: hover / press / click like a button,
    // drawn by no renderer. For grouping things drawn elsewhere (text, a
    // swatch strip on another canvas) into one hover / click target.
    HIT_REGION = 11,
    NUMBER_FIELD = 12,
    // A PNG box-filtered into the widget's trixel rect; data on C_WidgetImage.
    IMAGE = 13,
};

// Z-order bias WIDGET_APPLY_DROPDOWN adds to a dropdown for as long as it is
// expanded, so its item strip out-ranks whatever it overlaps in WIDGET_INPUT's
// hover routing. It lives here rather than beside that system because it is a
// contract on `C_Widget::zOrder_` below: every authored z-order must stay under
// it, or the bias inverts and the expanded strip loses the tie-break it exists
// to win. The `C_Widget` ctor asserts that.
inline constexpr int kWidgetDropdownOpenZBias = 1000;

// Most item rows an expanded dropdown shows at once unless its
// `C_WidgetDropdown::visibleRows_` says otherwise; a longer list scrolls inside
// that window.
inline constexpr int kDropdownMaxVisibleRows = 8;

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
    // Declared cover: the GUI layout audit drops an overlap or text-over-widget
    // finding against this widget's covering surface — a panel's background, a
    // dropdown's open strip, the whole rect of any other kind. A panel's title
    // and a dropdown's header are never covering.
    bool coversBelow_ = false;
    int zOrder_ = 0; // higher = drawn on top; wins hover when widgets overlap

    C_Widget() = default;
    C_Widget(WidgetKind kind, IRMath::ivec2 size, int zOrder = 0)
        : kind_{kind}
        , size_{size}
        , zOrder_{zOrder} {
        IR_ASSERT(
            zOrder < kWidgetDropdownOpenZBias,
            "C_Widget authored at zOrder_ >= kWidgetDropdownOpenZBias — an "
            "expanded dropdown biases into that range, so this widget would "
            "out-rank the item strip and make its rows unclickable."
        );
    }
};

// Interactive state — written by the WIDGET_INPUT system, read by the
// WIDGET_TO_TRIXEL system (for hover/pressed coloring) and by consumer
// systems (for `fireAction_`). Non-interactive widgets (PANEL, LABEL)
// keep this at default — the input system iterates `C_Widget +
// C_HitBox2DGui + C_WidgetState` so the non-hitbox kinds are skipped.
struct C_WidgetState {
    enum class KeyAction : std::uint8_t {
        NONE = 0,
        ACTIVATE,
        OPEN_OR_CLOSE,
        MOVE_UP,
        MOVE_DOWN,
        MINIMUM,
        MAXIMUM,
        RESET,
        SELECT,
    };
    bool hovered_ = false;    // mouse currently inside hitbox
    bool pressed_ = false;    // left button held since press-on-hover
    bool focused_ = false;    // keyboard focus; text inputs are pointer-focus-only
    bool fireAction_ = false; // true for one frame on click-released-over-widget
    float dragValue_ = 0.0f;  // slider's normalized drag value while pressed
    IRMath::vec2 sliderPressPosition_ = IRMath::vec2(0.0f);
    float sliderPressFraction_ = 0.0f;
    float sliderPressValue_ = 0.0f;
    float sliderFirstClickValue_ = 0.0f;
    std::uint8_t sliderClickAge_ = 0;
    bool sliderShiftDrag_ = false;
    bool sliderSteppedDrag_ = false;
    bool sliderPressStarted_ = false;
    bool sliderReleased_ = false;
    bool sliderDoubleClick_ = false;
    KeyAction keyAction_ = KeyAction::NONE;
    bool keyShiftHeld_ = false;

    enum class SliderPressRegion : std::uint8_t {
        NONE = 0,
        THUMB = 1,
        TRACK = 2,
    };
    SliderPressRegion sliderActiveRegion_ = SliderPressRegion::NONE;
    SliderPressRegion sliderFirstClickRegion_ = SliderPressRegion::NONE;
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
    IRRender::TextSize textSize_ = IRRender::TextSize::LARGE;
};

// BUTTON — clickable rect with a label. The click signal lives on
// C_WidgetState::fireAction_; consumers poll via
// `IRPrefab::Widget::wasClicked(eid)`.
struct C_WidgetButton {
    std::string label_;
    IRRender::TextSize textSize_ = IRRender::TextSize::LARGE;
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
    float defaultValue_ = 0.5f;
    float step_ = 0.01f;
    IREntity::EntityId fieldId_ = IREntity::kNullEntity;
    // The label row is one line of this size; label and value readout share it.
    IRRender::TextSize textSize_ = IRRender::TextSize::LARGE;
};

// CHECKBOX — toggle. checked_ flips on click-release inside the
// hitbox. Read via `IRPrefab::Widget::checkboxState(eid)`.
struct C_WidgetCheckbox {
    std::string label_;
    bool checked_ = false;
    IRRender::TextSize textSize_ = IRRender::TextSize::LARGE;
};

// LIST — vertical selectable item list that scrolls by whole rows.
// `scrollOffset_` is the requested index of the topmost visible item. It may
// be stale: `items_` can shrink under it at any point in a frame, so every
// reader takes the first visible item from `topIndex`, never from the field.
struct C_WidgetList {
    std::vector<std::string> items_;
    int selectedIndex_ = -1; // -1 = nothing selected
    int scrollOffset_ = 0;
    int itemHeight_ = 18;
    // Row text; row geometry stays itemHeight_.
    IRRender::TextSize textSize_ = IRRender::TextSize::LARGE;

    /// Passive position thumb, in GUI-canvas trixels below the widget's top
    /// edge. `height_` is zero when every item fits the view.
    struct ThumbSpan {
        int offsetY_ = 0;
        int height_ = 0;
    };

    // --- Scroll geometry --------------------------------------------------
    //
    // WIDGET_APPLY_LIST (wheel, click row), WIDGET_RENDER_LIST (rows, thumb),
    // `IRPrefab::Widget::setListSelectedIndex` (reveal) and any headless test
    // aiming a scripted click at a row all derive from these helpers, so the
    // row a click resolves to is the row that was painted. `viewHeight` is
    // the widget's `C_Widget::size_.y`; offsets are relative to its top edge.

    /// `itemHeight_` floored at one trixel — the divisor every row
    /// calculation uses, so an authored zero cannot divide by zero.
    int rowHeight() const {
        return IRMath::max(1, itemHeight_);
    }

    /// Whole rows that fit the view. A trailing partial row is not a row.
    int visibleRows(int viewHeight) const {
        return IRMath::max(0, viewHeight) / rowHeight();
    }

    /// Largest top index that still fills the view with items.
    int maxScrollOffset(int viewHeight) const {
        return IRMath::max(0, static_cast<int>(items_.size()) - visibleRows(viewHeight));
    }

    /// Index of the first visible item: `scrollOffset_` clamped to the content.
    int topIndex(int viewHeight) const {
        return IRMath::clamp(scrollOffset_, 0, maxScrollOffset(viewHeight));
    }

    /// Moves the view by @p rows (positive = toward the end), stopping at
    /// either end of the content.
    void scrollBy(int rows, int viewHeight) {
        scrollOffset_ = IRMath::clamp(topIndex(viewHeight) + rows, 0, maxScrollOffset(viewHeight));
    }

    /// Brings item @p index into view with the least movement: a row above
    /// the view lands on the top row, a row below it on the bottom row, and a
    /// visible row moves nothing. An index outside `items_` is ignored.
    void scrollIntoView(int index, int viewHeight) {
        const int rows = visibleRows(viewHeight);
        if (index < 0 || index >= static_cast<int>(items_.size()) || rows == 0) {
            return;
        }
        const int top = topIndex(viewHeight);
        if (index < top) {
            scrollOffset_ = index;
        } else if (index >= top + rows) {
            scrollOffset_ = index - rows + 1;
        } else {
            scrollOffset_ = top;
        }
    }

    /// Offset from the widget's top edge to the centre of visible row
    /// @p visibleRow (0 = the top row).
    float rowCenterOffsetY(int visibleRow) const {
        return (static_cast<float>(visibleRow) + 0.5f) * static_cast<float>(rowHeight());
    }

    /// Item index @p offsetY trixels below the widget's top edge, or -1 when
    /// that point is above the view, on the trailing partial row, or on a row
    /// past the last item.
    int itemAtOffsetY(int viewHeight, float offsetY) const {
        if (offsetY < 0.0f) {
            return -1;
        }
        const int row = static_cast<int>(offsetY / static_cast<float>(rowHeight()));
        if (row >= visibleRows(viewHeight)) {
            return -1;
        }
        const int index = topIndex(viewHeight) + row;
        return index < static_cast<int>(items_.size()) ? index : -1;
    }

    /// Where the position thumb sits along the view's height. Its height is
    /// the visible fraction of the content, floored at @p minHeight and capped
    /// at the view.
    ThumbSpan thumbSpan(int viewHeight, int minHeight) const {
        const int rows = visibleRows(viewHeight);
        const int count = static_cast<int>(items_.size());
        if (rows == 0 || count <= rows) {
            return {};
        }
        const int height = IRMath::clamp(
            viewHeight * rows / count,
            IRMath::clamp(minHeight, 1, viewHeight),
            viewHeight
        );
        const int offsetY = (viewHeight - height) * topIndex(viewHeight) / (count - rows);
        return {offsetY, height};
    }
};

// DROPDOWN — collapsed combo expanding to a vertical item strip. When
// closed, only the selected label is visible. When `isOpen_` is true,
// WIDGET_RENDER_DROPDOWN renders the strip where `stripLayout` places it —
// below the header, or above it when only that side has room — and the
// dropdown's effective hit region grows to cover header and strip (kept in
// sync by WIDGET_APPLY_DROPDOWN).
struct C_WidgetDropdown {
    std::vector<std::string> items_;
    int selectedIndex_ = -1;
    bool isOpen_ = false;
    int itemHeight_ = 18;
    // Header and item text; row geometry stays itemHeight_.
    IRRender::TextSize textSize_ = IRRender::TextSize::LARGE;
    // Most rows the open strip asks for; 0 = `kDropdownMaxVisibleRows`. The
    // canvas can grant fewer (`stripLayout`).
    int visibleRows_ = 0;

    // Index of the first row in the strip window; read it through topIndex().
    int scrollOffset_ = 0;

    /// Where an open strip sits for one header placement: `offsetY_` is the
    /// strip's top edge relative to the header's top edge (the header height
    /// below it, negative above it), `rows_` the whole rows it shows.
    struct StripLayout {
        int offsetY_ = 0;
        int rows_ = 0;
    };

    // --- Expanded-strip geometry ------------------------------------------
    //
    // Every consumer must agree on where the item rows are:
    // WIDGET_APPLY_DROPDOWN (hitbox extent, wheel / keyboard window, hover-row
    // routing), WIDGET_RENDER_DROPDOWN (where it paints them), the GUI layout
    // audit, and any caller aiming a scripted click at a row or revealing an
    // item outside a tick. Each derives one `StripLayout` from `stripLayout`
    // and passes it to the helpers below — a duplicated formula drifts
    // silently, and the only symptom is a click landing on the wrong row.
    // Offsets are in GUI-canvas trixels relative to the widget's own top-left;
    // `headerHeight` is the collapsed widget's `C_Widget::size_.y`. The strip
    // shows `rows_` rows starting at `topIndex`; item indices stay absolute
    // throughout.

    /// `itemHeight_` floored at one trixel — the divisor every row
    /// calculation uses, so an authored zero cannot divide by zero.
    int rowHeight() const {
        return IRMath::max(1, itemHeight_);
    }

    /// Rows the strip asks for before the canvas has its say.
    int requestedRowCount() const {
        const int cap = visibleRows_ > 0 ? visibleRows_ : kDropdownMaxVisibleRows;
        return IRMath::min(static_cast<int>(items_.size()), cap);
    }

    /// Places the strip for a header whose top edge is @p headerY on a GUI
    /// canvas @p canvasHeight tall: below the header when the requested rows
    /// fit there, else above it when they fit there, else on the side with
    /// more whole rows (below on a tie) cut to that many. Never a partial row,
    /// never past either canvas edge.
    StripLayout stripLayout(int headerY, int headerHeight, int canvasHeight) const {
        const int rowH = rowHeight();
        const int requested = requestedRowCount();
        const int canvas = IRMath::max(0, canvasHeight);
        const int roomBelow = IRMath::clamp(canvas - (headerY + headerHeight), 0, canvas);
        const int roomAbove = IRMath::clamp(headerY, 0, canvas);
        const int rowsBelow = roomBelow / rowH;
        const int rowsAbove = roomAbove / rowH;
        if (requested <= rowsBelow) {
            return {headerHeight, requested};
        }
        if (requested <= rowsAbove) {
            return {-requested * rowH, requested};
        }
        if (rowsBelow >= rowsAbove) {
            return {headerHeight, rowsBelow};
        }
        return {-rowsAbove * rowH, rowsAbove};
    }

    /// Largest top index that still fills @p layout's window with items.
    int maxScrollOffset(const StripLayout &layout) const {
        return IRMath::max(0, static_cast<int>(items_.size()) - layout.rows_);
    }

    /// Index of the first visible item: `scrollOffset_` clamped to the content.
    int topIndex(const StripLayout &layout) const {
        return IRMath::clamp(scrollOffset_, 0, maxScrollOffset(layout));
    }

    /// Moves the window by @p rows (positive = toward the end), stopping at
    /// either end of the content.
    void scrollBy(int rows, const StripLayout &layout) {
        scrollOffset_ = IRMath::clamp(topIndex(layout) + rows, 0, maxScrollOffset(layout));
    }

    /// Brings item @p index into the window with the least movement. An index
    /// outside `items_`, or a window with no rows, is ignored.
    void scrollIntoView(int index, const StripLayout &layout) {
        if (index < 0 || index >= static_cast<int>(items_.size()) || layout.rows_ == 0) {
            return;
        }
        const int top = topIndex(layout);
        if (index < top) {
            scrollOffset_ = index;
        } else if (index >= top + layout.rows_) {
            scrollOffset_ = index - layout.rows_ + 1;
        } else {
            scrollOffset_ = top;
        }
    }

    /// Height of the expanded item strip alone, excluding the header.
    int stripHeight(const StripLayout &layout) const {
        return rowHeight() * layout.rows_;
    }

    /// Offset from the widget's top edge to the top of header plus strip:
    /// zero below, the strip's negative offset above.
    int expandedOffsetY(const StripLayout &layout) const {
        return IRMath::min(0, layout.offsetY_);
    }

    /// Height of the header plus the strip, which always abut.
    int expandedHeight(int headerHeight, const StripLayout &layout) const {
        return headerHeight + stripHeight(layout);
    }

    /// Offset from the widget's top edge to the top edge of item @p itemIndex.
    /// An item above the window lands above the strip and one below it lands
    /// past the strip's bottom edge.
    int itemTopOffsetY(const StripLayout &layout, int itemIndex) const {
        return layout.offsetY_ + (itemIndex - topIndex(layout)) * rowHeight();
    }

    /// Offset from the widget's top edge to the center of item @p itemIndex.
    float itemCenterOffsetY(const StripLayout &layout, int itemIndex) const {
        return static_cast<float>(itemTopOffsetY(layout, itemIndex)) +
               0.5f * static_cast<float>(rowHeight());
    }

    /// Absolute item index at @p offsetY trixels below the widget's top edge,
    /// or -1 when that point is off the strip (the header included).
    int itemAtOffsetY(const StripLayout &layout, float offsetY) const {
        const float withinStrip = offsetY - static_cast<float>(layout.offsetY_);
        if (withinStrip < 0.0f) {
            return -1;
        }
        const int row = static_cast<int>(withinStrip / static_cast<float>(rowHeight()));
        return row < layout.rows_ ? topIndex(layout) + row : -1;
    }
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
// the generic WIDGET_INPUT pointer-focus path). Cursor
// is a logical character index 0..text.size(); blink phase is driven
// by a shared frame counter held on WIDGET_APPLY_TEXT_INPUT itself.
// `maxLength_ = 0` disables the length cap.
struct C_WidgetTextInput {
    std::string text_;
    int cursorPos_ = 0;
    int maxLength_ = 0;
};

struct C_WidgetNumberField {
    float value_ = 0.0f;
    float defaultValue_ = 0.0f;
    float minValue_ = 0.0f;
    float maxValue_ = 1.0f;
    float step_ = 0.0f;
    IRRender::TextSize textSize_ = IRRender::TextSize::LARGE;
    std::string editBuffer_;
    bool editing_ = false;

    static float clampAndSnap(float value, float minValue, float maxValue, float step) {
        const float clamped = IRMath::clamp(value, minValue, maxValue);
        if (!(step > 0.0f)) {
            return clamped;
        }
        const float snapped =
            minValue + static_cast<float>(IRMath::roundHalfUp((clamped - minValue) / step)) * step;
        return IRMath::min(snapped, maxValue);
    }

    void setValue(float value) {
        value_ = clampAndSnap(value, minValue_, maxValue_, step_);
    }

    int displayDecimals() const {
        if (!(step_ > 0.0f)) {
            return 2;
        }
        float scale = 1.0f;
        for (int decimals = 0; decimals <= 4; ++decimals) {
            const float scaledStep = step_ * scale;
            const float scaledMin = minValue_ * scale;
            if (IRMath::abs(scaledStep - static_cast<float>(IRMath::roundHalfUp(scaledStep))) <
                    1.0e-4f &&
                IRMath::abs(scaledMin - static_cast<float>(IRMath::roundHalfUp(scaledMin))) <
                    1.0e-4f) {
                return decimals;
            }
            scale *= 10.0f;
        }
        return 4;
    }

    void formatValue(std::string &out) const {
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%.*f", displayDecimals(), value_);
        out.assign(buffer);
    }

    std::string formattedValue() const {
        std::string out;
        formatValue(out);
        return out;
    }

    void updateEdit(
        bool focused,
        bool disabled,
        std::string_view typed,
        bool backspace,
        bool escape,
        int maxCharacters
    ) {
        if (focused && !editing_) {
            editBuffer_.reserve(static_cast<std::size_t>(maxCharacters));
            formatValue(editBuffer_);
            editing_ = true;
        }
        if (!editing_) {
            return;
        }
        if (disabled || escape) {
            editBuffer_.clear();
            editing_ = false;
            return;
        }
        for (const char character : typed) {
            if (static_cast<int>(editBuffer_.size()) >= maxCharacters) {
                break;
            }
            if (character >= '0' && character <= '9') {
                editBuffer_.push_back(character);
            } else if (character == '-' && editBuffer_.empty()) {
                editBuffer_.push_back(character);
            } else if (character == '.' && editBuffer_.find('.') == std::string::npos) {
                editBuffer_.push_back(character);
            }
        }
        if (backspace) {
            if (!editBuffer_.empty()) {
                editBuffer_.pop_back();
            } else {
                value_ = defaultValue_;
                formatValue(editBuffer_);
            }
        }
        if (focused) {
            return;
        }

        std::string seeded;
        formatValue(seeded);
        if (editBuffer_ != seeded) {
            float parsed = 0.0f;
            const char *begin = editBuffer_.data();
            const char *end = begin + editBuffer_.size();
            const auto result = std::from_chars(begin, end, parsed);
            if (result.ec == std::errc{} && result.ptr == end && IRMath::isFinite(parsed)) {
                setValue(parsed);
            }
        }
        editBuffer_.clear();
        editing_ = false;
    }
};

// COLOR_SWATCH — a single solid-color clickable cell. Carries its own
// RGBA so consumers (palette panels, theme editors) can populate a
// grid of distinct colors without overriding the widget theme. The
// `selected_` bit is decorative — flips an outlined border so the
// active swatch stands out within a group; consumers maintain
// mutual exclusion themselves (clearing siblings on click). Click
// signal still travels on `C_WidgetState::fireAction_` like every
// other interactive widget.
struct C_WidgetColorSwatch {
    IRMath::Color color_ = IRMath::Color{0, 0, 0, 255};
    bool selected_ = false;
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

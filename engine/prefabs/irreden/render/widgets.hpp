#ifndef IRREDEN_WIDGETS_H
#define IRREDEN_WIDGETS_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/render/image_data.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_widget_image.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_gui_hover_state.hpp>
#include <irreden/render/components/component_widget_focus_request.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>
#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/render/gui_rect.hpp>
#include <irreden/render/widget_theme.hpp>
#include <irreden/render/widget_hotkeys.hpp>
#include <irreden/render/widget_text_layout.hpp>

#include <climits>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace IRPrefab::Widget {

struct SliderOptions {
    std::optional<float> defaultValue_;
    std::optional<float> step_;
    IREntity::EntityId fieldId_ = IREntity::kNullEntity;
};

inline float clampSliderValue(const IRComponents::C_WidgetSlider &slider, float value) {
    return IRMath::clamp(value, slider.minValue_, slider.maxValue_);
}

inline float snapSliderValue(const IRComponents::C_WidgetSlider &slider, float value) {
    const float clamped = clampSliderValue(slider, value);
    const float steps = (clamped - slider.minValue_) / slider.step_;
    return clampSliderValue(
        slider,
        slider.minValue_ + static_cast<float>(IRMath::roundHalfUp(steps)) * slider.step_
    );
}

inline void setSliderValue(IRComponents::C_WidgetSlider &slider, float value) {
    slider.currentValue_ = clampSliderValue(slider, value);
}

inline void resetSliderValue(IRComponents::C_WidgetSlider &slider) {
    slider.currentValue_ = slider.defaultValue_;
}

inline void nudgeSliderValue(IRComponents::C_WidgetSlider &slider, int steps) {
    setSliderValue(slider, slider.currentValue_ + static_cast<float>(steps) * slider.step_);
}

inline IRPrefab::GuiRect sliderThumbRect(
    const IRComponents::C_Widget &widget,
    const IRComponents::C_WidgetSlider &slider,
    const IRComponents::C_GuiPosition &guiPos,
    const WidgetTheme &theme
) {
    const int labelHeight = IRPrefab::WidgetText::sliderLabelHeight(slider);
    const float range = slider.maxValue_ - slider.minValue_;
    const float fraction =
        IRMath::clamp((slider.currentValue_ - slider.minValue_) / range, 0.0f, 1.0f);
    const int thumbWidth = theme.sliderThumbWidth_;
    return {
        IRMath::ivec2(
            guiPos.pos_.x +
                static_cast<int>(fraction * static_cast<float>(widget.size_.x - thumbWidth)),
            guiPos.pos_.y + labelHeight
        ),
        IRMath::ivec2(thumbWidth, widget.size_.y - labelHeight),
    };
}

// A hit region draws nothing, so keyboard focus on one would be invisible.
inline bool isTabFocusCandidate(const IRComponents::C_Widget &widget) {
    return !widget.disabled_ && widget.kind_ != IRComponents::WidgetKind::TEXT_INPUT &&
           widget.kind_ != IRComponents::WidgetKind::NUMBER_FIELD &&
           widget.kind_ != IRComponents::WidgetKind::HIT_REGION;
}

inline bool takesKeyboardFocus(const IRComponents::C_Widget &widget) {
    return !widget.disabled_ && (widget.kind_ == IRComponents::WidgetKind::TEXT_INPUT ||
                                 widget.kind_ == IRComponents::WidgetKind::NUMBER_FIELD);
}

// The single widget WIDGET_INPUT routes hover to: among enabled widgets whose
// `C_HitBox2DGui::hovered_` is set, the highest `C_Widget::zOrder_`; on equal
// z-order the one met last in archetype iteration wins. Every
// `C_HitBox2DGui` entity must carry a `C_Widget`. kNullEntity when none.
inline IREntity::EntityId topmostHoveredWidget() {
    IREntity::EntityId topmost = IREntity::kNullEntity;
    int topZ = INT_MIN;
    IREntity::forEachComponent<IRComponents::C_HitBox2DGui>(
        [&topmost, &topZ](IREntity::EntityId &id, IRComponents::C_HitBox2DGui &hitbox) {
            if (!hitbox.hovered_) {
                return;
            }
            const auto &widget = IREntity::getComponent<IRComponents::C_Widget>(id);
            if (!widget.disabled_ && widget.zOrder_ >= topZ) {
                topZ = widget.zOrder_;
                topmost = id;
            }
        }
    );
    return topmost;
}

// Ergonomic free-function constructors for the five Phase-0 minimum
// widgets. Each call produces a single entity carrying:
//   - C_Widget       (header — kind + size + disabled flag)
//   - C_GuiPosition  (top-left position on the GUI canvas)
//   - C_GuiElement   (tag for canvas-scoped iteration)
//   - C_WidgetState  (interactive state; default-constructed)
//   - C_WidgetPanel / C_WidgetLabel / C_WidgetButton / C_WidgetSlider
//                    / C_WidgetCheckbox (kind-specific data; HIT_REGION
//                    has none)
//   - C_HitBox2DGui  (only for interactive kinds — BUTTON, SLIDER,
//                    CHECKBOX, HIT_REGION. PANEL and LABEL skip the hitbox
//                    so they do not consume mouse hover.)
//
// Z-order routing (C_Widget::zOrder_) resolves overlap only among entities
// that carry C_HitBox2DGui. PANEL and LABEL have no hitbox and therefore
// do not participate in z-order routing — a panel placed over a button
// does NOT block the button's click. To make a panel click-blocking, add
// C_HitBox2DGui manually: IREntity::setComponent(panelId, C_HitBox2DGui{panelSize});
//
// Returned `EntityId` is the widget's address; consumers use
// `wasClicked`, `sliderValue`, `checkboxState` to read interaction
// results between frames.

inline IREntity::EntityId makePanel(
    IRMath::ivec2 pos,
    IRMath::ivec2 size,
    std::string title = "",
    bool drawBorder = true,
    int zOrder = 0
) {
    IRComponents::C_Widget widget{IRComponents::WidgetKind::PANEL, size, zOrder};
    widget.coversBelow_ = true;
    return IREntity::createEntity(
        widget,
        IRComponents::C_GuiPosition{pos},
        IRComponents::C_GuiElement{},
        IRComponents::C_WidgetState{},
        IRComponents::C_WidgetPanel{std::move(title), drawBorder}
    );
}

inline IREntity::EntityId makeLabel(
    IRMath::ivec2 pos,
    std::string text,
    IRMath::Color colorOverride = IRMath::Color{0, 0, 0, 0},
    IRRender::TextSize textSize = IRRender::TextSize::LARGE
) {
    IRComponents::C_WidgetLabel labelData;
    labelData.text_ = std::move(text);
    labelData.colorOverride_ = colorOverride;
    labelData.textSize_ = textSize;
    return IREntity::createEntity(
        IRComponents::C_Widget{IRComponents::WidgetKind::LABEL, IRMath::ivec2(0, 0)},
        IRComponents::C_GuiPosition{pos},
        IRComponents::C_GuiElement{},
        std::move(labelData)
    );
}

inline IREntity::EntityId makeButton(
    IRMath::ivec2 pos,
    IRMath::ivec2 size,
    std::string label,
    IRRender::TextSize textSize = IRRender::TextSize::LARGE
) {
    IRComponents::C_WidgetButton buttonData;
    buttonData.label_ = std::move(label);
    buttonData.textSize_ = textSize;
    return IREntity::createEntity(
        IRComponents::C_Widget{IRComponents::WidgetKind::BUTTON, size},
        IRComponents::C_GuiPosition{pos},
        IRComponents::C_GuiElement{},
        IRComponents::C_WidgetState{},
        std::move(buttonData),
        IRComponents::C_HitBox2DGui{size}
    );
}

inline IREntity::EntityId makeSlider(
    IRMath::ivec2 pos,
    IRMath::ivec2 size,
    std::string label,
    float minValue,
    float maxValue,
    float initialValue,
    IRRender::TextSize textSize = IRRender::TextSize::LARGE,
    SliderOptions options = {}
) {
    IRComponents::C_WidgetSlider sliderData;
    sliderData.label_ = std::move(label);
    sliderData.minValue_ = minValue;
    sliderData.maxValue_ = maxValue;
    sliderData.currentValue_ = IRMath::clamp(initialValue, minValue, maxValue);
    sliderData.defaultValue_ =
        IRMath::clamp(options.defaultValue_.value_or(sliderData.currentValue_), minValue, maxValue);
    sliderData.step_ = options.step_.value_or((maxValue - minValue) / 100.0f);
    IR_ASSERT(
        IRMath::isFinite(sliderData.step_) && sliderData.step_ > 0.0f,
        "C_WidgetSlider step must be finite and above zero"
    );
    sliderData.fieldId_ = options.fieldId_;
    sliderData.textSize_ = textSize;
    return IREntity::createEntity(
        IRComponents::C_Widget{IRComponents::WidgetKind::SLIDER, size},
        IRComponents::C_GuiPosition{pos},
        IRComponents::C_GuiElement{},
        IRComponents::C_WidgetState{},
        std::move(sliderData),
        IRComponents::C_HitBox2DGui{size}
    );
}

// HIT_REGION — an invisible widget: hover, press and click route through
// WIDGET_INPUT exactly as for a button (z-order included), but no renderer
// draws it. Lay one over content drawn elsewhere to make that content one
// hover / click target; the caller draws its own hover feedback.
inline IREntity::EntityId makeHitRegion(IRMath::ivec2 pos, IRMath::ivec2 size, int zOrder = 0) {
    return IREntity::createEntity(
        IRComponents::C_Widget{IRComponents::WidgetKind::HIT_REGION, size, zOrder},
        IRComponents::C_GuiPosition{pos},
        IRComponents::C_GuiElement{},
        IRComponents::C_WidgetState{},
        IRComponents::C_HitBox2DGui{size}
    );
}

inline IREntity::EntityId makeCheckbox(
    IRMath::ivec2 pos,
    IRMath::ivec2 size,
    std::string label,
    bool initialChecked = false,
    IRRender::TextSize textSize = IRRender::TextSize::LARGE
) {
    IRComponents::C_WidgetCheckbox checkboxData;
    checkboxData.label_ = std::move(label);
    checkboxData.checked_ = initialChecked;
    checkboxData.textSize_ = textSize;
    return IREntity::createEntity(
        IRComponents::C_Widget{IRComponents::WidgetKind::CHECKBOX, size},
        IRComponents::C_GuiPosition{pos},
        IRComponents::C_GuiElement{},
        IRComponents::C_WidgetState{},
        std::move(checkboxData),
        IRComponents::C_HitBox2DGui{size}
    );
}

// Read-the-interaction-result helpers. These DO `getComponent` —
// allowed because they belong to creation per-frame logic,
// not from inside a system tick.
inline bool wasClicked(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetState>(widget).fireAction_;
}

inline bool isHovered(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetState>(widget).hovered_;
}

inline bool isPressed(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetState>(widget).pressed_;
}

inline float sliderValue(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetSlider>(widget).currentValue_;
}

inline bool checkboxState(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetCheckbox>(widget).checked_;
}

// WIDGET_INPUT::create() calls this, so the singleton's first-use
// createEntity never runs from a focus() made inside a system tick.
inline void ensureFocusRequestSingleton() {
    IREntity::singleton<IRComponents::C_WidgetFocusRequest>();
}

// Moves keyboard focus to `widget` at the next WIDGET_INPUT beginTick, as a
// pointer click on it would, so a key command can focus a text input without
// typing its own key into it. The latest request in a frame wins.
inline void focus(IREntity::EntityId widget) {
    IREntity::singleton<IRComponents::C_WidgetFocusRequest>().widget_ = widget;
}

// The pending focus() request, cleared by the read; kNullEntity when none.
inline IREntity::EntityId takeFocusRequest() {
    auto *request = IREntity::singletonOrNull<IRComponents::C_WidgetFocusRequest>();
    if (request == nullptr) {
        return IREntity::kNullEntity;
    }
    return std::exchange(request->widget_, IREntity::kNullEntity);
}

// Hovered-widget export. Create one C_GuiHoverState singleton per
// world with makeGuiHoverState(); WIDGET_INPUT::endTick publishes the
// z-ordered topmost hovered widget into it each frame. hoveredWidget()
// reads it back — kNullEntity when nothing is hovered or no singleton
// exists. Lets a headless GUI test assert "which widget is hovered"
// without re-scanning every hitbox.
inline IREntity::EntityId makeGuiHoverState() {
    return IREntity::createEntity(IRComponents::C_GuiHoverState{});
}

inline IREntity::EntityId hoveredWidget() {
    IREntity::EntityId result = IREntity::kNullEntity;
    IREntity::forEachComponent<IRComponents::C_GuiHoverState>(
        [&result](IREntity::EntityId &, IRComponents::C_GuiHoverState &hoverState) {
            result = hoverState.hoveredWidget_;
        }
    );
    return result;
}

inline void setSliderValue(IREntity::EntityId widget, float value) {
    auto &slider = IREntity::getComponent<IRComponents::C_WidgetSlider>(widget);
    setSliderValue(slider, value);
}

inline void setCheckboxState(IREntity::EntityId widget, bool checked) {
    IREntity::getComponent<IRComponents::C_WidgetCheckbox>(widget).checked_ = checked;
}

inline void setDisabled(IREntity::EntityId widget, bool disabled) {
    IREntity::getComponent<IRComponents::C_Widget>(widget).disabled_ = disabled;
}

// Declares (or withdraws) `widget` as a deliberate cover for the GUI layout
// audit (`C_Widget::coversBelow_`). makePanel and makeDropdown declare theirs.
inline void setCoversBelow(IREntity::EntityId widget, bool covers) {
    IREntity::getComponent<IRComponents::C_Widget>(widget).coversBelow_ = covers;
}

inline bool coversBelow(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_Widget>(widget).coversBelow_;
}

inline void setButtonLabel(IREntity::EntityId widget, std::string label) {
    IREntity::getComponent<IRComponents::C_WidgetButton>(widget).label_ = std::move(label);
}

inline void setLabelText(IREntity::EntityId widget, std::string text) {
    IREntity::getComponent<IRComponents::C_WidgetLabel>(widget).text_ = std::move(text);
}

// ---------------------------------------------------------------------------
// Compound widgets: list, dropdown, radio, text input, scroll.
// ---------------------------------------------------------------------------

inline IREntity::EntityId makeList(
    IRMath::ivec2 pos,
    IRMath::ivec2 size,
    std::vector<std::string> items,
    int initialSelected = -1,
    int itemHeight = 18
) {
    IRComponents::C_WidgetList listData;
    listData.items_ = std::move(items);
    listData.selectedIndex_ = initialSelected;
    listData.itemHeight_ = itemHeight;
    return IREntity::createEntity(
        IRComponents::C_Widget{IRComponents::WidgetKind::LIST, size},
        IRComponents::C_GuiPosition{pos},
        IRComponents::C_GuiElement{},
        IRComponents::C_WidgetState{},
        std::move(listData),
        IRComponents::C_HitBox2DGui{size}
    );
}

inline IREntity::EntityId makeDropdown(
    IRMath::ivec2 pos,
    IRMath::ivec2 size,
    std::vector<std::string> items,
    int initialSelected = 0,
    int itemHeight = 18,
    IRRender::TextSize textSize = IRRender::TextSize::LARGE
) {
    IRComponents::C_WidgetDropdown ddData;
    ddData.items_ = std::move(items);
    ddData.selectedIndex_ = initialSelected;
    ddData.itemHeight_ = itemHeight;
    ddData.textSize_ = textSize;
    IRComponents::C_Widget widget{IRComponents::WidgetKind::DROPDOWN, size};
    widget.coversBelow_ = true;
    return IREntity::createEntity(
        widget,
        IRComponents::C_GuiPosition{pos},
        IRComponents::C_GuiElement{},
        IRComponents::C_WidgetState{},
        std::move(ddData),
        IRComponents::C_HitBox2DGui{size}
    );
}

inline IREntity::EntityId makeRadio(
    IRMath::ivec2 pos,
    IRMath::ivec2 size,
    std::string label,
    std::uint32_t groupId,
    int value,
    bool initialSelected = false
) {
    IRComponents::C_WidgetRadio radioData;
    radioData.label_ = std::move(label);
    radioData.groupId_ = groupId;
    radioData.value_ = value;
    radioData.selected_ = initialSelected;
    return IREntity::createEntity(
        IRComponents::C_Widget{IRComponents::WidgetKind::RADIO, size},
        IRComponents::C_GuiPosition{pos},
        IRComponents::C_GuiElement{},
        IRComponents::C_WidgetState{},
        std::move(radioData),
        IRComponents::C_HitBox2DGui{size}
    );
}

inline IREntity::EntityId makeTextInput(
    IRMath::ivec2 pos, IRMath::ivec2 size, std::string initialText = "", int maxLength = 0
) {
    IRComponents::C_WidgetTextInput tiData;
    tiData.text_ = std::move(initialText);
    tiData.cursorPos_ = static_cast<int>(tiData.text_.size());
    tiData.maxLength_ = maxLength;
    return IREntity::createEntity(
        IRComponents::C_Widget{IRComponents::WidgetKind::TEXT_INPUT, size},
        IRComponents::C_GuiPosition{pos},
        IRComponents::C_GuiElement{},
        IRComponents::C_WidgetState{},
        std::move(tiData),
        IRComponents::C_HitBox2DGui{size}
    );
}

inline IREntity::EntityId makeNumberField(
    IRMath::ivec2 pos,
    IRMath::ivec2 size,
    float value,
    float minValue,
    float maxValue,
    float step = 0.0f,
    IRRender::TextSize textSize = IRRender::TextSize::LARGE
) {
    IRComponents::C_WidgetNumberField field;
    field.minValue_ = minValue;
    field.maxValue_ = maxValue;
    field.step_ = step;
    field.textSize_ = textSize;
    field.setValue(value);
    field.defaultValue_ = field.value_;
    return IREntity::createEntity(
        IRComponents::C_Widget{IRComponents::WidgetKind::NUMBER_FIELD, size},
        IRComponents::C_GuiPosition{pos},
        IRComponents::C_GuiElement{},
        IRComponents::C_WidgetState{},
        std::move(field),
        IRComponents::C_HitBox2DGui{size}
    );
}

inline IREntity::EntityId makeScroll(
    IRMath::ivec2 pos,
    IRMath::ivec2 size,
    int contentSize,
    IRComponents::C_WidgetScroll::Axis axis = IRComponents::C_WidgetScroll::Axis::VERTICAL,
    int initialScroll = 0
) {
    IRComponents::C_WidgetScroll scrollData;
    scrollData.axis_ = axis;
    scrollData.contentSize_ = contentSize;
    scrollData.scrollPos_ = initialScroll;
    return IREntity::createEntity(
        IRComponents::C_Widget{IRComponents::WidgetKind::SCROLL, size},
        IRComponents::C_GuiPosition{pos},
        IRComponents::C_GuiElement{},
        IRComponents::C_WidgetState{},
        std::move(scrollData),
        IRComponents::C_HitBox2DGui{size}
    );
}

// Readers and mutators for compound widgets. This getComponent use belongs to
// creation per-frame logic, not a system tick.
inline int listSelectedIndex(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetList>(widget).selectedIndex_;
}

// Scrolls the newly selected row into view when, and only when, the call
// changes the selection. A caller that mirrors its own state into the list
// every frame therefore never fights the mouse wheel over the scroll offset.
inline void setListSelectedIndex(IREntity::EntityId widget, int index) {
    auto &list = IREntity::getComponent<IRComponents::C_WidgetList>(widget);
    const int n = static_cast<int>(list.items_.size());
    const int selected = (n == 0) ? -1 : IRMath::clamp(index, -1, n - 1);
    if (selected == list.selectedIndex_) {
        return;
    }
    list.selectedIndex_ = selected;
    list.scrollIntoView(selected, IREntity::getComponent<IRComponents::C_Widget>(widget).size_.y);
}

inline int dropdownSelectedIndex(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetDropdown>(widget).selectedIndex_;
}

inline bool isDropdownOpen(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetDropdown>(widget).isOpen_;
}

// A negative index clears the selection, as for setListSelectedIndex.
inline void setDropdownSelectedIndex(IREntity::EntityId widget, int index) {
    auto &dd = IREntity::getComponent<IRComponents::C_WidgetDropdown>(widget);
    const int n = static_cast<int>(dd.items_.size());
    dd.selectedIndex_ = (n == 0) ? -1 : IRMath::clamp(index, -1, n - 1);
}

inline void
setDropdownItems(IREntity::EntityId widget, std::vector<std::string> items, int selectedIndex = 0) {
    IREntity::getComponent<IRComponents::C_WidgetDropdown>(widget).items_ = std::move(items);
    setDropdownSelectedIndex(widget, selectedIndex);
}

inline bool radioSelected(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetRadio>(widget).selected_;
}

inline int radioValue(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetRadio>(widget).value_;
}

inline const std::string &textInputValue(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetTextInput>(widget).text_;
}

inline void setTextInputValue(IREntity::EntityId widget, std::string text) {
    auto &ti = IREntity::getComponent<IRComponents::C_WidgetTextInput>(widget);
    ti.text_ = std::move(text);
    ti.cursorPos_ = static_cast<int>(ti.text_.size());
}

inline float numberFieldValue(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetNumberField>(widget).value_;
}

inline void setNumberFieldValue(IREntity::EntityId widget, float value) {
    IREntity::getComponent<IRComponents::C_WidgetNumberField>(widget).setValue(value);
}

inline int scrollPosition(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetScroll>(widget).scrollPos_;
}

inline void setScrollPosition(IREntity::EntityId widget, int pos) {
    auto &sc = IREntity::getComponent<IRComponents::C_WidgetScroll>(widget);
    sc.scrollPos_ = IRMath::max(0, pos);
}

// COLOR_SWATCH — clickable solid-color cell, primarily for palette
// panels. Each swatch carries its own RGBA so a grid of distinct
// colors can be built without theme overrides. Consumers maintain
// "active swatch" mutual exclusion themselves (clear siblings when
// one fires) — the widget framework does not group color swatches.
inline IREntity::EntityId makeColorSwatch(
    IRMath::ivec2 pos, IRMath::ivec2 size, IRMath::Color color, bool initialSelected = false
) {
    IRComponents::C_WidgetColorSwatch swatchData;
    swatchData.color_ = color;
    swatchData.selected_ = initialSelected;
    return IREntity::createEntity(
        IRComponents::C_Widget{IRComponents::WidgetKind::COLOR_SWATCH, size},
        IRComponents::C_GuiPosition{pos},
        IRComponents::C_GuiElement{},
        IRComponents::C_WidgetState{},
        std::move(swatchData),
        IRComponents::C_HitBox2DGui{size}
    );
}

inline IRMath::Color colorSwatchColor(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetColorSwatch>(widget).color_;
}

inline void setColorSwatchColor(IREntity::EntityId widget, IRMath::Color color) {
    IREntity::getComponent<IRComponents::C_WidgetColorSwatch>(widget).color_ = color;
}

inline bool colorSwatchSelected(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetColorSwatch>(widget).selected_;
}

inline void setColorSwatchSelected(IREntity::EntityId widget, bool selected) {
    IREntity::getComponent<IRComponents::C_WidgetColorSwatch>(widget).selected_ = selected;
}

// IMAGE — a PNG drawn into the widget's trixel rect by WIDGET_RENDER_IMAGE.
// The file is decoded and box-filtered here, on the calling thread, and again
// only when `setImagePath` / `setImageSize` change its inputs; the component
// keeps the path and the texels, never the decoded image. A missing or
// undecodable file draws a diagonal hatch in the theme's disabled colours and
// `imageError` names the path. The path is trusted: a caller taking it from
// script resolves it inside a base first (`IRGui.makeImage` does). A relative
// path resolves from the working directory.
namespace detail {

// One hatch line every period texels along a row, two texels wide: on the
// parity-aligned origin WIDGET_RENDER_IMAGE draws from, each line is a chain
// of lattice diamonds stepping one column per row, a straight diagonal.
constexpr int kImageHatchPeriod = 8;
constexpr int kImageHatchWidth = 2;

// Decodes the PNG at @p path and box-filters it into @p texelSize texels over
// @p background. False, with @p error naming the path, when it cannot decode.
using ImageDecoder = bool (*)(
    const std::string &path,
    IRMath::ivec2 texelSize,
    IRMath::Color background,
    std::vector<IRMath::Color> &texels,
    std::string &error
);

inline bool decodeImageTexels(
    const std::string &path,
    IRMath::ivec2 texelSize,
    IRMath::Color background,
    std::vector<IRMath::Color> &texels,
    std::string &error
) {
    const IRRender::ImageData decoded{path.c_str(), error};
    if (decoded.data_ == nullptr) {
        return false;
    }
    texels = IRRender::boxFilterToTrixels(
        decoded.data_,
        IRMath::ivec2(decoded.width_, decoded.height_),
        texelSize,
        background
    );
    return true;
}

// The decoder every image load goes through. Tests swap in a stub that
// records each call, so a refused path is proven never to reach the file.
inline ImageDecoder &imageDecoder() {
    static ImageDecoder decoder = &decodeImageTexels;
    return decoder;
}

inline void loadImageTexels(IRComponents::C_WidgetImage &image, IRMath::ivec2 size) {
    const WidgetTheme &theme = defaultTheme();
    image.texelSize_ = IRMath::max(size, IRMath::ivec2(0));
    image.error_.clear();
    image.loaded_ = imageDecoder()(
        image.path_,
        image.texelSize_,
        theme.backgroundIdle_,
        image.texels_,
        image.error_
    );
    if (image.loaded_) {
        return;
    }
    IRE_LOG_WARN("IRPrefab::Widget::makeImage: {}", image.error_);
    image.texels_.assign(
        static_cast<std::size_t>(image.texelSize_.x) * image.texelSize_.y,
        theme.backgroundDisabled_
    );
    for (int y = 0; y < image.texelSize_.y; ++y) {
        for (int x = 0; x < image.texelSize_.x; ++x) {
            if ((x + y) % kImageHatchPeriod < kImageHatchWidth) {
                image.texels_[static_cast<std::size_t>(y) * image.texelSize_.x + x] =
                    theme.textDisabled_;
            }
        }
    }
}

} // namespace detail

inline IREntity::EntityId makeImage(IRMath::ivec2 pos, IRMath::ivec2 size, std::string path) {
    IRComponents::C_WidgetImage imageData;
    imageData.path_ = std::move(path);
    detail::loadImageTexels(imageData, size);
    return IREntity::createEntity(
        IRComponents::C_Widget{IRComponents::WidgetKind::IMAGE, size},
        IRComponents::C_GuiPosition{pos},
        IRComponents::C_GuiElement{},
        std::move(imageData)
    );
}

inline void setImagePath(IREntity::EntityId widget, std::string path) {
    auto &image = IREntity::getComponent<IRComponents::C_WidgetImage>(widget);
    image.path_ = std::move(path);
    detail::loadImageTexels(image, IREntity::getComponent<IRComponents::C_Widget>(widget).size_);
}

inline void setImageSize(IREntity::EntityId widget, IRMath::ivec2 size) {
    IREntity::getComponent<IRComponents::C_Widget>(widget).size_ = size;
    detail::loadImageTexels(IREntity::getComponent<IRComponents::C_WidgetImage>(widget), size);
}

// Empty when the image decoded; otherwise the load failure, naming the path.
inline const std::string &imageError(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetImage>(widget).error_;
}

inline bool imageLoaded(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetImage>(widget).loaded_;
}

} // namespace IRPrefab::Widget

#endif /* IRREDEN_WIDGETS_H */

#ifndef IRREDEN_WIDGETS_H
#define IRREDEN_WIDGETS_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_gui_hover_state.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>
#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/render/widget_theme.hpp>
#include <irreden/render/widget_hotkeys.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace IRPrefab::Widget {

// Ergonomic free-function constructors for the five Phase-0 minimum
// widgets. Each call produces a single entity carrying:
//   - C_Widget       (header — kind + size + disabled flag)
//   - C_GuiPosition  (top-left position on the GUI canvas)
//   - C_GuiElement   (tag for canvas-scoped iteration)
//   - C_WidgetState  (interactive state; default-constructed)
//   - C_WidgetPanel / C_WidgetLabel / C_WidgetButton / C_WidgetSlider
//                    / C_WidgetCheckbox (kind-specific data)
//   - C_HitBox2DGui  (only for interactive kinds — BUTTON, SLIDER,
//                    CHECKBOX. PANEL and LABEL skip the hitbox so they
//                    do not consume mouse hover.)
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
    return IREntity::createEntity(
        IRComponents::C_Widget{IRComponents::WidgetKind::PANEL, size, zOrder},
        IRComponents::C_GuiPosition{pos},
        IRComponents::C_GuiElement{},
        IRComponents::C_WidgetState{},
        IRComponents::C_WidgetPanel{std::move(title), drawBorder}
    );
}

inline IREntity::EntityId makeLabel(
    IRMath::ivec2 pos, std::string text, IRMath::Color colorOverride = IRMath::Color{0, 0, 0, 0}
) {
    IRComponents::C_WidgetLabel labelData;
    labelData.text_ = std::move(text);
    labelData.colorOverride_ = colorOverride;
    return IREntity::createEntity(
        IRComponents::C_Widget{IRComponents::WidgetKind::LABEL, IRMath::ivec2(0, 0)},
        IRComponents::C_GuiPosition{pos},
        IRComponents::C_GuiElement{},
        std::move(labelData)
    );
}

inline IREntity::EntityId makeButton(IRMath::ivec2 pos, IRMath::ivec2 size, std::string label) {
    IRComponents::C_WidgetButton buttonData;
    buttonData.label_ = std::move(label);
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
    float initialValue
) {
    IRComponents::C_WidgetSlider sliderData;
    sliderData.label_ = std::move(label);
    sliderData.minValue_ = minValue;
    sliderData.maxValue_ = maxValue;
    sliderData.currentValue_ = initialValue;
    return IREntity::createEntity(
        IRComponents::C_Widget{IRComponents::WidgetKind::SLIDER, size},
        IRComponents::C_GuiPosition{pos},
        IRComponents::C_GuiElement{},
        IRComponents::C_WidgetState{},
        std::move(sliderData),
        IRComponents::C_HitBox2DGui{size}
    );
}

inline IREntity::EntityId makeCheckbox(
    IRMath::ivec2 pos, IRMath::ivec2 size, std::string label, bool initialChecked = false
) {
    IRComponents::C_WidgetCheckbox checkboxData;
    checkboxData.label_ = std::move(label);
    checkboxData.checked_ = initialChecked;
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
// allowed because they're called from a creation's per-frame logic,
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

// Hovered-widget export (#1796). Create one C_GuiHoverState singleton per
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
    slider.currentValue_ = IRMath::clamp(value, slider.minValue_, slider.maxValue_);
}

inline void setCheckboxState(IREntity::EntityId widget, bool checked) {
    IREntity::getComponent<IRComponents::C_WidgetCheckbox>(widget).checked_ = checked;
}

inline void setDisabled(IREntity::EntityId widget, bool disabled) {
    IREntity::getComponent<IRComponents::C_Widget>(widget).disabled_ = disabled;
}

inline void setButtonLabel(IREntity::EntityId widget, std::string label) {
    IREntity::getComponent<IRComponents::C_WidgetButton>(widget).label_ = std::move(label);
}

inline void setLabelText(IREntity::EntityId widget, std::string text) {
    IREntity::getComponent<IRComponents::C_WidgetLabel>(widget).text_ = std::move(text);
}

// ---------------------------------------------------------------------------
// T-177 follow-up widgets: list, dropdown, radio, text input, scroll.
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
    int itemHeight = 18
) {
    IRComponents::C_WidgetDropdown ddData;
    ddData.items_ = std::move(items);
    ddData.selectedIndex_ = initialSelected;
    ddData.itemHeight_ = itemHeight;
    return IREntity::createEntity(
        IRComponents::C_Widget{IRComponents::WidgetKind::DROPDOWN, size},
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

// Readers / mutators for the follow-up widgets. Same getComponent
// allowance as the Phase 0 readers — called from a creation's
// per-frame logic, not from a system tick.
inline int listSelectedIndex(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetList>(widget).selectedIndex_;
}

inline void setListSelectedIndex(IREntity::EntityId widget, int index) {
    auto &list = IREntity::getComponent<IRComponents::C_WidgetList>(widget);
    const int n = static_cast<int>(list.items_.size());
    list.selectedIndex_ = (n == 0) ? -1 : IRMath::clamp(index, -1, n - 1);
}

inline int dropdownSelectedIndex(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetDropdown>(widget).selectedIndex_;
}

inline bool isDropdownOpen(IREntity::EntityId widget) {
    return IREntity::getComponent<IRComponents::C_WidgetDropdown>(widget).isOpen_;
}

inline void setDropdownSelectedIndex(IREntity::EntityId widget, int index) {
    auto &dd = IREntity::getComponent<IRComponents::C_WidgetDropdown>(widget);
    const int n = static_cast<int>(dd.items_.size());
    dd.selectedIndex_ = (n == 0) ? -1 : IRMath::clamp(index, 0, n - 1);
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

} // namespace IRPrefab::Widget

#endif /* IRREDEN_WIDGETS_H */

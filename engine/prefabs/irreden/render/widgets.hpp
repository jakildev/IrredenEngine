#ifndef IRREDEN_WIDGETS_H
#define IRREDEN_WIDGETS_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>
#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/render/widget_theme.hpp>
#include <irreden/render/widget_hotkeys.hpp>

#include <string>

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
// Returned `EntityId` is the widget's address; consumers use
// `wasClicked`, `sliderValue`, `checkboxState` to read interaction
// results between frames.

inline IREntity::EntityId
makePanel(IRMath::ivec2 pos, IRMath::ivec2 size, std::string title = "", bool drawBorder = true) {
    return IREntity::createEntity(
        IRComponents::C_Widget{IRComponents::WidgetKind::PANEL, size},
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

} // namespace IRPrefab::Widget

#endif /* IRREDEN_WIDGETS_H */

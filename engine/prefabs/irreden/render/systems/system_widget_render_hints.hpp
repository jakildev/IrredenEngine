#ifndef SYSTEM_WIDGET_RENDER_HINTS_H
#define SYSTEM_WIDGET_RENDER_HINTS_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_widget_hotkey.hpp>
#include <irreden/render/components/component_widget_input_frame.hpp>
#include <irreden/render/gui_text_batch.hpp>
#include <irreden/render/trixel_rect.hpp>
#include <irreden/render/widget_theme.hpp>
#include <irreden/render/widgets.hpp>

namespace IRSystem {

template <> struct System<WIDGET_RENDER_HINTS> {
    IRComponents::C_TriangleCanvasTextures *canvas_ = nullptr;
    IRPrefab::Widget::WidgetTheme theme_;
    IRRender::RectFillScratch scratch_;
    std::vector<IRRender::GlyphDrawCommand> textCommands_;
    std::size_t lastHintGlyphCommandCount_ = 0;
    IREntity::EntityId lastFocusRingWidget_ = IREntity::kNullEntity;
    bool hintsVisible_ = false;

    void beginTick() {
        const IREntity::EntityId canvas = IRRender::getCanvas("gui");
        canvas_ = &IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(canvas);
        theme_ = IRPrefab::Widget::defaultTheme();
        lastHintGlyphCommandCount_ = 0;
        lastFocusRingWidget_ = IREntity::kNullEntity;
        const auto *frame = IREntity::singletonOrNull<IRComponents::C_WidgetInputFrame>();
        hintsVisible_ = frame != nullptr && frame->hintsVisible_;
        if (frame == nullptr || !frame->focusVisible_ ||
            !IREntity::entityExists(frame->focusedWidget_)) {
            return;
        }
        const auto widget =
            IREntity::getComponentOptional<IRComponents::C_Widget>(frame->focusedWidget_);
        const auto position =
            IREntity::getComponentOptional<IRComponents::C_GuiPosition>(frame->focusedWidget_);
        if (!widget || !position) {
            return;
        }
        IRRender::drawBorder(
            *canvas_,
            (*position)->pos_ - IRMath::ivec2(1),
            (*widget)->size_ + IRMath::ivec2(2),
            theme_.focusRing_,
            IRRender::kWidgetBorderDistance,
            1,
            scratch_
        );
        lastFocusRingWidget_ = frame->focusedWidget_;
    }

    void tick(
        const IRComponents::C_Widget &widget,
        const IRComponents::C_WidgetState &state,
        const IRComponents::C_WidgetHotkey &hotkey,
        const IRComponents::C_GuiPosition &position
    ) {
        if (canvas_ == nullptr || widget.disabled_) {
            return;
        }
        if (!hintsVisible_ || hotkey.hint_.empty()) {
            return;
        }
        const IRMath::ivec2 badgeSize(
            IRPrefab::GuiText::textRunWidth(hotkey.hint_, 1, IRRender::TextSize::SMALL) + 4,
            IRPrefab::GuiText::glyphHeight(1, IRRender::TextSize::SMALL) + 2
        );
        IRRender::fillRect(
            *canvas_,
            position.pos_,
            badgeSize,
            state.focused_ ? theme_.hintFocusedBackground_ : theme_.hintBackground_,
            IRRender::kWidgetBorderDistance,
            scratch_
        );
        IRPrefab::GuiText::queueGuiText(
            textCommands_,
            hotkey.hint_,
            position.pos_ + IRMath::ivec2(2, 1),
            canvas_->size_,
            theme_.hintText_,
            1,
            IRComponents::TextAlignH::LEFT,
            IRComponents::TextAlignV::TOP,
            0,
            0,
            0,
            IRRender::TextSize::SMALL
        );
    }

    void endTick() {
        lastHintGlyphCommandCount_ = textCommands_.size();
        IRPrefab::GuiText::dispatchGuiText(textCommands_);
    }

    static SystemId create() {
        IRPrefab::Widget::ensureThemeSingleton();
        return registerSystem<
            WIDGET_RENDER_HINTS,
            IRComponents::C_Widget,
            IRComponents::C_WidgetState,
            IRComponents::C_WidgetHotkey,
            IRComponents::C_GuiPosition,
            AlsoWrites<IRComponents::C_TriangleCanvasTextures>>("WidgetRenderHints");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_RENDER_HINTS_H */

#ifndef SYSTEM_WIDGET_RENDER_IMAGE_H
#define SYSTEM_WIDGET_RENDER_IMAGE_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_widget_image.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/trixel_rect.hpp>
#include <irreden/render/trixel_text.hpp>

namespace IRSystem {

// Writes each image widget's resampled texels onto the GUI canvas. The texel
// array is built off the tick (IRPrefab::Widget::makeImage / setImagePath /
// setImageSize); this system only copies it. The origin is parity-aligned like
// text, so the placeholder hatch reads as diagonals; the copy is clipped to
// the widget rect, so an array larger than a resized rect never overdraws.
// Sits after WIDGET_RENDER_PANEL (an image lies on a panel) and before
// WIDGET_RENDER_LABEL (a caption lies on the image).
template <> struct System<WIDGET_RENDER_IMAGE> {
    IRComponents::C_TriangleCanvasTextures *canvas_ = nullptr;
    IRRender::RectFillScratch scratch_;

    void beginTick() {
        IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
        canvas_ = &IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);
    }

    void tick(
        const IRComponents::C_Widget &widget,
        const IRComponents::C_WidgetImage &image,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        if (!canvas_ || image.texels_.empty())
            return;

        const IRMath::ivec2 origin = IRRender::parityAlignedPosition(guiPos.pos_, canvas_->size_);
        const IRMath::ivec2 size =
            IRMath::min(image.texelSize_, guiPos.pos_ + widget.size_ - origin);
        if (size.x <= 0 || size.y <= 0)
            return;
        IRRender::blitRect(
            *canvas_,
            origin,
            size,
            image.texels_.data(),
            image.texelSize_.x,
            IRRender::kWidgetBackgroundDistance,
            scratch_
        );
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_RENDER_IMAGE,
            IRComponents::C_Widget,
            IRComponents::C_WidgetImage,
            IRComponents::C_GuiPosition,
            AlsoWrites<IRComponents::C_TriangleCanvasTextures>>("WidgetRenderImage");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_RENDER_IMAGE_H */

#ifndef SYSTEM_CANVAS_DRAW_H
#define SYSTEM_CANVAS_DRAW_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/canvas_draw.hpp>
#include <irreden/render/components/component_canvas_draw_layer.hpp>
#include <irreden/render/components/component_canvas_draw_queue.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gui_text_batch.hpp>

#include <cstddef>
#include <span>

namespace IRSystem {

// Clears every `C_CanvasDrawLayer` canvas, then rasterizes the frame's queued
// canvas draws (`IRPrefab::CanvasDraw`) batch by batch onto their canvases
// with the text program and command buffer `TEXT_TO_TRIXEL` created; without
// those resources the queue is dropped unrasterized.
//
// Pipeline placement: RENDER after TEXT_TO_TRIXEL and after every other
// producer of a target canvas (`VOXEL_TO_TRIXEL_STAGE_1` clears the
// background canvas, the widget renderers paint the GUI canvas), before
// TRIXEL_GLITCH / TRIXEL_TO_FRAMEBUFFER. A draw may be queued anywhere in the
// frame before this system runs.
template <> struct System<CANVAS_DRAW> {
    IRComponents::C_CanvasDrawQueue *queue_ = nullptr;

    void beginTick() {
        queue_ = &IRPrefab::CanvasDraw::queue();
    }

    void tick(const IRComponents::C_CanvasDrawLayer &, IRComponents::C_TriangleCanvasTextures &layer) {
        layer.clear();
    }

    void endTick() {
        auto *program =
            IRRender::getNamedResourceOrNull<IRRender::ShaderProgram>("TextToTrixelProgram");
        auto *cmdBuf = IRRender::getNamedResourceOrNull<IRRender::Buffer>("GlyphDrawCommandBuffer");
        for (IRComponents::C_CanvasDrawQueue::Batch &batch : queue_->batches_) {
            if (batch.commands_.empty()) {
                continue;
            }
            if (program != nullptr && cmdBuf != nullptr) {
                auto &textures =
                    IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(batch.canvas_);
                IRPrefab::GuiText::stampGlyphLayout(batch.commands_, textures);
                program->use();
                textures.getTextureColorsForGeometryWrite()->bindAsImage(
                    0,
                    IRRender::TextureAccess::WRITE_ONLY,
                    IRRender::TextureFormat::RGBA8
                );
                textures.getTextureDistances()->bindAsImage(
                    1,
                    IRRender::TextureAccess::WRITE_ONLY,
                    IRRender::TextureFormat::R32I
                );
                // Each group is one dispatch; `dispatchGlyphCommands` fences
                // the image writes after it, so a later group paints over.
                const std::span<const IRComponents::C_CanvasDrawQueue::Batch::Primitive> primitives(
                    batch.primitives_
                );
                const std::span<const IRRender::GlyphDrawCommand> commands(batch.commands_);
                std::size_t begin = 0;
                while (begin < primitives.size()) {
                    const std::size_t end = IRPrefab::CanvasDraw::overlapGroupEnd(primitives, begin);
                    const std::size_t firstCommand = begin == 0 ? 0 : primitives[begin - 1].end_;
                    IRPrefab::GuiText::dispatchGlyphCommands(
                        *cmdBuf, commands.subspan(firstCommand, primitives[end - 1].end_ - firstCommand)
                    );
                    begin = end;
                }
            }
            batch.commands_.clear();
            batch.primitives_.clear();
        }
    }

    static SystemId create() {
        IRPrefab::CanvasDraw::ensureQueueSingleton();
        const SystemId systemId = registerSystem<
            CANVAS_DRAW,
            IRComponents::C_CanvasDrawLayer,
            IRComponents::C_TriangleCanvasTextures>("CanvasDraw");
        IRRender::tagGpuStage(systemId, "canvasDraw");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_CANVAS_DRAW_H */

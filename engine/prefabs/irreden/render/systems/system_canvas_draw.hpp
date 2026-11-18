#ifndef SYSTEM_CANVAS_DRAW_H
#define SYSTEM_CANVAS_DRAW_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/canvas_draw.hpp>
#include <irreden/render/components/component_canvas_draw_images.hpp>
#include <irreden/render/components/component_canvas_draw_layer.hpp>
#include <irreden/render/components/component_canvas_draw_queue.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gui_text_batch.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/shader.hpp>
#include <irreden/render/shader_names.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace IRSystem {

// Clears every `C_CanvasDrawLayer` canvas, then rasterizes the frame's queued
// canvas draws (`IRPrefab::CanvasDraw`) batch by batch onto their canvases
// with the text program and command buffer `TEXT_TO_TRIXEL` created; without
// those resources the queue is dropped unrasterized. An image draw runs this
// system's own kernel, which reads its one command from the head of that same
// command buffer: every buffer slot is taken, and each dispatch re-uploads the
// head anyway.
//
// Pipeline placement: RENDER after TEXT_TO_TRIXEL and after every other
// producer of a target canvas (`VOXEL_TO_TRIXEL_STAGE_1` clears the
// background canvas, the widget renderers paint the GUI canvas), before
// TRIXEL_GLITCH / TRIXEL_TO_FRAMEBUFFER. A draw may be queued anywhere in the
// frame before this system runs.
template <> struct System<CANVAS_DRAW> {
    IRComponents::C_CanvasDrawQueue *queue_ = nullptr;
    IRComponents::C_CanvasDrawImages *images_ = nullptr;
    IRRender::ShaderProgram *imageProgram_ = nullptr;

    void beginTick() {
        queue_ = &IRPrefab::CanvasDraw::queue();
        images_ = &IRPrefab::CanvasDraw::images();
    }

    void
    tick(const IRComponents::C_CanvasDrawLayer &, IRComponents::C_TriangleCanvasTextures &layer) {
        layer.clear();
    }

    void endTick() {
        auto *program =
            IRRender::getNamedResourceOrNull<IRRender::ShaderProgram>("TextToTrixelProgram");
        auto *cmdBuf = IRRender::getNamedResourceOrNull<IRRender::Buffer>("GlyphDrawCommandBuffer");
        for (IRComponents::C_CanvasDrawQueue::Batch &batch : queue_->batches_) {
            if (batch.primitives_.empty()) {
                continue;
            }
            if (program != nullptr && cmdBuf != nullptr) {
                auto &textures =
                    IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(batch.canvas_);
                IRPrefab::GuiText::stampGlyphLayout(batch.commands_, textures);
                // A group's draws are disjoint: its glyphs share one dispatch
                // and each image takes one. Every dispatch fences the image
                // writes after it, so a later group paints over.
                const std::span<const IRComponents::C_CanvasDrawQueue::Batch::Primitive> primitives(
                    batch.primitives_
                );
                const std::span<const IRRender::GlyphDrawCommand> commands(batch.commands_);
                const IRRender::ShaderProgram *bound = nullptr;
                const auto bind = [&bound, &textures](IRRender::ShaderProgram &next) {
                    if (bound != &next) {
                        bindCanvas(next, textures);
                        bound = &next;
                    }
                };
                std::size_t begin = 0;
                while (begin < primitives.size()) {
                    const std::size_t end =
                        IRPrefab::CanvasDraw::overlapGroupEnd(primitives, begin);
                    const std::size_t firstCommand = begin == 0 ? 0 : primitives[begin - 1].end_;
                    const std::span<const IRRender::GlyphDrawCommand> glyphs =
                        commands.subspan(firstCommand, primitives[end - 1].end_ - firstCommand);
                    if (!glyphs.empty()) {
                        bind(*program);
                        IRPrefab::GuiText::dispatchGlyphCommands(*cmdBuf, glyphs);
                    }
                    for (std::size_t i = begin; i < end; ++i) {
                        const int image = primitives[i].image_;
                        if (image != IRComponents::C_CanvasDrawQueue::Batch::kNoImage) {
                            bind(*imageProgram_);
                            dispatchImage(batch.images_[static_cast<std::size_t>(image)], *cmdBuf);
                        }
                    }
                    begin = end;
                }
            }
            batch.commands_.clear();
            batch.images_.clear();
            batch.primitives_.clear();
        }
    }

    static void
    bindCanvas(IRRender::ShaderProgram &program, IRComponents::C_TriangleCanvasTextures &to) {
        program.use();
        to.getTextureColorsForGeometryWrite()
            ->bindAsImage(0, IRRender::TextureAccess::WRITE_ONLY, IRRender::TextureFormat::RGBA8);
        to.bindDistancesForWrite(
            1,
            IRRender::TextureAccess::WRITE_ONLY,
            IRComponents::CanvasDistanceContent::QUERIED
        );
    }

    // One dispatch over the draw's clipped box, with the image program and
    // the target canvas bound. An image released since the draw was queued is
    // skipped.
    void dispatchImage(
        const IRComponents::C_CanvasDrawQueue::Batch::ImageDraw &draw,
        const IRRender::Buffer &cmdBuf
    ) const {
        const IRComponents::C_CanvasDrawImages::Image *image =
            IRPrefab::CanvasDraw::detail::findImage(*images_, draw.handle_);
        if (image == nullptr || image->texture_.second == nullptr) {
            return;
        }
        image->texture_.second->bindAsImage(
            IRRender::kCanvasImageAtlasUnit,
            IRRender::TextureAccess::READ_ONLY,
            IRRender::TextureFormat::RGBA8
        );
        cmdBuf.subData(0, sizeof(IRRender::CanvasImageCommand), &draw.command_);
        const IRMath::ivec2 extent = draw.command_.clipHi_ - draw.command_.clipLo_;
        IRRender::device()->dispatchCompute(
            static_cast<std::uint32_t>(IRMath::divCeil(extent.x, IRRender::kCanvasImageGroupSize)),
            static_cast<std::uint32_t>(IRMath::divCeil(extent.y, IRRender::kCanvasImageGroupSize)),
            1
        );
        IRRender::device()->memoryBarrier(IRRender::BarrierType::ALL);
    }

    static SystemId create() {
        IRPrefab::CanvasDraw::ensureQueueSingleton();
        IRPrefab::CanvasDraw::ensureImagesSingleton();
        IRRender::createNamedResource<IRRender::ShaderProgram>(
            "CanvasImageToTrixelProgram",
            std::vector{IRRender::ShaderStage{
                IRRender::kFileCompCanvasImageToTrixel,
                IRRender::ShaderType::COMPUTE
            }}
        );
        const SystemId systemId = registerSystem<
            CANVAS_DRAW,
            IRComponents::C_CanvasDrawLayer,
            IRComponents::C_TriangleCanvasTextures>("CanvasDraw");
        getSystemParams<System<CANVAS_DRAW>>(systemId)->imageProgram_ =
            IRRender::getNamedResource<IRRender::ShaderProgram>("CanvasImageToTrixelProgram");
        IRRender::tagGpuStage(systemId, "canvasDraw");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_CANVAS_DRAW_H */

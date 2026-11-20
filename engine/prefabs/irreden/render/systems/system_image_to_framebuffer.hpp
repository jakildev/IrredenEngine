#ifndef SYSTEM_IMAGE_TO_FRAMEBUFFER_H
#define SYSTEM_IMAGE_TO_FRAMEBUFFER_H

// IMAGE_TO_FRAMEBUFFER — RENDER pipeline, after TRIXEL_TO_FRAMEBUFFER (which
// clears the framebuffer and composites the canvases) and before every pass
// that reads or overlays the finished main framebuffer:
// VIEWPORT_TO_FRAMEBUFFER, the framebuffer debug overlays, FRAMEBUFFER_GLITCH
// and FRAMEBUFFER_TO_SCREEN. ENTITY_CANVAS_TO_FRAMEBUFFER may sit on either
// side: an image never ties a composited depth.
//
// Draws the frame's queued framebuffer images
// (`IRPrefab::CanvasDraw::queueFramebufferImage`) as depth-tested quads, one
// fragment per framebuffer pixel, each writing the depth the composite gives a
// canvas texel of its distance plus half an encode step. A turned image's quad
// spans its bounds and the fragments outside the turned box are discarded. It
// reports no hovered entity, so a pick goes to whatever the image covers.
// Depth test and depth write are on when it returns, the state every
// framebuffer pass rests in; it never touches blending.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_name.hpp>
#include <irreden/render/canvas_draw.hpp>
#include <irreden/render/components/component_canvas_draw_images.hpp>
#include <irreden/render/components/component_framebuffer_image_queue.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/shader.hpp>
#include <irreden/render/shader_names.hpp>
#include <irreden/render/shapes_2d.hpp>

#include <cstddef>
#include <vector>

namespace IRSystem {

// std140 twin of `FrameDataImageToFramebuffer` in `v_image_to_framebuffer.glsl`,
// `f_image_to_framebuffer.glsl` and `image_to_framebuffer.metal`.
struct FrameDataImageToFramebuffer {
    IRMath::ivec2 targetSize_ = IRMath::ivec2(1);
    IRMath::ivec2 padding_ = IRMath::ivec2(0);
    IRRender::CanvasImageCommand command_;
};
static_assert(offsetof(FrameDataImageToFramebuffer, command_) == 16);
static_assert(sizeof(FrameDataImageToFramebuffer) == 112);

template <> struct System<IMAGE_TO_FRAMEBUFFER> {
    IRRender::Buffer *frameDataBuf_ = nullptr;
    IRRender::ShaderProgram *program_ = nullptr;
    IRRender::VAO *quadVao_ = nullptr;
    IRComponents::C_FramebufferImageQueue *queue_ = nullptr;
    IRComponents::C_CanvasDrawImages *images_ = nullptr;
    FrameDataImageToFramebuffer frameData_{};

    void beginTick() {
        queue_ = &IRPrefab::CanvasDraw::framebufferImageQueue();
        images_ = &IRPrefab::CanvasDraw::images();
    }

    void tick(
        const IRComponents::C_TrixelCanvasFramebuffer &framebuffer, const IRComponents::C_Name &name
    ) {
        if (queue_->draws_.empty() || name.name_ != "mainFramebuffer") {
            return;
        }
        IR_PROFILE_SCOPE("imageToFb");
        frameData_.targetSize_ = framebuffer.getResolutionPlusBuffer();
        bool passBound = false;
        // The depth test keeps the first fragment drawn at a depth, so the
        // queue is walked backwards: at one distance the later call owns the
        // pixel, as a later canvas draw paints over an earlier one.
        for (auto draw = queue_->draws_.rbegin(); draw != queue_->draws_.rend(); ++draw) {
            // An image released since the draw was queued is skipped.
            const IRComponents::C_CanvasDrawImages::Image *image =
                IRPrefab::CanvasDraw::detail::findImage(*images_, draw->handle_);
            if (image == nullptr || image->texture_.second == nullptr) {
                continue;
            }
            frameData_.command_ = draw->command_;
            if (!IRPrefab::CanvasDraw::clipFramebufferImage(
                    frameData_.command_,
                    frameData_.targetSize_
                )) {
                continue;
            }
            if (!passBound) {
                bindPass(framebuffer);
                passBound = true;
            }
            // A texture bind, not the kernel's image bind: a Metal render
            // encoder is handed only the units bound this way.
            image->texture_.second->bind(IRRender::kCanvasImageAtlasUnit);
            // The whole buffer every draw: Metal then swaps in a fresh
            // backing instead of encoding a patch copy into one a queued draw
            // still reads.
            frameDataBuf_->subData(0, sizeof(FrameDataImageToFramebuffer), &frameData_);
            IRRender::device()->drawElements(
                IRRender::DrawMode::TRIANGLES,
                IRShapes2D::kQuadIndicesLength,
                IRRender::IndexType::UNSIGNED_SHORT
            );
        }
    }

    void endTick() {
        queue_->draws_.clear();
    }

    void bindPass(const IRComponents::C_TrixelCanvasFramebuffer &framebuffer) const {
        framebuffer.bindFramebuffer();
        program_->use();
        quadVao_->bind();
        frameDataBuf_->bindBase(
            IRRender::BufferTarget::UNIFORM,
            IRRender::kBufferIndex_ImageToFramebufferFrameData
        );
        IRRender::device()->setPolygonMode(IRRender::PolygonMode::FILL);
        IRRender::device()->setDepthTest(true);
        IRRender::device()->setDepthWrite(true);
    }

    static SystemId create() {
        IRPrefab::CanvasDraw::ensureFramebufferImageQueueSingleton();
        IRPrefab::CanvasDraw::ensureImagesSingleton();
        IRRender::createNamedResource<IRRender::ShaderProgram>(
            "ImageToFramebufferProgram",
            std::vector{
                IRRender::ShaderStage{
                    IRRender::kFileVertImageToFramebuffer,
                    IRRender::ShaderType::VERTEX
                },
                IRRender::ShaderStage{
                    IRRender::kFileFragImageToFramebuffer,
                    IRRender::ShaderType::FRAGMENT
                }
            }
        );
        IRRender::createNamedResource<IRRender::Buffer>(
            "ImageToFramebufferFrameData",
            nullptr,
            sizeof(FrameDataImageToFramebuffer),
            IRRender::BUFFER_STORAGE_DYNAMIC,
            IRRender::BufferTarget::UNIFORM,
            IRRender::kBufferIndex_ImageToFramebufferFrameData
        );
        const SystemId systemId = registerSystem<
            IMAGE_TO_FRAMEBUFFER,
            IRComponents::C_TrixelCanvasFramebuffer,
            IRComponents::C_Name>("ImageToFramebuffer");
        auto *system = getSystemParams<System<IMAGE_TO_FRAMEBUFFER>>(systemId);
        system->frameDataBuf_ =
            IRRender::getNamedResource<IRRender::Buffer>("ImageToFramebufferFrameData");
        system->program_ =
            IRRender::getNamedResource<IRRender::ShaderProgram>("ImageToFramebufferProgram");
        system->quadVao_ = IRRender::getNamedResource<IRRender::VAO>("QuadVAO");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_IMAGE_TO_FRAMEBUFFER_H */

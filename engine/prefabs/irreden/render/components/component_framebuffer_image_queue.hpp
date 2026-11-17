#ifndef COMPONENT_FRAMEBUFFER_IMAGE_QUEUE_H
#define COMPONENT_FRAMEBUFFER_IMAGE_QUEUE_H

#include <irreden/render/ir_render_types.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace IRComponents {

// World-scoped singleton holding the frame's queued framebuffer image draws in
// call order. `IRPrefab::CanvasDraw::queueFramebufferImage` appends;
// `System<IMAGE_TO_FRAMEBUFFER>` draws and empties it, keeping the capacity.
// Nothing else empties it, so it holds at most `kMaxDraws`: without that
// system in the pipeline the queue fills once and stays put, and a draw
// queued onto a full queue is dropped.
struct C_FramebufferImageQueue {
    static constexpr std::size_t kMaxDraws = 256;

    // One queued image: the `C_CanvasDrawImages` handle its texture is looked
    // up by at the flush, and its command. The command's box is in main
    // framebuffer pixels and its clip is the whole box; the flush cuts it to
    // the framebuffer.
    struct Draw {
        std::uint32_t handle_ = 0;
        IRRender::CanvasImageCommand command_;
    };

    std::vector<Draw> draws_;
};

} // namespace IRComponents

#endif /* COMPONENT_FRAMEBUFFER_IMAGE_QUEUE_H */

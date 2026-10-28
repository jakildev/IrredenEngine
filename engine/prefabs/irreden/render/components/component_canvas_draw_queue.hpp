#ifndef COMPONENT_CANVAS_DRAW_QUEUE_H
#define COMPONENT_CANVAS_DRAW_QUEUE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/voxel_text.hpp>

#include <cstddef>
#include <vector>

namespace IRComponents {

// World-scoped singleton holding the frame's queued canvas draws, one batch
// per target canvas in first-use order. `IRPrefab::CanvasDraw` appends glyph
// commands; `System<CANVAS_DRAW>` rasterizes each batch onto its canvas and
// empties it, keeping the batches and their capacity for the next frame.
struct C_CanvasDrawQueue {
    struct Batch {
        IREntity::EntityId canvas_ = IREntity::kNullEntity;
        // The canvas's texel size, read at the frame's first draw onto it:
        // text layout aligns to the canvas's lattice parity and wraps to its
        // edge.
        IRMath::ivec2 canvasSize_ = IRMath::ivec2(0);
        std::vector<IRRender::GlyphDrawCommand> commands_;
        // One queued draw (a text, a voxel text, a rectangle): the texel box
        // its commands may write and the index past its last command. The
        // flush rasterizes primitives that meet an earlier one in a later
        // dispatch, so overlapping draws land in call order.
        struct Primitive {
            IRMath::ivec2 lo_ = IRMath::ivec2(0);
            IRMath::ivec2 hi_ = IRMath::ivec2(0);
            std::size_t end_ = 0;
        };
        std::vector<Primitive> primitives_;
    };

    std::vector<Batch> batches_;
    // Voxel-text layout buffers, reused across every queued voxel text.
    IRRender::VoxelTextLayout voxelLayout_;
    IRRender::VoxelTextScratch voxelScratch_;
};

} // namespace IRComponents

#endif /* COMPONENT_CANVAS_DRAW_QUEUE_H */

#ifndef IR_RENDER_TYPES_H
#define IR_RENDER_TYPES_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

#include <cstdint>

using namespace IRMath;

namespace IRRender {
typedef uint32_t ResourceId;
typedef uint32_t ResourceType;

struct TrixelData {
    vec4 color_;
    int distance_;
};

struct GlobalConstantsGLSL {
    int kMinTriangleDistance = IRConstants::kTrixelDistanceMinDistance;
    int kMaxTriangleDistance = IRConstants::kTrixelDistanceMaxDistance;
};

struct FrameDataFramebuffer {
    mat4 mvpMatrix;
    vec2 textureOffset; // TODO: Update in texture scroll system and make
    // a frame data component as well or add as field for shader program
};

struct FrameDataTrixelToFramebuffer {
    mat4 mpMatrix_;
    vec2 canvasZoomLevel_;
    vec2 cameraTrixelOffset_;
    vec2 textureOffset_;
    vec2 mouseHoveredTriangleIndex_;
    /// When smooth mode: effective subdivisions for hover coord conversion. x=subdivisions, y=unused.
    vec2 effectiveSubdivisionsForHover_;
    /// Config: when 0, hovered trixel is not visually highlighted (entity detection still works).
    float showHoverHighlight_;
};

struct FrameDataVoxelToCanvas {
    vec2 cameraTrixelOffset_;
    ivec2 trixelCanvasOffsetZ1_;
    ivec2 voxelRenderOptions_;
};

struct FrameDataTrixelToTrixel {
    ivec2 cameraTrixelOffset_;
    ivec2 trixelCanvasOffsetZ1_;
    ivec2 trixelTextureOffsetZ1_;
    vec2 texturePos2DIso_;
};

struct GlyphDrawCommand {
    uint32_t positionPacked;  // x | (y << 16)
    uint32_t glyphIndex;
    uint32_t colorPacked;     // RGBA as packed uint
    uint32_t distance;
};

enum class FitMode { FIT, STRETCH, UNKNOWN };
enum class VoxelRenderMode { SNAPPED = 0, SMOOTH = 1 };

} // namespace IRRender

#endif /* IR_RENDER_TYPES_H */

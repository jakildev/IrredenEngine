/*
 * Project: Irreden Engine
 * File: ir_render_types.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_RENDER_TYPES_H
#define IR_RENDER_TYPES_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

#include <cstdint>
#include <unordered_map>

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
        glm::mat4 mvpMatrix;
        vec2 textureOffset; // TODO: Update in texture scroll system and make
        // a frame data component as well or add as field for shader program
    };

    struct FrameDataTrixelToFramebuffer {
        mat4 mpMatrix_;
        vec2 canvasZoomLevel_;
        vec2 cameraTrixelOffset_;
        vec2 textureOffset_;
        vec2 mouseHoveredTriangleIndex_;
    };

    struct FrameDataVoxelToCanvas {
        vec2 cameraTrixelOffset_;
        ivec2 trixelCanvasOffsetZ1_;
    };

    struct FrameDataTrixelToTrixel {
        ivec2 cameraTrixelOffset_;
        ivec2 trixelCanvasOffsetZ1_;
        ivec2 trixelTextureOffsetZ1_;
        vec2 texturePos2DIso_;
    };

    enum class FitMode {
        FIT,
        STRETCH,
        UNKNOWN
    };

} // namespace IRRender

#endif /* IR_RENDER_TYPES_H */

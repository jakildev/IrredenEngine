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

#include <glad/glad.h>

#include <cstdint>
#include <unordered_map>

using namespace IRMath;

namespace IRRender {
    typedef uint32_t ResourceId;
    typedef uint32_t ResourceType;

    const std::unordered_map<GLenum, GLint> kMapSizeofGLType = {
        {GL_BYTE, sizeof(GLbyte)},
        {GL_SHORT, sizeof(GLshort)},
        {GL_INT, sizeof(GLint)},
        {GL_FIXED, sizeof(GLfixed)},
        {GL_FLOAT, sizeof(GLfloat)},
        {GL_HALF_FLOAT, sizeof(GLhalf)},
        {GL_DOUBLE, sizeof(GLdouble)},
        {GL_UNSIGNED_BYTE, sizeof(GLubyte)},
        {GL_UNSIGNED_SHORT, sizeof(GLushort)},
        {GL_UNSIGNED_INT, sizeof(GLuint)},
        {GL_INT_2_10_10_10_REV, sizeof(GLuint)},
        {GL_UNSIGNED_INT_2_10_10_10_REV, sizeof(GLuint)},
        {GL_UNSIGNED_INT_10F_11F_11F_REV,  sizeof(GLuint)}
    };

    const std::unordered_map<GLenum, GLint> kMapUnpackAlignmentofGLType = {
        {GL_R8, 1},
        {GL_RGB8, 4},
        {GL_RGBA8, 4},
        {GL_DEPTH24_STENCIL8, 4}
    };

    constexpr GLuint kBufferIndex_FrameDataUniform = 0; // unused

    struct TrixelData {
        vec4 color_;
        int distance_;
    };

    // C++ weekly ep 339
    // Use 'static constexpr' for constexpr values at function scope
    // Use 'inline constexpr' for constexpr values at file scope

    struct GlobalConstantsGLSL {
        int kMinTriangleDistance = IRConstants::kTrixelDistanceMinDistance;
        int kMaxTriangleDistance = IRConstants::kTrixelDistanceMaxDistance;
    };
    constexpr GLuint kBufferIndex_GlobalConstantsGLSL = 1;

    struct FrameDataFramebuffer {
        glm::mat4 mvpMatrix;
        vec2 textureOffset; // TODO: Update in texture scroll system and make
        // a frame data component as well or add as field for shader program
    };
    constexpr GLuint kBufferIndex_FramebufferFrameDataUniform = 2;
    constexpr GLsizeiptr kFramebufferFrameDataUniformBufferSize =
        sizeof(FrameDataFramebuffer);

    struct FrameDataTrixelToFramebuffer {
        mat4 mpMatrix_;
        vec2 canvasZoomLevel_;
        vec2 cameraTrixelOffset_;
        vec2 textureOffset_;
        vec2 mouseHoveredTriangleIndex_;
    };
    constexpr GLuint kBufferIndex_FrameDataUniformIsoTriangles = 3;

    constexpr GLuint kBufferIndex_SingleVoxelPositions = 5;
    constexpr GLuint kBufferIndex_SingleVoxelColors = 6;

    struct FrameDataVoxelToCanvas {
        vec2 cameraTrixelOffset_;
        ivec2 trixelCanvasOffsetZ1_;
    };

    constexpr GLuint kBufferIndex_FrameDataVoxelToCanvas = 7;

    constexpr GLuint kBufferIndex_VoxelSetUnlockedPositions = 8;
    constexpr GLuint kBufferIndex_VoxelSetUnlockedColors = 9;

    struct FrameDataTrixelToTrixel {
        ivec2 cameraTrixelOffset_;
        ivec2 trixelCanvasOffsetZ1_;
        ivec2 trixelTextureOffsetZ1_;
        vec2 texturePos2DIso_;
    };

    constexpr GLuint kBufferIndex_FrameDataTrixelToTrixel = 10;

} // namespace IRRender

#endif /* IR_RENDER_TYPES_H */

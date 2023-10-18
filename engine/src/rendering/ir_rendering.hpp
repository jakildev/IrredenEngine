/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\rendering\ir_rendering.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_RENDERING_H
#define IR_RENDERING_H

#include <glad/glad.h>
#include <unordered_map>
#include "../math/ir_math.hpp"
#include "../world/ir_constants.hpp"

using namespace IRMath;

namespace IRRendering {

    #define IR_ENABLE_WIREFRAME ENG_API->glPolygonMode(GL_FRONT_AND_BACK, GL_LINE)
    #define IR_DISABLE_WIREFRAME ENG_API->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL)

    const unsigned int kRenderTargetMaxVertices = 65536;
    const unsigned int kRenderTargetMaxIndices = 65536;

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

    struct FrameData {
        mat4 mvpMatrix;
        int isWireframe;
    };
    const GLuint kBufferIndex_FrameDataUniform = 0;
    const GLsizeiptr kFrameDataUniformBufferSize = sizeof(FrameData);

    // C++ weekly ep 339
    // Use 'static constexpr' for constexpr values at function scope
    // Use 'inline constexpr' for constexpr values at file scope
    inline constexpr struct GlobalConstantsGLSL {
        ivec2 kCanvasTriangleOriginOffsetX1 = IRConstants::kScreenTriangleOriginOffsetX1;
        ivec2 kCanvasTriangleOriginOffsetZ1 = IRConstants::kScreenTriangleOriginOffsetZ1;
        int kMinTriangleDistance = IRConstants::kTriangleDistanceMinDistance;
        int kMaxTriangleDistance = IRConstants::kTriangleDistanceMaxDistance;
    } kGlobalConstantsGLSL;
    const GLuint kBufferIndex_GlobalConstantsGLSL = 1;

    struct FrameDataFramebuffer {
        glm::mat4 mvpMatrix;
        vec2 textureOffset;
    };
    const GLuint kBufferIndex_FramebufferFrameDataUniform = 2;
    const GLsizeiptr kFramebufferFrameDataUniformBufferSize =
        sizeof(FrameDataFramebuffer);

    struct FrameDataIsoTriangles {
        mat4 mpMatrix_;
        vec2 canvasZoomLevel_;
        vec2 canvasOffset_;
        vec2 textureOffset_;
    };
    const GLuint kBufferIndex_FrameDataUniformIsoTriangles = 3;
    const GLsizeiptr kFrameDataUniformBufferSizeIsoTriangles =
        sizeof(FrameDataIsoTriangles);

    struct FrameDataIsoTriangleCompute {
        ivec2 imageOffset_;
        unsigned int imageDistanceOffset_;
    };
    const GLuint kBufferIndex_ComputeShaderImageOffset = 4;

    const GLuint kBufferIndex_SingleVoxelPositions = 5;
    const GLuint kBufferIndex_SingleVoxelColors = 6;

    struct FrameDataVoxelToCanvas {
        vec2 canvasOffset_;
    };

    const GLuint kBufferIndex_FrameDataVoxelToCanvas = 7;

    const GLuint kBufferIndex_VoxelSetUnlockedPositions = 8;
    const GLuint kBufferIndex_VoxelSetUnlockedColors = 9;

} // namespace IRRendering

#endif /* IR_RENDERING_H */

#ifndef OPENGL_TYPES_H
#define OPENGL_TYPES_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/shader_names.hpp>

#include <glad/glad.h>

#include <cstdint>
#include <unordered_map>

using namespace IRMath;

namespace IRRender {

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
    {GL_UNSIGNED_INT_10F_11F_11F_REV, sizeof(GLuint)}
};

const std::unordered_map<GLenum, GLint> kMapUnpackAlignmentofGLType = {
    {GL_R8, 1}, {GL_RGB8, 4}, {GL_RGBA8, 4}, {GL_DEPTH24_STENCIL8, 4}
};

constexpr GLuint kBufferIndex_FrameDataUniform = 0; // unused
constexpr GLuint kBufferIndex_GlobalConstantsGLSL = 1;
constexpr GLuint kBufferIndex_FramebufferFrameDataUniform = 2;
constexpr GLsizeiptr kFramebufferFrameDataUniformBufferSize = sizeof(FrameDataFramebuffer);
constexpr GLuint kBufferIndex_FrameDataUniformIsoTriangles = 3;
constexpr GLuint kBufferIndex_SingleVoxelPositions = 5;
constexpr GLuint kBufferIndex_SingleVoxelColors = 6;
constexpr GLuint kBufferIndex_FrameDataVoxelToCanvas = 7;
constexpr GLuint kBufferIndex_VoxelSetUnlockedPositions = 8;
constexpr GLuint kBufferIndex_VoxelSetUnlockedColors = 9;
constexpr GLuint kBufferIndex_FrameDataTrixelToTrixel = 10;
constexpr GLuint kBufferIndex_FontData = 11;
constexpr GLuint kBufferIndex_GlyphDrawCommands = 12;
constexpr GLuint kBufferIndex_VoxelEntityIds = 13;
constexpr GLuint kBufferIndex_HoveredEntityId = 14;

} // namespace IRRender

#endif /* OPENGL_TYPES_H */

#ifndef IR_RENDER_H
#define IR_RENDER_H

#include <glad/glad.h>
#include <unordered_map>
#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

namespace IRRender {
    class Renderer;
    class ImageData;
    class RenderingResourceManager;

    typedef uint32_t ResourceId;
    typedef uint32_t ResourceType;

    using namespace IRMath;

    #define IR_ENABLE_WIREFRAME ENG_API->glPolygonMode(GL_FRONT_AND_BACK, GL_LINE)
    #define IR_DISABLE_WIREFRAME ENG_API->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL)

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
    constexpr GLuint kBufferIndex_FrameDataUniform = 0;

    // C++ weekly ep 339
    // Use 'static constexpr' for constexpr values at function scope
    // Use 'inline constexpr' for constexpr values at file scope
    inline constexpr struct GlobalConstantsGLSL {
        ivec2 kCanvasTriangleOriginOffsetX1 = IRConstants::kScreenTriangleOriginOffsetX1;
        ivec2 kCanvasTriangleOriginOffsetZ1 = IRConstants::kScreenTriangleOriginOffsetZ1;
        int kMinTriangleDistance = IRConstants::kTriangleDistanceMinDistance;
        int kMaxTriangleDistance = IRConstants::kTriangleDistanceMaxDistance;
    } kGlobalConstantsGLSL;
    constexpr GLuint kBufferIndex_GlobalConstantsGLSL = 1;

    struct FrameDataFramebuffer {
        glm::mat4 mvpMatrix;
        vec2 textureOffset;
    };
    constexpr GLuint kBufferIndex_FramebufferFrameDataUniform = 2;
    constexpr GLsizeiptr kFramebufferFrameDataUniformBufferSize =
        sizeof(FrameDataFramebuffer);

    struct FrameDataIsoTriangles {
        mat4 mpMatrix_;
        vec2 canvasZoomLevel_;
        vec2 canvasOffset_;
        vec2 textureOffset_;
    };
    constexpr GLuint kBufferIndex_FrameDataUniformIsoTriangles = 3;

    struct FrameDataIsoTriangleCompute {
        ivec2 imageOffset_;
        unsigned int imageDistanceOffset_;
    };
    constexpr GLuint kBufferIndex_ComputeShaderImageOffset = 4;

    constexpr GLuint kBufferIndex_SingleVoxelPositions = 5;
    constexpr GLuint kBufferIndex_SingleVoxelColors = 6;

    struct FrameDataVoxelToCanvas {
        vec2 canvasOffset_;
    };

    constexpr GLuint kBufferIndex_FrameDataVoxelToCanvas = 7;

    constexpr GLuint kBufferIndex_VoxelSetUnlockedPositions = 8;
    constexpr GLuint kBufferIndex_VoxelSetUnlockedColors = 9;

    extern RenderingResourceManager* g_renderingResourceManager;
    RenderingResourceManager& getRenderingResourceManager();

    // RenderingResourceManager& initRenderingResourceManager();

    template <typename T, typename... Args>
    std::pair<ResourceId, T*> createResource(Args&&... args);

    template <typename T>
    void destroyResource(ResourceId resource);


} // namespace IRRender

#endif /* IR_RENDER_H */

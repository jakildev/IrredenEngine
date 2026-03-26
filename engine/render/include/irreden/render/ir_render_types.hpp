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
    ivec2 voxelDispatchGrid_;
    int voxelCount_;
    int _voxelDispatchPadding_ = 0;
    ivec2 canvasSizePixels_;
    ivec2 _canvasPadding_ = ivec2(0);
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

struct GPUEntityTransform {
    vec4 worldPosition;
    std::uint32_t poolOffset;
    std::uint32_t voxelCount;
    std::uint32_t _padding0 = 0;
    std::uint32_t _padding1 = 0;
};

struct GPUUpdateParams {
    int entityCount;
    int _padding[3] = {};
};

enum class ShapeType : std::uint32_t {
    BOX = 0,
    SPHERE = 1,
    CYLINDER = 2,
    ELLIPSOID = 3,
    WING = 4,
    PRISM = 5,
    TAPERED_BOX = 6,
    CUSTOM_SDF = 7
};

enum ShapeFlags : std::uint32_t {
    SHAPE_FLAG_NONE = 0,
    SHAPE_FLAG_HOLLOW = 1u << 0,
    SHAPE_FLAG_MIRROR_X = 1u << 1,
    SHAPE_FLAG_MIRROR_Y = 1u << 2,
    SHAPE_FLAG_VISIBLE = 1u << 3,
};

struct GPUShapeDescriptor {
    vec4 worldPosition;
    vec4 params;
    std::uint32_t shapeType;
    std::uint32_t color;
    std::uint32_t entityId;
    std::uint32_t jointIndex;
    std::uint32_t flags;
    std::uint32_t lodLevel;
    std::uint32_t _padding[2] = {};
};

struct GPUJointTransform {
    vec4 rotation;
    vec4 translation;
    std::uint32_t parentJointIndex;
    std::uint32_t _padding[3] = {};
};

struct GPUAnimationParams {
    float time;
    float speed;
    float phase;
    float _padding0;
    vec4 blend;
};

struct GPUShapesFrameData {
    vec2 cameraTrixelOffset;
    ivec2 trixelCanvasOffsetZ1;
    ivec2 canvasSize;
    int shapeCount;
    int passIndex;
    ivec2 voxelRenderOptions;
};

constexpr std::uint32_t kBufferIndex_FrameDataUniform = 0;
constexpr std::uint32_t kBufferIndex_GlobalConstantsGLSL = 1;
constexpr std::uint32_t kBufferIndex_FramebufferFrameDataUniform = 2;
constexpr std::uint32_t kFramebufferFrameDataUniformBufferSize = sizeof(FrameDataFramebuffer);
constexpr std::uint32_t kBufferIndex_FrameDataUniformIsoTriangles = 3;
constexpr std::uint32_t kBufferIndex_SingleVoxelPositions = 5;
constexpr std::uint32_t kBufferIndex_SingleVoxelColors = 6;
constexpr std::uint32_t kBufferIndex_FrameDataVoxelToCanvas = 7;
constexpr std::uint32_t kBufferIndex_VoxelSetUnlockedPositions = 8;
constexpr std::uint32_t kBufferIndex_VoxelSetUnlockedColors = 9;
constexpr std::uint32_t kBufferIndex_FrameDataTrixelToTrixel = 10;
constexpr std::uint32_t kBufferIndex_FontData = 11;
constexpr std::uint32_t kBufferIndex_GlyphDrawCommands = 12;
constexpr std::uint32_t kBufferIndex_VoxelEntityIds = 13;
constexpr std::uint32_t kBufferIndex_HoveredEntityId = 14;
constexpr std::uint32_t kBufferIndex_DebugOverlayData = 15;
constexpr std::uint32_t kBufferIndex_LocalVoxelPositions = 17;
constexpr std::uint32_t kBufferIndex_EntityTransforms = 18;
constexpr std::uint32_t kBufferIndex_UpdateParams = 19;
constexpr std::uint32_t kBufferIndex_ShapeDescriptors = 20;
constexpr std::uint32_t kBufferIndex_JointTransforms = 21;
constexpr std::uint32_t kBufferIndex_AnimationParams = 22;

} // namespace IRRender

#endif /* IR_RENDER_TYPES_H */

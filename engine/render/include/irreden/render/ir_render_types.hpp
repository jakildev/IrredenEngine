#ifndef IR_RENDER_TYPES_H
#define IR_RENDER_TYPES_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

#include <cstdint>

using namespace IRMath;

namespace IRRender {

/// Opaque handle returned by @c createResource<T>() / @c createNamedResource<T>().
/// Pass to @c getResource<T>() / @c destroyResource<T>().
typedef uint32_t ResourceId;

/// Internal type discriminant used by @c RenderingResourceManager; not used by callers.
typedef uint32_t ResourceType;

/// Single trixel (triangle-pixel) value written by the voxel→trixel compute shaders.
struct TrixelData {
    vec4 color_;
    int distance_;
};

/// UBO uploaded once at init time. Contains the distance sentinel values used
/// in both GLSL and Metal shaders to detect "empty" trixels.
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
    /// When smooth mode: effective subdivisions for hover coord conversion. x=subdivisions,
    /// y=unused.
    vec2 effectiveSubdivisionsForHover_;
    /// Config: when 0, hovered trixel is not visually highlighted (entity detection still works).
    float showHoverHighlight_;
    /// Added to raw canvas distance before depth normalization.
    /// 0 for world/overlay canvases; pos3DtoDistance(entityPos) for per-entity canvases.
    int distanceOffset_ = 0;
};

struct FrameDataVoxelToCanvas {
    vec2 cameraTrixelOffset_;
    ivec2 trixelCanvasOffsetZ1_;
    ivec2 voxelRenderOptions_;
    ivec2 voxelDispatchGrid_;
    int voxelCount_;
    int _voxelDispatchPadding_ = 0;
    ivec2 canvasSizePixels_;
    // Iso-space cull viewport: voxels whose iso position falls outside
    // [cullIsoMin, cullIsoMax] are skipped.  Matches the CPU chunk mask
    // viewport so the per-voxel test refines chunk boundaries cleanly.
    // Derived from the cull camera (frozen or live) and zoom.
    ivec2 cullIsoMin_ = ivec2(0);
    ivec2 cullIsoMax_ = ivec2(0);
    // Z-yaw camera rotation, in radians. visualYaw_ is the canonical
    // continuous angle written by gameplay; rasterYaw_ is the cardinal-snap
    // multiple of pi/2 nearest visualYaw_; residualYaw_ = visualYaw_ -
    // rasterYaw_. The integer trixel rasterizer picks a basis permutation
    // from rasterYaw_; the screen-space residual composite pass consumes
    // residualYaw_.
    float visualYaw_ = 0.0f;
    float rasterYaw_ = 0.0f;
    float residualYaw_ = 0.0f;
    float _yawPadding_ = 0.0f;
};

struct FrameDataTrixelToTrixel {
    ivec2 cameraTrixelOffset_;
    ivec2 trixelCanvasOffsetZ1_;
    ivec2 trixelTextureOffsetZ1_;
    vec2 texturePos2DIso_;
};

struct GlyphDrawCommand {
    uint32_t positionPacked; // x | (y << 16)
    uint32_t glyphIndex;
    uint32_t colorPacked; // RGBA as packed uint
    uint32_t distance;
    uint32_t styleFlags = 0;
};

/// How the output canvas is scaled to fill the window.
enum class FitMode { FIT, STRETCH, UNKNOWN };

/// Controls how the voxel→trixel compute pass subdivides trixel cells.
/// - @c NONE          — no subdivision; voxels snap to the nearest trixel
///   grid cell. Fast, pixel-perfect. Equivalent to the old SNAPPED mode.
/// - @c POSITION_ONLY — positions are subdivided by @c subdivisions (no zoom
///   scaling), but SDF/shape evaluation stays at base resolution. Gives
///   smooth entity movement without the GPU cost of full zoom subdivision.
/// - @c FULL          — positions subdivided by @c subdivisions × zoom.
///   Smoothest camera panning but highest GPU cost. Equivalent to the old
///   SMOOTH mode. Changing mode or subdivisions mid-frame stalls the pipeline.
/// @note Currently global (per-frame). Per-entity subdivision modes are future work.
enum class SubdivisionMode { NONE = 0, POSITION_ONLY = 1, FULL = 2 };
enum class LodLevel : std::uint32_t { LOD_0 = 0, LOD_1 = 1, LOD_2 = 2, LOD_3 = 3, LOD_4 = 4 };

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

/// SDF primitive type dispatched to the shapes→trixel compute shader.
/// Each value corresponds to a branch in the shader's SDF evaluation function.
enum class ShapeType : std::uint32_t {
    BOX = 0,
    SPHERE = 1,
    CYLINDER = 2,
    ELLIPSOID = 3,
    CURVED_PANEL = 4,
    WEDGE = 5,
    TAPERED_BOX = 6,
    CUSTOM_SDF = 7, ///< User-supplied SDF; requires a matching shader specialization.
    CONE = 8,
    TORUS = 9
};

/// Bit-combinable rendering flags stored in @c GPUShapeDescriptor::flags.
/// Combine with @c |.
enum ShapeFlags : std::uint32_t {
    SHAPE_FLAG_NONE = 0,
    SHAPE_FLAG_HOLLOW = 1u << 0, ///< Render only the shell; skip interior voxels.
    SHAPE_FLAG_MIRROR_X = 1u << 1,
    SHAPE_FLAG_MIRROR_Y = 1u << 2,
    SHAPE_FLAG_VISIBLE = 1u << 3,
    /// Forward-looking: snap joint rotation to nearest 90° in iso-adjusted space.
    /// Not yet implemented.
    SHAPE_FLAG_DISCRETE_ROTATION = 1u << 4,
    SHAPE_FLAG_CHECKERBOARD = 1u << 5,
    /// Color each voxel by its LOCAL iso-depth along the camera's forward axis,
    /// normalized to [0, 1] over the shape's own depth extent. Useful for
    /// visually distinguishing individual shapes regardless of world position.
    SHAPE_FLAG_DEPTH_COLOR = 1u << 6,
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
    ivec2 cullIsoMin;
    ivec2 cullIsoMax;
    // Z-yaw camera rotation (radians); SDF math is continuous so it consumes
    // visualYaw directly.
    float visualYaw = 0.0f;
    float _yawPadding[3] = {};
};

struct FrameDataSun {
    // xyz = unit vector pointing from surfaces toward the sun; w unused.
    // Default mirrors RenderManager::m_sunDirection (overhead with small
    // +X / +Y tilt). Live frame data is overwritten from resolveSun()
    // each tick — this default only matters before the first tick.
    vec4 sunDirection_ = vec4(0.3f, 0.2f, -0.93f, 0.0f);
    float sunIntensity_ = 1.0f;
    float sunAmbient_ = 0.4f;
    int shadowsEnabled_ = 1;
    int shapeCasterCount_ = 0;
    ivec4 _padding_ = ivec4(0);
};
static_assert(sizeof(FrameDataSun) == 48, "FrameDataSun must match std140 layout");

/// @{
/// @name GPU buffer binding points
/// **CRITICAL:** These indices are hard-coded in both C++ and GLSL/MSL shaders.
/// A mismatch between C++ and the shader is **silent** — no error, just wrong
/// uniforms / garbage data. When adding or renaming a buffer, update the
/// corresponding @c binding or @c [[buffer(N)]] annotation in the shader as well.
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
constexpr std::uint32_t kBufferIndex_ChunkVisibility = 24;
constexpr std::uint32_t kBufferIndex_CompactedVoxelIndices = 25;
constexpr std::uint32_t kBufferIndex_IndirectDispatchParams = 26;
constexpr std::uint32_t kBufferIndex_FrameDataLightingToTrixel = 27;
constexpr std::uint32_t kBufferIndex_OccupancyGrid = 28;
constexpr std::uint32_t kBufferIndex_FrameDataSun = 29;
constexpr std::uint32_t kBufferIndex_ShapeTileDescriptors = 30;
// Metal caps buffer slots at 30; the sun-shadow pass runs after
// SHAPES_TO_TRIXEL, so it can reuse the shape descriptor slot as long as each
// pass explicitly binds the buffer it needs before dispatch.
constexpr std::uint32_t kBufferIndex_SunShadowShapeCasters = kBufferIndex_ShapeDescriptors;
/// @}

// One entry per dispatched tile in the batched shapes→trixel pass.
// shapeIndex picks the ShapeDescriptor; tileIsoOrigin is the iso-space
// origin of this tile's 8×8 pixel footprint (already pre-aligned on CPU).
struct ShapeTileDescriptor {
    int shapeIndex = 0;
    int _pad0 = 0;
    ivec2 tileIsoOrigin = ivec2(0);
};

struct VoxelIndirectDispatchParams {
    std::uint32_t numGroupsX = 0;
    std::uint32_t numGroupsY = 0;
    std::uint32_t numGroupsZ = 0;
    std::uint32_t visibleCount = 0;
    std::uint32_t completedGroups = 0;
    std::uint32_t _padding[3] = {};
};

// TODO: Future culling optimization constants
// Chunk-level frustum culling: voxel pool is partitioned into chunks of
// this size. A CPU-side visibility pass writes a per-chunk mask that the
// voxel-to-trixel shaders check for early-out.
constexpr int kVoxelChunkSize = 256;

} // namespace IRRender

#endif /* IR_RENDER_TYPES_H */

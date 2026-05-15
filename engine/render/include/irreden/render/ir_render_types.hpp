#ifndef IR_RENDER_TYPES_H
#define IR_RENDER_TYPES_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/math/sdf.hpp>

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

/// Per-frame UBO for the SPRITE_TO_SCREEN pass. The orthographic projection
/// is computed CPU-side from the current viewport (one mat4 per frame, never
/// per-instance) so the vertex shader's per-sprite work stays minimal.
struct FrameDataSpritesToScreen {
    mat4 projection_;
};

/// Per-instance entry uploaded into the SpriteInstancesBuffer SSBO consumed
/// by the SPRITE_TO_SCREEN vertex shader. Layout is std430-friendly (vec4
/// alignment, 48 bytes total) so the GLSL and MSL declarations can share
/// the same byte layout. Screen position is the sprite quad's top-left
/// corner in screen-pixel coordinates (Y up); size is in screen pixels.
struct GpuSpriteInstance {
    vec4 screenPosSize_; ///< (screenX, screenY, sizeX, sizeY)
    vec4 uvRect_;        ///< (u0, v0, u1, v1), normalized to atlas
    vec4 tintRgba_;      ///< per-sprite tint, components in [0, 1]
};
static_assert(sizeof(GpuSpriteInstance) == 48, "GpuSpriteInstance must remain 48 bytes (std430)");

/// CPU-side intermediate for the SPRITE_TO_SCREEN gather + sort. Holds the
/// data needed to (a) sort by iso depth back-to-front and (b) group runs of
/// equal texture handles for one drawArraysInstanced call per atlas, then
/// project into a GpuSpriteInstance during upload.
struct SpriteRenderEntry {
    ResourceId textureHandle_ = 0;
    int isoDepth_ = 0; ///< pos3DtoDistance(world); larger = farther
    vec2 screenPos_ = vec2(0.0f);
    vec2 size_ = vec2(0.0f);
    vec4 uvRect_ = vec4(0.0f, 0.0f, 1.0f, 1.0f);
    vec4 tintRgba_ = vec4(1.0f);
};

/// Per-frame UBO for the SCREEN_SPACE_RESIDUAL_ROTATE pass. The model+offset
/// pair mirrors FrameDataFramebuffer so this stage can act as a drop-in
/// replacement for FRAMEBUFFER_TO_SCREEN; residualYaw_ is the leftover yaw
/// (visualYaw - rasterYaw, in [-pi/4, pi/4]) the fragment shader applies as
/// a 2D rotation in pixel space around the framebuffer center.
struct FrameDataScreenResidualRotate {
    mat4 mvpMatrix;
    // Always zero today; if non-zero, adjust the rotation center in the fragment
    // shader (currently anchored at ts * 0.5) to account for the scroll offset.
    vec2 textureOffset;
    float residualYaw = 0.0f;
    float _pad0 = 0.0f;
};

/// CPU mirror of the @c HoveredEntityIdBuffer SSBO layout (binding 14).
/// The fragment shader writes the hovered entity id + depth here every frame;
/// @c IRRender::getEntityIdAtMouseTrixel reads it back via persistent map.
/// The binary layout must stay byte-identical to the GLSL/MSL @c std430 block
/// declared in @c f_trixel_to_framebuffer.glsl and @c trixel_to_framebuffer.metal.
struct HoveredEntityIdLayout {
    uvec2 entityId_{0u, 0u};
    float depth_{1.0f};
    float _pad_{0.0f};
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
/// Canonical definition lives in @ref IRMath::SDF::ShapeType so the math-side
/// SDF helpers (`IRMath::SDF::evaluate`, `boundingHalf`, …) and the renderer
/// stay in lockstep without two parallel enums to keep synchronized.
using ShapeType = IRMath::SDF::ShapeType;

/// Bit-combinable shape flags stored in @c GPUShapeDescriptor::flags. The
/// canonical definition lives in @ref IRMath::SDF::ShapeFlags so the math
/// side and the renderer share one source of truth; the using-declarations
/// below re-export the @c SHAPE_FLAG_* enumerators into @c IRRender so the
/// existing @c IRRender::SHAPE_FLAG_VISIBLE spelling keeps working.
using ShapeFlags = IRMath::SDF::ShapeFlags;
using IRMath::SDF::SHAPE_FLAG_CHECKERBOARD;
using IRMath::SDF::SHAPE_FLAG_DEPTH_COLOR;
using IRMath::SDF::SHAPE_FLAG_DISCRETE_ROTATION;
using IRMath::SDF::SHAPE_FLAG_HOLLOW;
using IRMath::SDF::SHAPE_FLAG_MIRROR_X;
using IRMath::SDF::SHAPE_FLAG_MIRROR_Y;
using IRMath::SDF::SHAPE_FLAG_NONE;
using IRMath::SDF::SHAPE_FLAG_VISIBLE;
using IRMath::SDF::SHAPE_FLAG_XRAY_OCCLUDED;

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
    // Z-yaw camera rotation, in radians. Mirrors FrameDataVoxelToCanvas:
    // visualYaw is the canonical continuous angle, rasterYaw is the cardinal
    // multiple of pi/2 nearest visualYaw, residualYaw = visualYaw - rasterYaw.
    // The shapes shader rasterizes at rasterYaw so the SDF surface lands on
    // the same integer voxel lattice as the voxel pool's cardinal-snap raster
    // (T-055); the screen-space residual composite pass (T-058) then rotates
    // the trixel framebuffer by residualYaw to recover continuous yaw.
    float visualYaw = 0.0f;
    float rasterYaw = 0.0f;
    float residualYaw = 0.0f;
    float _yawPadding = 0.0f;
};

struct FrameDataSun {
    // xyz = unit vector pointing from surfaces toward the sun; w unused.
    // Default mirrors RenderManager::m_sunDirection (overhead with small
    // -X / -Y tilt — those match the outward-normal signs of the visible
    // X_FACE / Y_FACE so dot-product shading produces Z > X > Y).
    // Live frame data is uploaded by BAKE_SUN_SHADOW_MAP each tick — this
    // default only matters before the first tick.
    vec4 sunDirection_ = vec4(-0.3f, -0.2f, -0.93f, 0.0f);
    float sunIntensity_ = 1.0f;
    float sunAmbient_ = 0.4f;
    int shadowsEnabled_ = 1;
    // Mirrors `RenderManager::m_aoEnabled`. When 0 the AO compute shader
    // short-circuits with a constant 1.0 (no darkening) so the lighting
    // pass treats AO as a no-op. Wired in here rather than in its own UBO
    // because every consumer (AO compute, lighting) already binds
    // FrameDataSun.
    int aoEnabled_ = 1;
    // Orthonormal basis perpendicular to sunDirection_, computed CPU-side
    // each frame in system_bake_sun_shadow_map. .w is std140 padding.
    vec4 sunBasisU_ = vec4(0.0f);
    vec4 sunBasisV_ = vec4(0.0f);
    // sunPx = round((dot(p, uHat/vHat) - sunBufferOriginUV_) / sunBufferTexelSize_).
    // Sized to the visible iso AABB swept along -sunDir by kSunShadowMaxDistance.
    vec2 sunBufferOriginUV_ = vec2(0.0f);
    vec2 sunBufferTexelSize_ = vec2(1.0f);
};
static_assert(sizeof(FrameDataSun) == 80, "FrameDataSun must match std140 layout");
static_assert(offsetof(FrameDataSun, sunBasisU_) == 32, "sunBasisU_ must align after aoEnabled_");
static_assert(
    offsetof(FrameDataSun, sunBufferOriginUV_) == 64,
    "sunBufferOriginUV_ must align after sunBasisV_"
);

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
// Slot 4 was previously unused; the GPU light-volume seed pass reads
// `LightSourceBuffer` here. Metal caps buffer bindings at 0–30, which
// rules out the 31+ range that would otherwise be a more obvious home
// for this SSBO; future cleanup of the index map can renumber freely.
constexpr std::uint32_t kBufferIndex_LightSourceBuffer = 4;
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
// Slot 16 is also used by Metal compute shaders (c_voxel_to_trixel_stage_*, c_shapes_to_trixel)
// for the distanceScratch SSBO; the reuse is safe because compute and render encoders maintain
// independent argument tables.
constexpr std::uint32_t kBufferIndex_FrameDataScreenResidualRotate = 16;
constexpr std::uint32_t kBufferIndex_LocalVoxelPositions = 17;
constexpr std::uint32_t kBufferIndex_EntityTransforms = 18;
constexpr std::uint32_t kBufferIndex_UpdateParams = 19;
constexpr std::uint32_t kBufferIndex_ShapeDescriptors = 20;
constexpr std::uint32_t kBufferIndex_JointTransforms = 21;
constexpr std::uint32_t kBufferIndex_AnimationParams = 22;
// Slot 23 was previously unused; reused for the GPU light-volume
// dilation chain's UBO. Same Metal-cap rationale as
// `kBufferIndex_LightSourceBuffer` above.
constexpr std::uint32_t kBufferIndex_LightVolumeParams = 23;
constexpr std::uint32_t kBufferIndex_ChunkVisibility = 24;
constexpr std::uint32_t kBufferIndex_CompactedVoxelIndices = 25;
constexpr std::uint32_t kBufferIndex_IndirectDispatchParams = 26;
constexpr std::uint32_t kBufferIndex_FrameDataLightingToTrixel = 27;
// Slot 28: feeds only the light-volume propagate shader (voxel-existence
// + SDF-blocker bits). Neither AO nor sun-shadow reads this bitfield SSBO
// (see `kBufferIndex_SunShadowDepthMap` below for the slot-28 alias).
constexpr std::uint32_t kBufferIndex_LightOcclusionGrid = 28;
constexpr std::uint32_t kBufferIndex_FrameDataSun = 29;
constexpr std::uint32_t kBufferIndex_ShapeTileDescriptors = 30;
// Aliases the light-occlusion-grid slot. The light-volume propagate
// shader reads LightOcclusionGrid; the sun bake writes /
// the sun shadow lookup reads SunShadowDepthMap. Both consumers run on
// different stages and rebind slot 28 to whichever resource they need
// before their own dispatch, so the alias is safe. Phased-out producer:
// this aliasing goes away in T-09Y once light-volume LOS moves off the
// world-space bitfield.
constexpr std::uint32_t kBufferIndex_SunShadowDepthMap = kBufferIndex_LightOcclusionGrid;
// SPRITE_TO_SCREEN aliases two slots whose prior consumers finish before the
// sprite draw. Safety is enforced by a defensive rebind in
// `SPRITE_TO_SCREEN::bindPipeline()` — both slots are re-asserted to the
// sprite resources immediately before each draw call, displacing any earlier
// occupant. Slot 0 (FrameDataUniform) is also used by the T-163 stateless
// particle UBO; slot 25 (CompactedVoxelIndices) is written by
// VOXEL_TO_TRIXEL_STAGE_1 and consumed by STAGE_2. Same Metal 0–30 cap
// rationale as `kBufferIndex_SunShadowDepthMap`.
constexpr std::uint32_t kBufferIndex_SpritesFrameData = kBufferIndex_FrameDataUniform;
constexpr std::uint32_t kBufferIndex_SpritesInstances = kBufferIndex_CompactedVoxelIndices;
// GPU particle slots (T-139 Phase 1). Metal caps bindings at 0–30, so we
// alias the particle SSBO and UBO onto slots whose other consumers run on
// non-overlapping compute encoders. Both `LightSourceBuffer` (slot 4) and
// `LightVolumeParams` (slot 23) are bound only by the COMPUTE_LIGHT_VOLUME
// stage's seed/propagate dispatches; the particle update and particle render
// passes never run inside that stage's encoder, so the rebind is safe (same
// rationale as `kBufferIndex_SunShadowDepthMap` above). Note: a creation
// that registers BOTH a particle render system and COMPUTE_LIGHT_VOLUME on
// the same canvas must order them so neither dispatch is in flight when the
// other rebinds — the established pipeline order
// (COMPUTE_LIGHT_VOLUME → LIGHTING_TO_TRIXEL → particle render) satisfies
// this.
constexpr std::uint32_t kBufferIndex_GpuParticleData = kBufferIndex_LightSourceBuffer;
constexpr std::uint32_t kBufferIndex_FrameDataGpuParticles = kBufferIndex_LightVolumeParams;
// Stateless particle slots (T-163 Phase 1). Split into a small per-frame
// UBO (header: currentTime, emitterCount, projection inputs) and a separate
// SSBO holding the emitter descriptor array. Splitting sidesteps the
// observed Metal-side flakiness when nested-struct arrays live in a
// `constant` (UBO) buffer at this size class — the SSBO path uses
// straightforward `device` storage with no implicit layout assumptions.
//
// UBO slot: aliases the long-reserved-but-unused `FrameDataUniform` slot 0
// — same slot the sprite pipeline borrows for `SpritesFrameData`. The
// aliasing is safe because `SPRITE_TO_SCREEN::bindPipeline()` defensively
// rebinds slot 0 to `SpritesFrameData` immediately before each draw call,
// so any prior stateless-particle UBO occupying slot 0 is always displaced
// before the sprite vertex shader reads it (OpenGL has global binding
// state; Metal compute and render encoders maintain independent argument
// tables, so the alias is inherently safe there).
// SSBO slot: aliases `kBufferIndex_LightSourceBuffer` (slot 4), already
// shared with `kBufferIndex_GpuParticleData` — both T-139 SSBO particles
// and T-163 stateless emitters can register on the same canvas, and each
// dispatch rebinds slot 4 to its own SSBO immediately before its dispatch
// (the established trixel pipeline order COMPUTE_LIGHT_VOLUME → particle
// passes guarantees the light volume's seed dispatch finishes before
// either particle pass binds the slot).
constexpr std::uint32_t kBufferIndex_FrameDataStatelessParticles = kBufferIndex_FrameDataUniform;
constexpr std::uint32_t kBufferIndex_StatelessParticleEmitters = kBufferIndex_LightSourceBuffer;
/// @}

/// Maximum number of light sources uploaded per frame to the
/// `LightSourceBuffer` SSBO consumed by the GPU light-volume seed pass.
/// Lights past this cap are silently dropped on the CPU side; the cap is
/// generous for the engine's current "few dozen lights" workloads.
constexpr std::uint32_t kLightVolumeMaxSources = 256;

/// Number of GPU dilation iterations the propagate pass runs each
/// frame. The propagate shader uses distance-tracked linear falloff —
/// alpha encodes residual strength, decremented by `stepFalloff` per
/// step. After this many iterations the wavefront has covered a Manhattan
/// radius of `1 / stepFalloff` cells (= 32 with the default falloff),
/// matching the typical EMISSIVE/POINT light radius the CPU BFS path
/// previously used. Phase 1c will replace the global radius cap with
/// per-light step counts.
constexpr int kLightVolumePropagateIterations = 32;

/// CPU mirror of the `LightSource` GPU struct uploaded to the
/// `LightSourceBuffer` SSBO. One entry per active `C_LightSource`
/// entity. Layout follows std430: every member is a `vec4` so the GPU
/// stride is 64 bytes per record. Decoded in `c_seed_light_volume`.
struct GPULightSource {
    /// xyz = world-space origin in voxel units (round-half-up of
    /// `C_PositionGlobal3D`); w = `LightType` cast to float.
    vec4 originAndType_ = vec4(0.0f);
    /// xyz = emissive RGB in [0, 1]; w = intensity scalar.
    vec4 colorAndIntensity_ = vec4(0.0f);
    /// xyz = unit direction (unused for EMISSIVE / POINT); w = radius
    /// in voxel cells (clamped to `kLightVolumePropagateIterations`).
    vec4 directionAndRadius_ = vec4(0.0f);
    /// x = cone aperture in degrees (SPOT only); yzw = std430 padding.
    vec4 coneAndPad_ = vec4(0.0f);
};
static_assert(sizeof(GPULightSource) == 64, "GPULightSource must match std430 layout");

/// CPU mirror of the propagate pass UBO. Uploaded each frame by
/// `system_compute_light_volume`. Read by `c_seed_light_volume.glsl`,
/// `c_propagate_light_volume.glsl`, and `c_lighting_to_trixel.glsl`.
struct LightVolumeParams {
    /// Must match `kLightVolumeSize` in
    /// `component_canvas_light_volume.hpp` (128 today).
    int gridSize_ = 128;
    /// `kLightVolumeSize / 2` — half-extent for world ↔ texel offset.
    int halfExtent_ = 64;
    /// Number of valid entries in the `LightSourceBuffer` SSBO this frame.
    int lightCount_ = 0;
    /// Per-step alpha decrement applied during distance-tracked
    /// propagation. The propagate shader stores residual strength in
    /// the alpha channel; each Manhattan step subtracts this value, so
    /// a light reaches `1 / stepFalloff_` cells before going dark
    /// (linear falloff, matching the CPU BFS's `1 - d/radius` curve).
    /// A single global falloff approximates per-light radius variation;
    /// per-light step counts remain a follow-up beyond Phase 1c.
    float stepFalloff_ = 1.0f / 32.0f;
    /// Phase 1c (#360): camera-anchored origin. The 128³ light volume
    /// is centered on this world voxel each frame so a panned camera
    /// keeps lights in-range. Stored as `ivec4` for std140 alignment;
    /// only `.xyz` is meaningful. `.w` is reserved for a future "snap
    /// quantum changed" or "force re-clear" flag.
    ivec4 worldOriginVoxel_ = ivec4(0);
};
static_assert(sizeof(LightVolumeParams) == 32, "LightVolumeParams must match std140 layout");

/// Phase 1c (#360): camera-anchored light-occlusion SSBO header.
/// Written to the first 16 bytes of `LightOcclusionGridBuffer` each
/// frame by `system_build_light_occlusion_grid`; the voxel + SDF-blocker
/// bitfields occupy the remainder (see `kLightOcclusionHeaderByteSize`
/// consumers in `system_build_light_occlusion_grid.hpp` and the SSBO
/// declarations in `c_propagate_light_volume.glsl` /
/// `metal/c_propagate_light_volume.metal`). The header avoids a second
/// buffer slot — Metal compute encoders share one global
/// `setBuffer(slot)` table per encoder, so a UBO and SSBO at the same
/// slot fight; embedding the header in the SSBO sidesteps the conflict
/// entirely. `.xyz` is the world voxel that maps to local cell
/// `(0,0,0)`; `.w` is reserved.
struct LightOcclusionGridHeader {
    ivec4 worldOriginVoxel_ = ivec4(0);
};
static_assert(
    sizeof(LightOcclusionGridHeader) == 16, "LightOcclusionGridHeader must match std430 layout"
);

// One entry per dispatched tile in the batched shapes→trixel pass.
// shapeIndex picks the ShapeDescriptor; tileIsoOrigin is the iso-space
// origin of this tile's 8×8 pixel footprint (already pre-aligned on CPU).
struct ShapeTileDescriptor {
    int shapeIndex = 0;
    int _pad0 = 0;
    ivec2 tileIsoOrigin = ivec2(0);
};

/// Single GPU particle record uploaded to the `GpuParticleData` SSBO. Phase 1
/// of T-139 — position + velocity drift + lifetime decay; spawn / collection
/// query / attraction-point fields land in subsequent phases.
///
/// Layout matches std430:
///   offset 0..11   position_  (vec3, 12 B)
///   offset 12..15  lifetime_  (float, 4 B — fills vec3 trailing pad)
///   offset 16..27  velocity_  (vec3, 12 B)
///   offset 28..31  color_     (uint32, 4 B — fills vec3 trailing pad)
/// Total 32 B per record. See `c_update_gpu_particles.glsl` and
/// `c_render_gpu_particles_to_trixel.glsl` for the GPU mirror.
///
/// Lifetime semantics: `lifetime_ <= 0.0f` means the slot is dead; both the
/// update and render compute kernels skip such slots. Setting `lifetime_ = 0`
/// is the canonical way to despawn from the CPU side.
struct GpuParticle {
    vec3 position_ = vec3(0.0f);
    float lifetime_ = 0.0f;
    vec3 velocity_ = vec3(0.0f);
    std::uint32_t color_ = 0u;
};
static_assert(sizeof(GpuParticle) == 32, "GpuParticle must match std430 layout");

/// Per-frame UBO for both the update and render-to-trixel particle compute
/// passes. The render pass ignores the `*Update*` fields and vice versa, but
/// they share one UBO so a single CPU upload feeds both dispatches.
struct FrameDataGpuParticles {
    // Update-pass fields:
    float deltaTime_ = 0.0f;
    std::uint32_t particleCount_ = 0u;
    std::uint32_t _updatePad0_ = 0u;
    std::uint32_t _updatePad1_ = 0u;
    // Render-pass fields (mirror the trixel-canvas projection inputs):
    vec2 cameraTrixelOffset_ = vec2(0.0f);
    ivec2 trixelCanvasOffsetZ1_ = ivec2(0);
    ivec2 canvasSizePixels_ = ivec2(0);
    int _renderPad0_ = 0;
    int _renderPad1_ = 0;
};
static_assert(
    sizeof(FrameDataGpuParticles) == 48, "FrameDataGpuParticles must match std140 layout"
);

/// GPU particle pool capacity per pool entity. Phase 1 caps the pool at this
/// fixed size; per-biome configurable capacity lands in Phase 2.
constexpr std::uint32_t kGpuParticlePoolCapacity = 4096u;

/// Cap on the number of stateless emitters per canvas. The 32 B header lives
/// in a UBO (slot 0); the emitter descriptors live in an SSBO (slot 4) —
/// only the header is subject to the 16 KB UBO guarantee. At 64 emitters
/// × 80 B = 5 120 B, the SSBO is a comfortable fit for Phase 1 workloads.
/// Tunable via a Phase 2 follow-up when real biome workloads land.
constexpr std::uint32_t kMaxStatelessEmitters = 64u;

/// Cap on particles per stateless emitter. The render dispatch fires
/// `emitterCount * kMaxParticlesPerEmitter` threads; threads with
/// `subIndex >= particlesPerEmitter` early-out, so a per-emitter
/// runtime cap of less than this constant pays its own way.
/// SYNC: kMaxParticlesPerEmitter must match the identically-named define in
/// c_render_stateless_particles_to_trixel.glsl and the Metal constant in
/// c_render_stateless_particles_to_trixel.metal — all three decompose gid
/// the same way; a value change in one that misses the others silently
/// breaks thread ID decomposition.
constexpr std::uint32_t kMaxParticlesPerEmitter = 256u;

/// T-163 Phase 1 — single stateless particle emitter descriptor. Particles
/// have no per-frame stored state; each shader thread reconstructs its
/// particle's position and color from `(emitter, subIndex, currentTime)` via
/// a closed-form gravity-with-jitter trajectory. The descriptor is purely an
/// input — the GPU never mutates it.
///
/// Layout is std430 (vec3 fields naturally followed by a trailing float use
/// the 4-byte pad slot, so each row is 16 B — coincidentally std140-
/// compatible). Five rows total = 80 B per emitter.
///   row 0: origin_.xyz                 | baseLifetimeSeconds_
///   row 1: baseVelocity_.xyz           | spawnRate_
///   row 2: gravity_.xyz                | baseColorPacked_
///   row 3: positionJitter_.xyz         | emitterFlags_
///   row 4: velocityJitter_.xyz         | particlesPerEmitter_
struct GpuParticleEmitter {
    vec3 origin_ = vec3(0.0f);
    float baseLifetimeSeconds_ = 1.0f;
    vec3 baseVelocity_ = vec3(0.0f);
    float spawnRate_ = 1.0f;
    vec3 gravity_ = vec3(0.0f);
    std::uint32_t baseColorPacked_ = 0u;
    vec3 positionJitter_ = vec3(0.0f);
    std::uint32_t emitterFlags_ = 0u;
    vec3 velocityJitter_ = vec3(0.0f);
    std::uint32_t particlesPerEmitter_ = 0u;
};
static_assert(
    sizeof(GpuParticleEmitter) == 80,
    "GpuParticleEmitter must match std430 layout (80 B per emitter)"
);

/// T-163 Phase 1 — per-frame UBO for the stateless particle render pass.
/// Header only: per-frame inputs (`currentTime_`, canvas projection
/// parameters) plus emitter count. The descriptor array lives in a
/// separate SSBO so the layout is straightforward `device` storage on
/// Metal rather than `constant` (UBO) storage, which sidestepped layout
/// flakiness with nested-struct arrays during Phase 1 bring-up.
///
/// `voxelRenderOptions_` mirrors `FrameDataVoxelToCanvas::voxelRenderOptions_`
/// (renderMode, effectiveSubdivisions). The particle pass reads it so each
/// particle paints a subdivision-scaled voxel diamond — same micro-position
/// walk the voxel pool uses — instead of a single 2×3 diamond regardless of
/// zoom. Without this the particle "voxels" stay at base resolution while
/// the voxel/SDF paths refine to sub² micro-cells per voxel under FULL mode,
/// and the two read as different sizes in the same frame.
struct FrameDataStatelessParticles {
    float currentTime_ = 0.0f;
    std::uint32_t emitterCount_ = 0u;
    vec2 cameraTrixelOffset_ = vec2(0.0f);
    ivec2 trixelCanvasOffsetZ1_ = ivec2(0);
    ivec2 canvasSizePixels_ = ivec2(0);
    ivec2 voxelRenderOptions_ = ivec2(0);
    ivec2 _padding_ = ivec2(0);
};
static_assert(
    sizeof(FrameDataStatelessParticles) == 48,
    "FrameDataStatelessParticles must match std140 layout (48 B header)"
);

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

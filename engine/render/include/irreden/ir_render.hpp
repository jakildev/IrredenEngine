#ifndef IR_RENDER_H
#define IR_RENDER_H

#include <irreden/render/render_manager.hpp>
#include <irreden/render/render_device.hpp>
#include <irreden/render/ir_render_enums.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/rendering_rm.hpp>
#include <irreden/render/shapes_2d.hpp>
#include <irreden/render/image_data.hpp>
#include <irreden/render/shader.hpp>
#include <irreden/render/shader_names.hpp>
#include <irreden/render/vao.hpp>
#include <irreden/render/voxel_pool_config.hpp>

namespace IRRender {

/// @cond INTERNAL
extern RenderingResourceManager *g_renderingResourceManager;
RenderingResourceManager &getRenderingResourceManager();

extern RenderManager *g_renderManager;
RenderManager &getRenderManager();
/// @endcond

/// @{
/// @name GPU resource management
/// Type-safe CRUD for GPU resources (shaders, textures, buffers, VAOs, …) managed
/// by the @c RenderingResourceManager pool. Resources are indexed by @c ResourceId
/// and type (@c typeid<T>); named resources support lookup by string key.

/// Allocate a new @c T resource. Returns @c {id, ptr} where @c ptr is valid until
/// @c destroyResource<T>(id) is called.
template <typename T, typename... Args> std::pair<ResourceId, T *> createResource(Args &&...args) {
    return getRenderingResourceManager().create<T>(std::forward<Args>(args)...);
}

/// Allocate a @c T resource and register it under @p name for later lookup via
/// @c getNamedResource<T>.
template <typename T, typename... Args>
std::pair<ResourceId, T *> createNamedResource(const std::string &name, Args &&...args) {
    return getRenderingResourceManager().createNamed<T>(name, std::forward<Args>(args)...);
}

/// Destroy the resource identified by @p resource and return its id to the free list.
template <typename T> void destroyResource(ResourceId resource) {
    getRenderingResourceManager().destroy<T>(resource);
}

/// Return a raw (non-owning) pointer to the resource. Null if @p resource is invalid.
template <typename T> T *getResource(ResourceId resource) {
    return getRenderingResourceManager().get<T>(resource);
}

/// Look up a previously-named resource. Returns null if @p resourceName is not registered.
template <typename T> T *getNamedResource(std::string resourceName) {
    return getRenderingResourceManager().getNamed<T>(resourceName);
}
/// @}

/// @{
/// @name Voxel pool
/// Allocate / release contiguous spans of voxel component arrays from the named canvas
/// pool. The four spans returned by @c allocateVoxels are co-indexed — element [i] of
/// each span belongs to voxel i — and @c VoxelPoolAllocation::startIndex_ is the offset
/// of those spans inside the pool's underlying voxel arrays. Pass that index back to
/// @c deallocateVoxels (and to @c C_VoxelPool::setEntityIdForRange); never recompute it
/// from `positions.data() - basePtr` against a separately-cached @c C_VoxelPool*, since
/// a canvas archetype mutation can leave such a cache pointing at a different pool.
/// @param size        Number of voxels to allocate.
/// @param canvasName  Canvas pool to allocate from (default @c "main").
inline VoxelPoolAllocation allocateVoxels(unsigned int size, std::string canvasName = "main") {
    return getRenderManager().allocateVoxels(size, canvasName);
}

/// Release a previously-allocated voxel span back to the pool. Pass the start index
/// returned by @c allocateVoxels and the same size used to allocate.
inline void deallocateVoxels(size_t startIndex, size_t size, std::string canvasName = "main") {
    getRenderManager().deallocateVoxels(startIndex, size, canvasName);
}

/// Push-at-mutation accessors for the per-slot active-mask owned by
/// @c C_VoxelPool. Callers that wrote into the color span via
/// @c VoxelPoolAllocation must follow up with one of these so the GPU
/// compact shader (`c_voxel_visibility_compact`) sees the same active
/// set as the CPU. See @c engine/prefabs/irreden/voxel/components/component_voxel_pool.hpp
/// "Active-slot mask" for the bit semantics.
inline void
markVoxelPoolRangeActive(size_t startIndex, size_t count, std::string canvasName = "main") {
    getRenderManager().markVoxelPoolRangeActive(startIndex, count, canvasName);
}

inline void
markVoxelPoolRangeInactive(size_t startIndex, size_t count, std::string canvasName = "main") {
    getRenderManager().markVoxelPoolRangeInactive(startIndex, count, canvasName);
}

inline void
markVoxelPoolVoxelActive(size_t voxelIndex, bool active, std::string canvasName = "main") {
    getRenderManager().markVoxelPoolVoxelActive(voxelIndex, active, canvasName);
}

inline void
resyncVoxelPoolRangeFromColors(size_t startIndex, size_t count, std::string canvasName = "main") {
    getRenderManager().resyncVoxelPoolRangeFromColors(startIndex, count, canvasName);
}
/// @}

/// @{
/// @name Canvas management
/// Each canvas is an ECS entity that owns three GPU textures (color RGBA8,
/// distance R32I, entity-id RG32UI) and is rendered in registration order.
/// Do not destroy a canvas entity mid-frame — see @c engine/render/CLAUDE.md.

/// Return the entity id of the canvas registered under @p canvasName.
inline IREntity::EntityId getCanvas(std::string canvasName) {
    return getRenderManager().getCanvas(canvasName);
}

/// Create a new canvas entity with a voxel pool of @p voxelPoolSize voxels and a
/// trixel texture of @p trixelSize pixels. Optionally bind it to an existing
/// @p framebuffer entity.
inline IREntity::EntityId createCanvas(
    std::string name,
    ivec3 voxelPoolSize,
    ivec2 trixelSize,
    IREntity::EntityId framebuffer = IREntity::EntityId{}
) {
    return getRenderManager().createCanvas(name, voxelPoolSize, trixelSize, framebuffer);
}

inline bool hasCanvas(const std::string &name) {
    return getRenderManager().hasCanvas(name);
}

/// Set the canvas that receives input from @c setActiveCanvas-aware systems
/// (e.g. the paint/draw tools in editor mode).
inline void setActiveCanvas(const std::string &name) {
    getRenderManager().setActiveCanvas(name);
}

inline IREntity::EntityId getActiveCanvasEntity() {
    return getRenderManager().getActiveCanvasEntity();
}
// Null-safe variant `getActiveCanvasEntityOrNull()` lives in
// `<irreden/render/active_canvas.hpp>` — included on demand by callers
// (e.g. C_ShapeDescriptor, C_VoxelSetNew) that need the headless-safe
// snapshot without pulling in the full render surface. See #753 (T-205).
/// @}

/// @{
/// @name Camera and viewport queries
/// Camera position and zoom are read by the voxel→trixel shaders every frame.
/// Viewport and scale-factor queries are needed when mapping pixel coordinates
/// to world space.

/// Camera position in isometric 2-D canvas space (trixel units).
vec2 getCameraPosition2DIso();
/// Current zoom factor as a 2-D scale (x and y may differ for anisotropic zoom).
vec2 getCameraZoom();
/// Size of one trixel in screen pixels at the current zoom level.
vec2 getTriangleStepSizeScreen();
/// Render viewport dimensions in pixels.
ivec2 getViewport();
/// Ratio of output framebuffer pixels to screen pixels (HiDPI scale factor).
ivec2 getOutputScaleFactor();
/// Copy a rectangular region from the default framebuffer into @p rgbaData (RGBA8).
/// Returns @c false on GL error. Used by the screenshot system.
bool readDefaultFramebuffer(int x, int y, int width, int height, void *rgbaData);
/// Mouse position in the output view (after upscaling), in pixels.
vec2 getMousePositionOutputView();
/// Logical game resolution before any output upscaling.
vec2 getGameResolution();
/// Main canvas size in trixels.
vec2 getMainCanvasSizeTrixels();
/// @}

/// @{
/// @name Mouse position (iso space)
/// Multiple iso-space mouse position variants are maintained because the render
/// and update pipelines run at different rates and the camera offset matters.
///
/// After T-293 the screen-space bilinear residual composite is gone —
/// `SCREEN_SPACE_RESIDUAL_ROTATE` is a pure framebuffer passthrough, and
/// residual yaw is folded into per-face `faceDeform[]` matrices that the
/// trixel emit shaders apply in 2D iso space. The picking helpers below
/// therefore no longer apply a `R2D(-residualYaw)` inverse: the cursor's
/// framebuffer pixel maps directly into the trixel-canvas frame. The 2D
/// variants stay in the **trixel canvas frame**: under non-zero rasterYaw
/// the canvas iso position = `M · R_z(rasterYaw) · world`, not `M · world`.
/// The trixel-index lookup path (@ref mouseTrixelPositionWorld → GPU
/// comparison) requires this frame to match the rasterized canvas; for
/// true world-frame coordinates use @ref mouseWorldPos3DAtIsoDepth, which
/// composes the additional `R_z(-rasterYaw)` lift.
///
/// Iso-space picking accuracy at non-cardinal yaws is bounded by the
/// geometric trixel deformation (a small per-face offset the picking
/// math does not reverse-compose today; follow-up).

/// Mouse position in iso canvas coordinates **as seen on screen** — no camera offset.
/// Use this to align UI overlays to the render output.
vec2 mousePosition2DIsoScreenRender();
/// Mouse position in iso canvas coordinates with camera offset applied.
/// Stays in the rasterYaw-rotated trixel canvas frame; not true world iso
/// under non-zero yaw — see the namespace doc above.
vec2 mousePosition2DIsoWorldRender();
/// Integer trixel coordinate of the mouse in the trixel canvas frame.
/// Matches what the voxel-to-trixel pass wrote, so a CPU-side trixel
/// index here equals the GPU-side trixel index of the voxel under the
/// cursor at any visualYaw.
ivec2 mouseTrixelPositionWorld();
/// Mouse position lifted to a 3D world point in the **unrotated world frame**
/// at the given **canvas-frame** iso depth. Composes the picking inverse
/// `R_z(-rasterYaw) · isoPixelToPos3D · screen` — after T-293 the screen-
/// space residual rotation is gone, so the `R2D(-residualYaw)` half of
/// the chain is no longer needed.
///
/// @p canvasIsoDepth is iso depth in the **rasterYaw-rotated canvas frame**
/// (= `rotated.x + rotated.y + rotated.z`), NOT in the unrotated world frame
/// (= `world.x + world.y + world.z`). At @c rasterYaw=0 the two coincide;
/// at non-zero @c rasterYaw they differ because the cardinal Z-rotation
/// permutes (x,y) without preserving the sum (e.g. index 1 maps
/// `(x,y,z)→(y,-x,z)`, so `y - x + z ≠ x + y + z` in general). The depth
/// is canvas-frame because @c isoPixelToPos3D recovers a 3D point in the
/// same frame as the iso pixel — the rotated canvas frame.
///
/// To target the iso-depth plane through a known world-frame reference
/// point (e.g. an entity's `C_WorldTransform.translation_`), rotate it into the
/// canvas frame and take its iso depth:
/// @code
///   const ivec3 worldRef = ... ;  // e.g. entity world position
///   const IRMath::CardinalIndex idx = IRMath::rasterYawCardinalIndex(
///       IRPrefab::Camera::getRasterYaw());
///   const float canvasIsoDepth = static_cast<float>(
///       IRMath::pos3DtoDistance(IRMath::rotateCardinalZ(worldRef, idx)));
///   const vec3 worldClick =
///       IRRender::mouseWorldPos3DAtIsoDepth(canvasIsoDepth);
/// @endcode
///
/// Use this when world-frame coordinates are needed (e.g. spawning an
/// entity at the click location).
vec3 mouseWorldPos3DAtIsoDepth(float canvasIsoDepth);
/// Entity id of the voxel under the mouse cursor, read from the entity-id GPU texture.
/// @note This reads a persistent-mapped GPU buffer — values become valid only after the
///       GPU pipeline has completed the previous frame's @c FRAMEBUFFER_TO_SCREEN pass.
IREntity::EntityId getEntityIdAtMouseTrixel();
/// @}

/// @{
/// @name Camera setters
void setCameraZoom(float zoom);
void setCameraPosition2DIso(vec2 pos);
/// @}

/// @{
/// @name Subdivision mode
/// @see SubdivisionMode for NONE / POSITION_ONLY / FULL trade-offs.
/// @see getVoxelRenderEffectiveSubdivisions for the value actually used by shaders.
void setSubdivisionMode(SubdivisionMode mode);
SubdivisionMode getSubdivisionMode();
/// Set the base subdivision count. In @c FULL mode it is multiplied by zoom;
/// in @c POSITION_ONLY it is used as-is; in @c NONE it is ignored (always 1).
void setVoxelRenderSubdivisions(int subdivisions);
int getVoxelRenderSubdivisions();
/// The actual subdivisions value sent to the shader, accounting for mode and zoom.
int getVoxelRenderEffectiveSubdivisions();
void zoomMainBackgroundPatternIn();
void zoomMainBackgroundPatternOut();
/// @}

/// @{
/// @name GUI state
void setGuiVisible(bool visible);
void toggleGuiVisible();
bool isGuiVisible();
/// Scale the GUI canvas. Changing this resizes the GUI canvas entity — do not change
/// mid-frame without understanding the coordinate-mapping consequences.
void setGuiScale(int scale);
int getGuiScale();
/// Enable / disable the trixel hover highlight (visual ring around the trixel under
/// the cursor). Entity-id detection continues regardless of this flag.
void setHoveredTrixelVisible(bool visible);
bool isHoveredTrixelVisible();
/// @}

/// @{
/// @name Directional sun lighting
/// Unit vector pointing from surfaces toward the sun. Consumed each frame
/// by the @c COMPUTE_SUN_SHADOW pass to ray-march the occupancy grid.
///
/// Irreden's isometric world uses +Z as the downward height axis, so a sun
/// above the ground must have @c dir.z <= 0. Positive Z points below the
/// world and makes top faces shadow themselves instead of casting shadows
/// onto the floor. The setter asserts this convention and normalizes on write
/// so callers can pass any non-zero vector.
void setSunDirection(vec3 dir);
vec3 getSunDirection();
/// Global directional sun intensity used when no ECS DIRECTIONAL light exists.
/// Values below zero are clamped to zero.
void setSunIntensity(float intensity);
float getSunIntensity();
/// Ambient floor for sun face shading. 0 is pure Lambert, 1 is fully ambient.
void setSunAmbient(float ambient);
float getSunAmbient();
/// When false, sun face shading remains active but projected shadows are disabled.
void setSunShadowsEnabled(bool enabled);
bool getSunShadowsEnabled();
/// When false, ambient occlusion crease darkening is skipped — the AO compute
/// shader short-circuits with a constant 1.0 so the lighting pass treats AO
/// as a no-op. Sun face shading and projected shadows are unaffected.
void setAOEnabled(bool enabled);
bool getAOEnabled();
/// @}

/// @{
/// @name Lighting debug overlay
/// Replaces the artistic composite in @c LIGHTING_TO_TRIXEL with a false-
/// color visualization of the underlying lighting buffers. See
/// @c DebugOverlayMode for the per-mode color encoding. Upstream lighting
/// passes (@c COMPUTE_VOXEL_AO, @c COMPUTE_SUN_SHADOW) keep running so the
/// values rendered are exactly what the artistic path would consume.
void setDebugOverlay(DebugOverlayMode mode);
DebugOverlayMode getDebugOverlay();
/// @}

} // namespace IRRender

#endif /* IR_RENDER_H */

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
/// pool. All four spans are co-indexed — element [i] of each span belongs to voxel i.
/// @param size        Number of voxels to allocate.
/// @param canvasName  Canvas pool to allocate from (default @c "main").
inline std::tuple<
    std::span<C_Position3D>,
    std::span<C_PositionOffset3D>,
    std::span<C_PositionGlobal3D>,
    std::span<C_Voxel>>
allocateVoxels(unsigned int size, std::string canvasName = "main") {
    return getRenderManager().allocateVoxels(size, canvasName);
}

/// Release voxel spans previously returned by @c allocateVoxels back to the pool.
inline void deallocateVoxels(
    std::span<C_Position3D> positions,
    std::span<C_PositionOffset3D> positionOffsets,
    std::span<C_PositionGlobal3D> positionGlobals,
    std::span<C_Voxel> voxels,
    std::string canvasName = "main"
) {
    getRenderManager()
        .deallocateVoxels(positions, positionOffsets, positionGlobals, voxels, canvasName);
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

/// Mouse position in iso canvas coordinates **as seen on screen** — no camera offset.
/// Use this to align UI overlays to the render output.
vec2 mousePosition2DIsoScreenRender();
/// Mouse position in iso canvas coordinates **in world space** — camera offset applied.
/// Use this for world-space entity placement or selection.
vec2 mousePosition2DIsoWorldRender();
/// Mouse position in iso world space sampled during the UPDATE pipeline tick.
/// Use this inside update systems; it may lag the render-pipeline variants by one frame.
vec2 mousePosition2DIsoUpdate();
/// Integer trixel coordinate of the mouse in world space.
ivec2 mouseTrixelPositionWorld();
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

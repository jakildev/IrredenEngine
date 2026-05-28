#include <irreden/ir_render.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_constants.hpp>

#include <irreden/render/rendering_rm.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/camera.hpp>

#include <cstring>

namespace IRRender {

RenderingResourceManager *g_renderingResourceManager = nullptr;
RenderingResourceManager &getRenderingResourceManager() {
    IR_ASSERT(g_renderingResourceManager != nullptr, "RenderingResourceManager not initalized");
    return *g_renderingResourceManager;
}

RenderManager *g_renderManager = nullptr;
RenderManager &getRenderManager() {
    IR_ASSERT(g_renderManager != nullptr, "RenderManager not initalized");
    return *g_renderManager;
}

RenderDevice *g_renderDevice = nullptr;
RenderDevice *device() {
    IR_ASSERT(g_renderDevice != nullptr, "RenderDevice not initialized");
    return g_renderDevice;
}

void setDevice(RenderDevice *renderDevice) {
    g_renderDevice = renderDevice;
}

vec2 getCameraPosition2DIso() {
    return getRenderManager().getCameraPosition2DIso();
}
vec2 getCameraZoom() {
    return getRenderManager().getCameraZoom();
}
vec2 getTriangleStepSizeScreen() {
    return getRenderManager().getTriangleStepSizeScreen();
}
ivec2 getViewport() {
    return getRenderManager().getViewport();
}
ivec2 getOutputScaleFactor() {
    return getRenderManager().getOutputScaleFactor();
}
bool readDefaultFramebuffer(int x, int y, int width, int height, void *rgbaData) {
    return device()->readDefaultFramebuffer(x, y, width, height, rgbaData);
}
vec2 getMousePositionOutputView() {
    const vec2 raw = IRInput::getMousePosition();
    const vec2 offset = getRenderManager().screenToOutputWindowOffset();
    const ivec2 scale = getRenderManager().getOutputScaleFactor();
    // screenToOutputOffset assumes outputResolution (game res); actual quad uses
    // resolution+extraPixelBuffer
    const vec2 bufferCorrection =
        vec2(IRConstants::kSizeExtraPixelBuffer) / vec2(2.0f) * vec2(scale);
    return raw - offset + bufferCorrection;
}
vec2 getGameResolution() {
    return getRenderManager().getGameResolution();
}

vec2 getMainCanvasSizeTrixels() {
    return getRenderManager().getMainCanvasSizeTriangles();
}

namespace {

// Residual yaw is folded into faceDeform[] in the trixel emit shaders (T-293);
// the screen-space residual-rotate stage has been fully retired (T-323). The
// picking chain therefore needs no residualYaw inverse step. Iso-space picking
// accuracy at non-cardinal yaws is bounded by the geometric trixel deformation,
// which is a small per-face offset the picking math doesn't reverse-compose today
// (follow-up).
vec2 mouseCanvasIso() {
    return IRMath::pos2DScreenToPos2DIso(
               IRRender::getMousePositionOutputView(),
               IRRender::getTriangleStepSizeScreen()
           ) -
           getMainCanvasSizeTrixels() / getCameraZoom() / vec2(2.0f);
}

} // namespace

vec2 mousePosition2DIsoScreenRender() {
    return mouseCanvasIso();
}

vec2 mousePosition2DIsoWorldRender() {
    return mousePosition2DIsoScreenRender() - IRRender::getCameraPosition2DIso();
}

vec3 mouseWorldPos3DAtIsoDepth(float canvasIsoDepth) {
    // Screen→world picking inverse per `.fleet/plans/T-054.md` (epic #310).
    // After T-293 the screen-space residual bilinear is gone and residual
    // yaw lives in the trixel emit shaders' faceDeform[] — the inverse
    // chain therefore collapses to the rasterYaw half only:
    //   world = R_z(-rasterYaw) · isoPixelToPos3D · screen
    // `mouseCanvasIso()` provides the canvas-frame iso pixel; isoPixelToPos3D
    // recovers the unique 3D point at the requested depth (= rotated.x +
    // rotated.y + rotated.z under rasterYaw); rotateCardinalZInv lifts
    // back to the world frame.
    const float rasterYaw = IRPrefab::Camera::getRasterYaw();
    const vec2 canvasIso = mouseCanvasIso() - IRRender::getCameraPosition2DIso();
    const vec3 rotatedWorld = IRMath::isoPixelToPos3D(
        static_cast<int>(glm::floor(canvasIso.x)),
        static_cast<int>(glm::floor(canvasIso.y)),
        canvasIsoDepth
    );
    return IRMath::rotateCardinalZInv(rotatedWorld, IRMath::rasterYawCardinalIndex(rasterYaw));
}

ivec2 mouseTrixelPositionWorld() {
    const ivec2 canvasSize = getRenderManager().getMainCanvasSizeTriangles();
    const ivec2 z1 = IRMath::trixelOriginOffsetZ1(canvasSize);
    const int shaderMod = (z1.x + z1.y) & 1;

    const int subdivisions = getVoxelRenderEffectiveSubdivisions();
    const vec2 scaledMousePos =
        IRRender::mousePosition2DIsoWorldRender() * static_cast<float>(subdivisions);

    const vec2 triIndex = IRMath::pos2DIsoToTriangleIndex(scaledMousePos, shaderMod);
    // Use floor() not truncation: truncation gives 0 for -0.5..0, breaking negative x coords
    return ivec2(glm::floor(triIndex + vec2(1, 1)));
}

IREntity::EntityId getEntityIdAtMouseTrixel() {
    auto *buf = IRRender::getNamedResource<Buffer>("HoveredEntityIdBuffer");
    if (!buf)
        return IREntity::kNullEntity;

    // Re-fetch every frame: on Metal, subData orphans the MTL::Buffer on
    // write (metal_buffer.cpp:58–79); a statically-cached pointer from
    // mapRange would be stale by the next frame.
    void *mappedPtr = buf->mapRange(
        0,
        sizeof(HoveredEntityIdLayout),
        BUFFER_STORAGE_MAP_READ | BUFFER_STORAGE_MAP_PERSISTENT | BUFFER_STORAGE_MAP_COHERENT
    );
    if (!mappedPtr)
        return IREntity::kNullEntity;

    uvec2 packed;
    std::memcpy(&packed, mappedPtr, sizeof(uvec2));

    return static_cast<IREntity::EntityId>(packed.x) |
           (static_cast<IREntity::EntityId>(packed.y) << 32);
}

void setCameraZoom(float zoom) {
    getRenderManager().setCameraZoom(zoom);
}

void setCameraPosition2DIso(vec2 pos) {
    getRenderManager().setCameraPosition2DIso(pos);
}

void setSubdivisionMode(SubdivisionMode mode) {
    getRenderManager().setSubdivisionMode(mode);
}

SubdivisionMode getSubdivisionMode() {
    return getRenderManager().getSubdivisionMode();
}

void setVoxelRenderSubdivisions(int subdivisions) {
    getRenderManager().setVoxelRenderSubdivisions(subdivisions);
}

int getVoxelRenderSubdivisions() {
    return getRenderManager().getVoxelRenderSubdivisions();
}

int getVoxelRenderEffectiveSubdivisions() {
    return getRenderManager().getVoxelRenderEffectiveSubdivisions();
}

void zoomMainBackgroundPatternIn() {
    getRenderManager().zoomMainBackgroundPatternIn();
}

void zoomMainBackgroundPatternOut() {
    getRenderManager().zoomMainBackgroundPatternOut();
}

void setGuiVisible(bool visible) {
    getRenderManager().setGuiVisible(visible);
}

void toggleGuiVisible() {
    getRenderManager().toggleGuiVisible();
}

bool isGuiVisible() {
    return getRenderManager().isGuiVisible();
}

void setGuiScale(int scale) {
    getRenderManager().setGuiScale(scale);
}

int getGuiScale() {
    return getRenderManager().getGuiScale();
}

void setHoveredTrixelVisible(bool visible) {
    getRenderManager().setHoveredTrixelVisible(visible);
}

bool isHoveredTrixelVisible() {
    return getRenderManager().isHoveredTrixelVisible();
}

void setSunDirection(vec3 dir) {
    IR_ASSERT(
        dir.z <= 0.0f,
        "Sun direction points from the world toward the sun; +Z is down, so z must be <= 0"
    );
    getRenderManager().setSunDirection(dir);
}

vec3 getSunDirection() {
    return getRenderManager().getSunDirection();
}

void setSunIntensity(float intensity) {
    getRenderManager().setSunIntensity(intensity);
}

float getSunIntensity() {
    return getRenderManager().getSunIntensity();
}

void setSunAmbient(float ambient) {
    getRenderManager().setSunAmbient(ambient);
}

float getSunAmbient() {
    return getRenderManager().getSunAmbient();
}

void setSunShadowsEnabled(bool enabled) {
    getRenderManager().setSunShadowsEnabled(enabled);
}

bool getSunShadowsEnabled() {
    return getRenderManager().getSunShadowsEnabled();
}

void setAOEnabled(bool enabled) {
    getRenderManager().setAOEnabled(enabled);
}

bool getAOEnabled() {
    return getRenderManager().getAOEnabled();
}

void setDebugOverlay(DebugOverlayMode mode) {
    getRenderManager().setDebugOverlay(mode);
}

DebugOverlayMode getDebugOverlay() {
    return getRenderManager().getDebugOverlay();
}

void setHDREnabled(bool enabled) {
    getRenderManager().setHDREnabled(enabled);
}

bool getHDREnabled() {
    return getRenderManager().getHDREnabled();
}

void setExposure(float exposure) {
    getRenderManager().setExposure(exposure);
}

float getExposure() {
    return getRenderManager().getExposure();
}

void setSkyIntensity(float intensity) {
    getRenderManager().setSkyIntensity(intensity);
}

float getSkyIntensity() {
    return getRenderManager().getSkyIntensity();
}

void setSkyColor(vec3 color) {
    getRenderManager().setSkyColor(color);
}

vec3 getSkyColor() {
    return getRenderManager().getSkyColor();
}

} // namespace IRRender

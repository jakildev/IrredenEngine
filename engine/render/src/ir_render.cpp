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

#include <cmath>
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

// Mirror of `kIdentityYawEpsilon` in `f_screen_residual_rotate.glsl` /
// `screen_residual_rotate.metal`. When |residualYaw| is below this, the
// composite shader bypasses its rotation and writes the source pixel-
// identical to the framebuffer, so the picking inverse must do the same
// or yaw=0 picks would shift by sub-pixel rounding.
constexpr float kIdentityYawEpsilon = 1e-6f;

// Inverse of the SCREEN_SPACE_RESIDUAL_ROTATE composite, applied to a
// position in framebuffer-pixel coordinates around the framebuffer
// center. The composite rotates source samples by -residualYaw (see
// f_screen_residual_rotate.glsl); recovering the source pixel from a
// screen-space pixel is the same rotation in the opposite direction.
//
// `IRPlatform::kGfx.screenYDirection_` flips the angle on backends whose
// screen-pixel Y axis runs opposite the framebuffer-texture Y axis (-1 on
// OpenGL, +1 on Metal/Vulkan), keeping the rotation visually correct
// regardless of which backend is compiled in.
//
// `residualYaw` is a parameter (not re-fetched here) so callers needing
// both halves of the yaw split can amortize the camera-component lookup
// — see `mouseWorldPos3DAtIsoDepth`.
vec2 inverseResidualYawOnFramebufferPixel(vec2 framebufferPixel, float residualYaw) {
    if (std::abs(residualYaw) < kIdentityYawEpsilon) {
        return framebufferPixel;
    }
    auto &framebuffer =
        IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
    const vec2 center = vec2(framebuffer.getResolutionPlusBuffer()) * 0.5f;
    const float effectiveAngle =
        -residualYaw * IRPlatform::kGfx.screenYDirection_;
    return IRMath::rotate2D(framebufferPixel - center, effectiveAngle) + center;
}

vec2 mouseCanvasIsoFromResidualYaw(float residualYaw) {
    return IRMath::pos2DScreenToPos2DIso(
               inverseResidualYawOnFramebufferPixel(
                   IRRender::getMousePositionOutputView(), residualYaw),
               IRRender::getTriangleStepSizeScreen()
           ) -
           getMainCanvasSizeTrixels() / getCameraZoom() / vec2(2.0f);
}

} // namespace

vec2 mousePosition2DIsoScreenRender() {
    return mouseCanvasIsoFromResidualYaw(IRPrefab::Camera::getResidualYaw());
}

vec2 mousePosition2DIsoWorldRender() {
    return mousePosition2DIsoScreenRender() - IRRender::getCameraPosition2DIso();
}

vec3 mouseWorldPos3DAtIsoDepth(float isoDepth) {
    // Full screen→world picking inverse per `.fleet/plans/T-054.md` (epic
    // #310, Option B):
    //   world = R_z(-rasterYaw) · isoPixelToPos3D · R2D(-residualYaw) · screen
    // The chain mirrors mousePosition2DIsoWorldRender (canvas-frame iso =
    // M · R_z(rasterYaw) · world); isoPixelToPos3D recovers the unique 3D
    // point at the requested iso depth (= x+y+z), and rotateCardinalZInv
    // undoes the cardinal raster rotation. Pulling both yaw halves via
    // getYawSplit() amortizes the camera-component lookup vs going
    // through the two-step public helpers.
    const auto [rasterYaw, residualYaw] = IRPrefab::Camera::getYawSplit();
    const vec2 canvasIso = mouseCanvasIsoFromResidualYaw(residualYaw) -
                           IRRender::getCameraPosition2DIso();
    const vec3 rotatedWorld = IRMath::isoPixelToPos3D(
        static_cast<int>(glm::floor(canvasIso.x)),
        static_cast<int>(glm::floor(canvasIso.y)),
        isoDepth);
    return IRMath::rotateCardinalZInv(rotatedWorld,
                                      IRMath::rasterYawCardinalIndex(rasterYaw));
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

void setScreenSpaceShadowsEnabled(bool enabled) {
    getRenderManager().setScreenSpaceShadowsEnabled(enabled);
}

bool getScreenSpaceShadowsEnabled() {
    return getRenderManager().getScreenSpaceShadowsEnabled();
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

} // namespace IRRender

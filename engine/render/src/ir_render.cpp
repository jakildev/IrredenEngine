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
#include <irreden/render/trixel_rect.hpp>
#include <irreden/render/trixel_text.hpp>

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
vec2 getEffectiveCameraIso() {
    const vec2 cameraIso = getRenderManager().getCameraPosition2DIso();
    if (getRenderManager().getRotationPivotMode() == RotationPivotMode::ORIGIN) {
        return cameraIso;
    }
    const float visualYaw = IRPrefab::Camera::getYaw();
    if (getRenderManager().hasRotationPivotFocus()) {
        // CAMERA_CENTER with an explicit point of interest (#1921): pivot Z-yaw
        // about the focus at its TRUE depth so it rotates in place. The
        // drift-cancel offset (`IRMath::cameraYawPivotOffset`) keeps the focus at
        // a constant on-screen position across the yaw sweep.
        return IRMath::cameraYawPivotOffset(
            cameraIso,
            getRenderManager().getRotationPivotFocus(),
            visualYaw
        );
    }
    // CAMERA_CENTER default — pivot Z-yaw about the EXACT z = 0 world point under
    // screen center, held fixed across the yaw sweep so the scene rotates in
    // place about what the player is looking at. The screen-center world point is
    // `isoPixelToPos3D(viewCenterIso, 0)`, where `viewCenterIso = canvasSize/2 -
    // trixelOriginOffsetZ1(canvasSize) - cameraIso` is the iso coordinate of the
    // viewport center (derive: a world point W lands at screen center when
    // `pos3DtoPos2DIso(W) + cameraIso == canvasCenterIso`, so `W =
    // isoPixelToPos3D(canvasCenterIso - cameraIso, 0)`). `cameraYawPivotOffset`
    // then drift-cancels so screen(F) is yaw-independent. At visualYaw == 0 it
    // returns cameraIso, byte-identical to ORIGIN mode (the cardinal fast path).
    // The DETACHED entity-canvas composite must place entities with
    // getEffectiveCameraIso() (not the raw camera pos) so detached and GRID pivot
    // together — see system_entity_canvas_to_framebuffer.hpp. See
    // docs/design/camera-yaw-pivot.md (#1352, #1942, #1944).
    const ivec2 canvasSize = getRenderManager().getMainCanvasSizeTriangles();
    const vec2 viewCenterIso =
        vec2(canvasSize) * 0.5f - vec2(IRMath::trixelOriginOffsetZ1(canvasSize)) - cameraIso;
    const vec3 cameraFocusWorld = IRMath::isoPixelToPos3D(viewCenterIso, 0.0f);
    return IRMath::cameraYawPivotOffset(cameraIso, cameraFocusWorld, visualYaw);
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

// The picking chain needs no residualYaw inverse step: residual yaw lives in the
// trixel emit shaders' faceDeform[], not in a screen-space stage. Iso-space
// picking accuracy at non-cardinal yaws is bounded by the geometric trixel
// deformation — a small per-face offset the picking math doesn't reverse-compose
// today (follow-up). See T-293, T-323.
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
    return mousePosition2DIsoScreenRender() - IRRender::getEffectiveCameraIso();
}

vec3 mouseWorldPos3DAtIsoDepth(float canvasIsoDepth) {
    // Screen→world picking inverse per `.fleet/plans/T-054.md` (epic #310).
    // The inverse chain is the rasterYaw half only:
    //   world = R_z(-rasterYaw) · isoPixelToPos3D · screen
    // `mouseCanvasIso()` provides the canvas-frame iso pixel; isoPixelToPos3D
    // recovers the unique 3D point at the requested depth (= rotated.x +
    // rotated.y + rotated.z under rasterYaw); rotateCardinalZInv lifts
    // back to the world frame.
    const float rasterYaw = IRPrefab::Camera::getRasterYaw();
    const vec2 canvasIso = mouseCanvasIso() - IRRender::getEffectiveCameraIso();
    const vec3 rotatedWorld = IRMath::isoPixelToPos3D(
        static_cast<int>(IRMath::floor(canvasIso.x)),
        static_cast<int>(IRMath::floor(canvasIso.y)),
        canvasIsoDepth
    );
    return IRMath::rotateCardinalZInv(rotatedWorld, IRMath::rasterYawCardinalIndex(rasterYaw));
}

ivec2 worldPos3DToMouseScreenPx(vec3 worldPos) {
    // Exact inverse of mouseWorldPos3DAtIsoDepth's screen→world chain, run in
    // reverse and reusing the identical live terms so the two never drift:
    //   world → rotateCardinalZ → pos3DtoPos2DIso (iso pixel of worldPos)
    //         → +0.5 (aim iso cell centre) → +effectiveCameraIso
    //         → +mainCanvasSizeTrixels/zoom/2 → *stepSize (undo /stepSize)
    //         → +letterboxOffset − bufferCorrection (undo getMousePositionOutputView)
    const float rasterYaw = IRPrefab::Camera::getRasterYaw();
    const vec3 rotated =
        IRMath::rotateCardinalZ(worldPos, IRMath::rasterYawCardinalIndex(rasterYaw));
    const vec2 canvasIso = IRMath::pos3DtoPos2DIso(rotated) + vec2(0.5f);
    const vec2 isoScreen = canvasIso + IRRender::getEffectiveCameraIso() +
                           getMainCanvasSizeTrixels() / getCameraZoom() / vec2(2.0f);
    const vec2 outputView = isoScreen * IRRender::getTriangleStepSizeScreen();
    const vec2 offset = getRenderManager().screenToOutputWindowOffset();
    const vec2 bufferCorrection = vec2(IRConstants::kSizeExtraPixelBuffer) / vec2(2.0f) *
                                  vec2(getRenderManager().getOutputScaleFactor());
    return IRMath::roundVec(outputView + offset - bufferCorrection);
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
    return ivec2(IRMath::floor(triIndex + vec2(1, 1)));
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

    // Strip the per-trixel priority carrier (top 2 bits of the high word, #1960)
    // before reconstructing the 64-bit id — THE chokepoint so a prioritized
    // fragment never reports a corrupted picked id.
    return static_cast<IREntity::EntityId>(IRRender::decodeCarrierEntityId(packed));
}

void setCameraZoom(float zoom) {
    getRenderManager().setCameraZoom(zoom);
}

void setCameraPosition2DIso(vec2 pos) {
    getRenderManager().setCameraPosition2DIso(pos);
}

void setCameraVisualYaw(float degrees) {
    IRPrefab::Camera::setYaw(degrees * IRMath::kPi / 180.0f);
}

void setSubdivisionMode(SubdivisionMode mode) {
    getRenderManager().setSubdivisionMode(mode);
}

SubdivisionMode getSubdivisionMode() {
    return getRenderManager().getSubdivisionMode();
}

void setRotationPivotMode(RotationPivotMode mode) {
    getRenderManager().setRotationPivotMode(mode);
}

RotationPivotMode getRotationPivotMode() {
    return getRenderManager().getRotationPivotMode();
}

void setRotationPivotFocus(vec3 focusWorld) {
    getRenderManager().setRotationPivotFocus(focusWorld);
}

void clearRotationPivotFocus() {
    getRenderManager().clearRotationPivotFocus();
}

bool hasRotationPivotFocus() {
    return getRenderManager().hasRotationPivotFocus();
}

vec3 getRotationPivotFocus() {
    return getRenderManager().getRotationPivotFocus();
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

void setGuiCanvasFullResolution() {
    getRenderManager().setGuiCanvasFullResolution();
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

void setVoxelOcclusionCullEnabled(bool enabled) {
    getRenderManager().setVoxelOcclusionCullEnabled(enabled);
}

bool getVoxelOcclusionCullEnabled() {
    return getRenderManager().getVoxelOcclusionCullEnabled();
}

void setVoxelPerVoxelOcclusionEnabled(bool enabled) {
    getRenderManager().setVoxelPerVoxelOcclusionEnabled(enabled);
}

bool getVoxelPerVoxelOcclusionEnabled() {
    return getRenderManager().getVoxelPerVoxelOcclusionEnabled();
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

void setDepthColorDebug(bool on, float extent) {
    getRenderManager().setDepthColorDebug(on, extent);
}

bool getDepthColorDebugMode() {
    return getRenderManager().getDepthColorDebugMode();
}

float getDepthColorDebugExtent() {
    return getRenderManager().getDepthColorDebugExtent();
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

namespace {
// Resolve the engine-default "gui" trixel canvas, or nullptr when none exists
// (headless / pre-render contexts). Shared by the GUI shape-draw entry points
// so the canvas lookup lives in one place.
IRComponents::C_TriangleCanvasTextures *guiCanvasTexturesOrNull() {
    const IREntity::EntityId guiCanvas = getCanvas("gui");
    if (guiCanvas == IREntity::kNullEntity) {
        return nullptr;
    }
    return &IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);
}
} // namespace

void drawGuiDisc(ivec2 center, int radius, Color color) {
    IRComponents::C_TriangleCanvasTextures *canvas = guiCanvasTexturesOrNull();
    if (canvas == nullptr) {
        return;
    }
    // Reused across calls so the per-scanline span buffers amortize; the render
    // path is single-threaded, so a function-local scratch is safe.
    static RectFillScratch scratch;
    fillDisc(*canvas, center, radius, color, kGuiTextDistance, scratch);
}

void drawGuiLine(ivec2 from, ivec2 to, Color color) {
    IRComponents::C_TriangleCanvasTextures *canvas = guiCanvasTexturesOrNull();
    if (canvas == nullptr) {
        return;
    }
    drawLine(*canvas, from, to, color, kGuiTextDistance);
}

} // namespace IRRender

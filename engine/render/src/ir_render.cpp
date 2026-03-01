#include <irreden/ir_render.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_constants.hpp>

#include <irreden/render/rendering_rm.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>

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
vec2 getMousePositionOutputView() {
    const vec2 raw = IRInput::getMousePositionRender();
    const vec2 offset = getRenderManager().screenToOutputWindowOffset();
    const ivec2 scale = getRenderManager().getOutputScaleFactor();
    // screenToOutputOffset assumes outputResolution (game res); actual quad uses resolution+extraPixelBuffer
    const vec2 bufferCorrection = vec2(IRConstants::kSizeExtraPixelBuffer) / vec2(2.0f) * vec2(scale);
    return raw - offset + bufferCorrection;
}
vec2 getGameResolution() {
    return getRenderManager().getGameResolution();
}

vec2 getMainCanvasSizeTrixels() {
    return getRenderManager().getMainCanvasSizeTriangles();
}

vec2 mousePosition2DIsoScreenRender() {
    return IRMath::pos2DScreenToPos2DIso(IRRender::getMousePositionOutputView(),
                                         IRRender::getTriangleStepSizeScreen()) -
           (getMainCanvasSizeTrixels()) / getCameraZoom() / vec2(2.0f);
}

vec2 mousePosition2DIsoWorldRender() {
    return mousePosition2DIsoScreenRender() - IRRender::getCameraPosition2DIso();
}

ivec2 mouseTrixelPositionWorld() {
    const ivec2 canvasSize = getRenderManager().getMainCanvasSizeTriangles();
    const ivec2 z1 = IRMath::trixelOriginOffsetZ1(canvasSize);
    const int shaderMod = (z1.x + z1.y) & 1;

    const int subdivisions = getVoxelRenderEffectiveSubdivisions();
    const vec2 scaledMousePos =
        IRRender::mousePosition2DIsoWorldRender() * static_cast<float>(subdivisions);

    const vec2 triIndex =
        IRMath::pos2DIsoToTriangleIndex(scaledMousePos, shaderMod);
    // Use floor() not truncation: truncation gives 0 for -0.5..0, breaking negative x coords
    return ivec2(glm::floor(triIndex + vec2(1, 1)));
}

IREntity::EntityId getEntityIdAtMouseTrixel() {
    auto *buf = IRRender::getNamedResource<Buffer>("HoveredEntityIdBuffer");
    if (!buf) return IREntity::kNullEntity;

    uvec2 packed{0u, 0u};
    buf->getSubData(0, sizeof(uvec2), &packed);

    return static_cast<IREntity::EntityId>(packed.x) |
           (static_cast<IREntity::EntityId>(packed.y) << 32);
}

void setCameraZoom(float zoom) {
    getRenderManager().setCameraZoom(zoom);
}

void setCameraPosition2DIso(vec2 pos) {
    getRenderManager().setCameraPosition2DIso(pos);
}

void setVoxelRenderMode(VoxelRenderMode mode) {
    getRenderManager().setVoxelRenderMode(mode);
}

VoxelRenderMode getVoxelRenderMode() {
    return getRenderManager().getVoxelRenderMode();
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

} // namespace IRRender

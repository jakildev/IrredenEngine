#include <irreden/ir_render.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_input.hpp>

#include <irreden/render/rendering_rm.hpp>

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
    return IRInput::getMousePositionRender() - getRenderManager().screenToOutputWindowOffset();
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
           (getMainCanvasSizeTrixels() - vec2(2, 2) // TODO: why this needed here???
            ) / getCameraZoom() /
               vec2(2.0f);
}

vec2 mousePosition2DIsoWorldRender() {
    return mousePosition2DIsoScreenRender() - IRRender::getCameraPosition2DIso();
}

ivec2 mouseTrixelPositionWorld() {
    return glm::floor(IRMath::pos2DIsoToTriangleIndex(
        IRRender::mousePosition2DIsoWorldRender(),
        ivec2(1, 0) // TODO: just a fix to get triangle index to line up for now
        ));
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

} // namespace IRRender

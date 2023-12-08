/*
 * Project: Irreden Engine
 * File: ir_render.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_render.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_input.hpp>

#include <irreden/render/rendering_rm.hpp>

namespace IRRender {


    RenderingResourceManager* g_renderingResourceManager = nullptr;
    RenderingResourceManager& getRenderingResourceManager() {
        IR_ASSERT(
            g_renderingResourceManager != nullptr,
            "RenderingResourceManager not initalized"
        );
        return *g_renderingResourceManager;
    }

    RenderManager* g_renderManager = nullptr;
    RenderManager& getRenderManager() {
        IR_ASSERT(
            g_renderManager != nullptr,
            "RenderManager not initalized"
        );
        return *g_renderManager;
    }

    vec2 getCameraPositionScreen() {
        return getRenderManager().getCameraPositionScreen();
    }
    vec2 getCameraOffset2DIso() {
        return getRenderManager().getCameraOffset2DIso();
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
    int getOutputScaleFactor() {
        return getRenderManager().getOutputScaleFactor();
    }
    vec2 getMousePositionOutputView() {
        return IRInput::getMousePositionRender() -
            getRenderManager().screenToOutputWindowOffset();
    }

    vec2 mousePositionScreenToMainCanvasTriangleIndex(
        const vec2& mousePositionOutputWindow
    )
    {
        vec2 mousePositionMainCanvas =
            IRMath::pos2DScreenToPos2DIso(
                mousePositionOutputWindow,
                IRRender::getTriangleStepSizeScreen()
            );

        mousePositionMainCanvas -=
            IRRender::getCameraOffset2DIso();
        return mousePositionMainCanvas;
        // return IRMath::pos2DIsoToTriangleIndex(
        //     mousePositionMainCanvas
        // );
    }


} // namespace IRRender


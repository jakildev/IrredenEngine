#include <irreden/ir_render.hpp>

#include <irreden/ir_window.hpp>

#include <irreden/render/render_manager.hpp>
#include <irreden/render/ir_gl_api.hpp>

#include <irreden/render/entities/entity_voxel_pool_canvas.hpp>
#include <irreden/render/entities/entity_trixel_canvas.hpp>
#include <irreden/render/entities/entity_framebuffer.hpp>

#include <irreden/render/components/component_texture_scroll.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_2d_iso.hpp>
#include <irreden/common/components/component_size_triangles.hpp>
#include <irreden/update/components/component_velocity_2d_iso.hpp>
#include <irreden/render/components/component_camera.hpp>

#include <algorithm>
#include <cmath>

namespace IRRender {

RenderManager::RenderManager(
        ivec2 gameResolution,
        FitMode fitMode
    )
    :   m_globalConstantsGLSL{

        }
    ,   m_bufferUniformConstantsGLSL{
            &m_globalConstantsGLSL,
            sizeof(GlobalConstantsGLSL),
            GL_NONE,
            GL_UNIFORM_BUFFER,
            kBufferIndex_GlobalConstantsGLSL
        }
    ,   m_mainFramebuffer{
            IREntity::createEntity<kFramebuffer>(
                "mainFramebuffer",
                gameResolution,
                IRConstants::kSizeExtraPixelBuffer
            )
        }
    ,   m_mainCanvas{
            IREntity::createEntity<kVoxelPoolCanvas>(
                "main",
                IRConstants::kVoxelPoolSize,
                IRMath::gameResolutionToSize2DIso(
                    gameResolution + IRConstants::kSizeExtraPixelBuffer
                ),
                m_mainFramebuffer
            )
        }
    ,   m_backgroundCanvas{
            IREntity::createEntity<kTrixelCanvas>(
                "background",
                IREntity::getComponent<C_SizeTriangles>(m_mainCanvas).size_,
                m_mainFramebuffer
            )
        }
    ,   m_guiCanvas{
            IREntity::createEntity<kTrixelCanvas>(
                "gui",
                IREntity::getComponent<C_SizeTriangles>(m_mainCanvas).size_ / ivec2(m_guiScale),
                m_mainFramebuffer
            )
        }
    // ,   m_playerCanvas{
    //         IRECS::createEntity<kVoxelPoolCanvas>(
    //             "player",
    //             ivec2(
    //                 IRConstants::kScreenTrixelMaxCanvasSizeWithBuffer
    //             ),
    //             IRConstants::kVoxelPoolSize / ivec3(2),
    //             IRConstants::kGameResolution,
    //             IRConstants::kSizeExtraPixelBuffer
    //         )
    //     }
    ,   m_camera{
            createEntity(
                C_Camera{},
                C_Position2DIso{
                    vec2(0.0f, 0.0f)
                },
                C_Velocity2DIso{
                    vec2(0.0f, 0.0f)
                },
                C_ZoomLevel{1.0f}
            )
        }
    ,   m_viewport{0}
    ,   m_gameResolution{gameResolution}
    ,   m_outputResolution{0}
    ,   m_fitMode{fitMode}
    // ,   m_bufferVoxelPositions{
    //         nullptr,
    //         IRConstants::kMaxSingleVoxels * sizeof(C_Position3D),
    //         GL_DYNAMIC_STORAGE_BIT,
    //         GL_SHADER_STORAGE_BUFFER,
    //         kBufferIndex_SingleVoxelPositions
    //     }
    // ,   m_bufferVoxelColors{
    //         nullptr,
    //         IRConstants::kMaxSingleVoxels * sizeof(C_Voxel),
    //         GL_DYNAMIC_STORAGE_BIT,
    //         GL_SHADER_STORAGE_BUFFER,
    //         kBufferIndex_SingleVoxelColors
    //     }
    ,   m_renderImpl{createRenderer()}
    {
    IRE_LOG_INFO("Fit mode: {}", static_cast<int>(fitMode));
    m_renderImpl->init();
    IREntity::setName(m_camera, "camera");
    // IRECS::setComponent(
    //     m_backgroundCanvas,
    //     C_TextureScrollPosition{
    //         vec2(0.0f)
    //     }
    // );
    // IRECS::setComponent(
    //     m_backgroundCanvas,
    //     C_TextureScrollVelocity{}
    // );
    // IRECS::setComponent(
    //     m_backgroundCanvas,
    //     C_TriangleCanvasBackground{
    //         BackgroundTypes::kGradientRandom,
    //         colorPalette,
    //         ivec2(IRConstants::kScreenTrixelMaxCanvasSize) /
    //             ivec2(2)
    //     }
    // );
    // m_canvasMap["background"] = m_backgroundCanvas;
    m_canvasMap["background"] = m_backgroundCanvas;
    m_canvasMap["main"] = m_mainCanvas;
    m_canvasMap["gui"] = m_guiCanvas;
    // m_canvasMap["player"] = m_playerCanvas;

    IREntity::setComponent(m_guiCanvas, C_TrixelCanvasRenderBehavior{
        false,  // useCameraPositionIso
        false,  // useCameraZoom
        false,  // applyRenderSubdivisions
        false,  // mouseHoverEnabled
        false,  // usePixelPerfectCameraOffset
        0.0f,   // parityOffsetIsoX
        0.0f,   // parityOffsetIsoY
        0.0f,   // staticPixelOffsetX
        0.0f    // staticPixelOffsetY
    });

    initRenderingResources();
    initRenderingSystems();
    g_renderManager = this;
    IRE_LOG_INFO("Created renderer.");
}

RenderManager::~RenderManager() {
    if (g_renderManager == this) {
        g_renderManager = nullptr;
    }
}

void RenderManager::tick() {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

    IRWindow::getWindowSize(m_viewport);
    updateOutputResolution();
    IRSystem::executePipeline(IRTime::Events::RENDER);
    IRWindow::getWindow().swapBuffers();
}

std::tuple<
    std::span<C_Position3D>,
    std::span<C_PositionOffset3D>,
    std::span<C_PositionGlobal3D>,
    std::span<C_Voxel>>
RenderManager::allocateVoxels(unsigned int numVoxels, std::string canvasName) {
    if (canvasName == "main") {
        return IREntity::getComponent<C_VoxelPool>(m_mainCanvas).allocateVoxels(numVoxels);
    }

    IR_ASSERT(false, "only allocating main voxels for now.");
    return std::make_tuple(
        std::span<C_Position3D>{},
        std::span<C_PositionOffset3D>{},
        std::span<C_PositionGlobal3D>{},
        std::span<C_Voxel>{}
    );
}

vec2 RenderManager::getCameraPosition2DIso() const {
    return IREntity::getComponent<C_Position2DIso>(m_camera).pos_;
}

vec2 RenderManager::getTriangleStepSizeScreen() const {
    return IRMath::calcTriangleStepSizeScreen(
        m_gameResolution,
        getCameraZoom(),
        m_outputScaleFactor
    );
}

vec2 RenderManager::getTriangleStepSizeGameResolution() const {
    return IRMath::calcTriangleStepSizeGameResolution(m_gameResolution, getCameraZoom());
}

vec2 RenderManager::getCameraZoom() const {
    return IREntity::getComponent<C_ZoomLevel>(m_camera).zoom_;
}

void RenderManager::setVoxelRenderMode(VoxelRenderMode mode) {
    m_voxelRenderMode = mode;
}

VoxelRenderMode RenderManager::getVoxelRenderMode() const {
    return m_voxelRenderMode;
}

void RenderManager::setVoxelRenderSubdivisions(int subdivisions) {
    m_voxelRenderSubdivisions = IRMath::max(1, subdivisions);
}

int RenderManager::getVoxelRenderSubdivisions() const {
    return m_voxelRenderSubdivisions;
}

int RenderManager::getVoxelRenderEffectiveSubdivisions() const {
    if (m_voxelRenderMode == VoxelRenderMode::SNAPPED) {
        return 1;
    }
    const int zoomScale =
        static_cast<int>(IRMath::round(IRMath::max(getCameraZoom().x, getCameraZoom().y)));
    return IRMath::clamp(m_voxelRenderSubdivisions * IRMath::max(1, zoomScale), 1, 16);
}

void RenderManager::setCameraZoom(float zoom) {
    float clamped = IRMath::clamp(
        zoom,
        IRConstants::kTrixelCanvasZoomMin.x,
        IRConstants::kTrixelCanvasZoomMax.x
    );
    float snapped = std::pow(2.0f, std::round(std::log2(clamped)));
    IREntity::setComponent(m_camera, C_ZoomLevel{snapped});
}

void RenderManager::setCameraPosition2DIso(vec2 pos) {
    IREntity::setComponent(m_camera, C_Position2DIso{pos});
    IREntity::setComponent(m_camera, C_Velocity2DIso{vec2(0.0f, 0.0f)});
}

void RenderManager::zoomMainBackgroundPatternIn() {
    auto zoomLevel = IREntity::getComponentOptional<C_ZoomLevel>(m_backgroundCanvas);
    if (!zoomLevel.has_value()) {
        IREntity::setComponent(m_backgroundCanvas, C_ZoomLevel{1.0f});
        zoomLevel = IREntity::getComponentOptional<C_ZoomLevel>(m_backgroundCanvas);
        if (!zoomLevel.has_value()) {
            return;
        }
    }
    (*zoomLevel.value()).zoomIn();
}

void RenderManager::zoomMainBackgroundPatternOut() {
    auto zoomLevel = IREntity::getComponentOptional<C_ZoomLevel>(m_backgroundCanvas);
    if (!zoomLevel.has_value()) {
        IREntity::setComponent(m_backgroundCanvas, C_ZoomLevel{1.0f});
        zoomLevel = IREntity::getComponentOptional<C_ZoomLevel>(m_backgroundCanvas);
        if (!zoomLevel.has_value()) {
            return;
        }
    }
    (*zoomLevel.value()).zoomOut();
}

void RenderManager::deallocateVoxels(
    std::span<C_Position3D> positions,
    std::span<C_PositionOffset3D> positionOffsets,
    std::span<C_PositionGlobal3D> positionGlobals,
    std::span<C_Voxel> voxels,
    std::string canvasName
) {
    if (canvasName == "main") {
        // Hack due to exception on shutdown when deleting entities
        // Need to find a better solution.
        if (!IREntity::entityExists(m_mainCanvas)) {
            return;
        }
        IREntity::getComponent<C_VoxelPool>(m_mainCanvas)
            .deallocateVoxels(positions, positionOffsets, positionGlobals, voxels);
        return;
    }

    IR_ASSERT(false, "only deallocating main voxels for now.");
}

EntityId RenderManager::getCanvas(std::string canvasName) {
    return m_canvasMap[canvasName];
}

ivec2 RenderManager::getMainCanvasSizeTriangles() const {
    return IREntity::getComponent<C_SizeTriangles>(m_mainCanvas).size_;
}

void RenderManager::initRenderingResources() {
    Buffer *vertexBuffer = IRRender::createResource<Buffer>(
                               IRShapes2D::kQuadVertices,
                               sizeof(IRShapes2D::kQuadVertices),
                               0
    )
                               .second;
    Buffer *indexBuffer = IRRender::createResource<Buffer>(
                              IRShapes2D::kQuadIndices,
                              sizeof(IRShapes2D::kQuadIndices),
                              0
    )
                              .second;
    IRRender::createNamedResource<VAO>(
        "QuadVAO",
        vertexBuffer->getHandle(),
        indexBuffer->getHandle(),
        1,
        &kAttrFloat2
    );

    Buffer *vertexBufferTextured = IRRender::createResource<Buffer>(
                                       IRShapes2D::k2DQuadTextured,
                                       sizeof(IRShapes2D::k2DQuadTextured),
                                       0
    )
                                       .second;
    IRRender::createNamedResource<VAO>(
        "QuadVAOArrays",
        vertexBufferTextured->getHandle(),
        0,
        2,
        kAttrList2Float2
    );
}

void RenderManager::initRenderingSystems() {}

void RenderManager::printRenderInfo() {
    m_renderImpl->printInfo();
}

ivec2 RenderManager::calcOutputScaleByMode() {
    if (m_fitMode == FitMode::FIT) {
        return ivec2(
            IRMath::min(
                IRMath::floor(m_viewport.x / m_gameResolution.x),
                IRMath::floor(m_viewport.y / m_gameResolution.y)
            )
        );
    }
    if (m_fitMode == FitMode::STRETCH) {
        return ivec2(
            IRMath::floor(m_viewport.x / m_gameResolution.x),
            IRMath::floor(m_viewport.y / m_gameResolution.y)
        );
    }
    IR_ASSERT(false, "Unexpected FitMode type");
    return ivec2(1);
}

void RenderManager::updateOutputResolution() {
    m_outputScaleFactor = calcOutputScaleByMode();
    m_outputResolution = ivec2(
        m_gameResolution.x * m_outputScaleFactor.x,
        m_gameResolution.y * m_outputScaleFactor.y
    );
}

vec2 RenderManager::screenToOutputWindowOffset() const {
    return vec2(m_viewport.x - m_outputResolution.x, m_viewport.y - m_outputResolution.y) / vec2(2);
}

void RenderManager::setGuiVisible(bool visible) {
    m_guiVisible = visible;
}

void RenderManager::toggleGuiVisible() {
    m_guiVisible = !m_guiVisible;
    IRE_LOG_INFO("GUI overlay {}", m_guiVisible ? "enabled" : "disabled");
}

bool RenderManager::isGuiVisible() const {
    return m_guiVisible;
}

void RenderManager::setGuiScale(int scale) {
    scale = std::clamp(scale, 1, 8);
    if (scale == m_guiScale) return;
    m_guiScale = scale;

    ivec2 mainSize = IREntity::getComponent<C_SizeTriangles>(m_mainCanvas).size_;
    ivec2 newSize = mainSize / ivec2(scale);

    auto &textures = IREntity::getComponent<C_TriangleCanvasTextures>(m_guiCanvas);
    textures.onDestroy();
    textures = C_TriangleCanvasTextures{newSize};

    IREntity::getComponent<C_SizeTriangles>(m_guiCanvas).size_ = newSize;

    IRE_LOG_INFO("GUI scale set to {}x (canvas {}x{})", scale, newSize.x, newSize.y);
}

int RenderManager::getGuiScale() const {
    return m_guiScale;
}

void RenderManager::setHoveredTrixelVisible(bool visible) {
    m_hoveredTrixelVisible = visible;
}

bool RenderManager::isHoveredTrixelVisible() const {
    return m_hoveredTrixelVisible;
}

} // namespace IRRender
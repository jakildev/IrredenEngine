#include <irreden/ir_render.hpp>

#include <irreden/ir_window.hpp>

#include <irreden/render/render_manager.hpp>
#include <irreden/render/vao.hpp>
#include <irreden/render/vertex_attributes.hpp>

#include <irreden/render/entities/entity_voxel_pool_canvas.hpp>
#include <irreden/render/entities/entity_trixel_canvas.hpp>
#include <irreden/render/entities/entity_framebuffer.hpp>

#include <irreden/render/components/component_texture_scroll.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>

#include <irreden/common/components/component_position_2d_iso.hpp>
#include <irreden/common/components/component_size_triangles.hpp>
#include <irreden/common/components/component_persistent.hpp>
#include <irreden/update/components/component_velocity_2d_iso.hpp>
#include <irreden/render/components/component_camera.hpp>

#include <algorithm>
#include <cmath>

namespace IRRender {

RenderManager::RenderManager(
        ivec2 gameResolution,
        FitMode fitMode
    )
    :   m_renderImpl{createRenderer()}
    ,   m_globalConstantsGLSL{

        }
    ,   m_bufferUniformConstantsGLSL{
            &m_globalConstantsGLSL,
            sizeof(GlobalConstantsGLSL),
            BUFFER_STORAGE_NONE,
            BufferTarget::UNIFORM,
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
                IRRender::VoxelPoolConfig::getSize(),
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
    {
    IRE_LOG_INFO("Fit mode: {}", static_cast<int>(fitMode));
    IRE_LOG_INFO(
        "Voxel pool: edge={} ({} voxels)",
        IRRender::VoxelPoolConfig::getEdge(),
        IRRender::VoxelPoolConfig::getTotalSize()
    );
    m_renderImpl->init();
    IREntity::setName(m_camera, "camera");

    // #1814: the renderer's camera + framebuffer/canvas entities are normal
    // (non-singleton) ECS entities. Tag them C_Persistent so a scene-transition
    // IREntity::resetGameplay() spares them — without the tag they'd be torn
    // down on the first reset and the render context would break.
    IREntity::setComponent(m_camera, C_Persistent{});
    IREntity::setComponent(m_mainFramebuffer, C_Persistent{});
    IREntity::setComponent(m_mainCanvas, C_Persistent{});
    IREntity::setComponent(m_backgroundCanvas, C_Persistent{});
    IREntity::setComponent(m_guiCanvas, C_Persistent{});
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

    IREntity::setComponent(
        m_guiCanvas,
        C_TrixelCanvasRenderBehavior{
            false, // useCameraPositionIso
            false, // useCameraZoom
            false, // applyRenderSubdivisions
            false, // mouseHoverEnabled
            false, // usePixelPerfectCameraOffset
            0.0f,  // parityOffsetIsoX
            0.0f,  // parityOffsetIsoY
            0.0f,  // staticPixelOffsetX
            0.0f   // staticPixelOffsetY
        }
    );

    m_activeCanvas = m_mainCanvas;

    // The main world canvas's per-axis trixel canvases (smooth camera Z-yaw,
    // #1308; docs/design/per-axis-trixel-canvas-rotation.md) are bundled on
    // every voxel-pool canvas by Prefab<kVoxelPoolCanvas>. Their GPU textures
    // stay allocated lazily — the main canvas's only while the camera sits at a
    // non-cardinal residual yaw (syncAllocationToCameraYaw) — so a static /
    // cardinal scene pays nothing (the byte-identical fast path). Detached
    // entities no longer use the per-axis machinery: detached SO(3) renders
    // through the re-voxelize path (#1555–#1560), not per-axis forward-scatter.

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

void RenderManager::beginFrame() {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

    IRWindow::getWindowSize(m_viewport);
    updateOutputResolution();
    IRRender::device()->beginFrame();
}

void RenderManager::renderFrame() {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

    IRSystem::executePipeline(IRTime::Events::RENDER);
}

void RenderManager::presentFrame() {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

    IRRender::device()->present();
}

VoxelPoolAllocation RenderManager::allocateVoxels(unsigned int numVoxels, std::string canvasName) {
    auto it = m_canvasMap.find(canvasName);
    IR_ASSERT(it != m_canvasMap.end(), "Canvas not found: {}", canvasName);
    auto poolOpt = IREntity::getComponentOptional<C_VoxelPool>(it->second);
    IR_ASSERT(poolOpt.has_value(), "Canvas has no VoxelPool: {}", canvasName);
    return (*poolOpt.value()).allocateVoxels(numVoxels);
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

void RenderManager::setSubdivisionMode(SubdivisionMode mode) {
    m_subdivisionMode = mode;
}

SubdivisionMode RenderManager::getSubdivisionMode() const {
    return m_subdivisionMode;
}

void RenderManager::setRotationPivotMode(RotationPivotMode mode) {
    m_rotationPivotMode = mode;
}

RotationPivotMode RenderManager::getRotationPivotMode() const {
    return m_rotationPivotMode;
}

void RenderManager::setRotationPivotFocus(vec3 focusWorld) {
    m_rotationPivotFocus = focusWorld;
    m_hasRotationPivotFocus = true;
}

void RenderManager::clearRotationPivotFocus() {
    m_hasRotationPivotFocus = false;
}

bool RenderManager::hasRotationPivotFocus() const {
    return m_hasRotationPivotFocus;
}

vec3 RenderManager::getRotationPivotFocus() const {
    return m_rotationPivotFocus;
}

void RenderManager::setVoxelRenderSubdivisions(int subdivisions) {
    m_voxelRenderSubdivisions = IRMath::max(1, subdivisions);
}

int RenderManager::getVoxelRenderSubdivisions() const {
    return m_voxelRenderSubdivisions;
}

int RenderManager::getVoxelRenderEffectiveSubdivisions() const {
    switch (m_subdivisionMode) {
    case SubdivisionMode::NONE:
        return 1;
    case SubdivisionMode::POSITION_ONLY:
        return IRMath::clamp(m_voxelRenderSubdivisions, 1, 16);
    case SubdivisionMode::FULL: {
        const int zoomScale =
            static_cast<int>(IRMath::round(IRMath::max(getCameraZoom().x, getCameraZoom().y)));
        return IRMath::clamp(m_voxelRenderSubdivisions * IRMath::max(1, zoomScale), 1, 16);
    }
    }
    return 1;
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

void RenderManager::deallocateVoxels(size_t startIndex, size_t size, std::string canvasName) {
    auto it = m_canvasMap.find(canvasName);
    if (it == m_canvasMap.end()) {
        return;
    }
    if (!IREntity::entityExists(it->second)) {
        return;
    }
    auto poolOpt = IREntity::getComponentOptional<C_VoxelPool>(it->second);
    if (!poolOpt.has_value()) {
        return;
    }
    (*poolOpt.value()).deallocateVoxels(startIndex, size);
}

namespace {
// Shared lookup for the active-mask routes below — same shape as
// deallocateVoxels but returning the pool so the call site can route
// to whichever mask helper it needs.
C_VoxelPool *lookupPoolForMaskOp(
    const std::unordered_map<std::string, EntityId> &canvasMap, const std::string &canvasName
) {
    auto it = canvasMap.find(canvasName);
    if (it == canvasMap.end() || !IREntity::entityExists(it->second)) {
        return nullptr;
    }
    auto poolOpt = IREntity::getComponentOptional<C_VoxelPool>(it->second);
    if (!poolOpt.has_value()) {
        return nullptr;
    }
    return &(*poolOpt.value());
}
} // namespace

void RenderManager::markVoxelPoolRangeActive(
    size_t startIndex, size_t count, std::string canvasName
) {
    if (auto *pool = lookupPoolForMaskOp(m_canvasMap, canvasName)) {
        pool->setActiveMaskRange(startIndex, count);
    }
}

void RenderManager::markVoxelPoolRangeInactive(
    size_t startIndex, size_t count, std::string canvasName
) {
    if (auto *pool = lookupPoolForMaskOp(m_canvasMap, canvasName)) {
        pool->clearActiveMaskRange(startIndex, count);
    }
}

void RenderManager::markVoxelPoolVoxelActive(
    size_t voxelIndex, bool active, std::string canvasName
) {
    if (auto *pool = lookupPoolForMaskOp(m_canvasMap, canvasName)) {
        if (active) {
            pool->setActiveBit(voxelIndex);
        } else {
            pool->clearActiveBit(voxelIndex);
        }
    }
}

void RenderManager::resyncVoxelPoolRangeFromColors(
    size_t startIndex, size_t count, std::string canvasName
) {
    if (auto *pool = lookupPoolForMaskOp(m_canvasMap, canvasName)) {
        pool->resyncActiveMaskFromColors(startIndex, count);
    }
}

EntityId RenderManager::getCanvas(std::string canvasName) {
    return m_canvasMap[canvasName];
}

EntityId RenderManager::createCanvas(
    std::string name, ivec3 voxelPoolSize, ivec2 trixelSize, EntityId framebuffer
) {
    EntityId fb = framebuffer;
    if (fb == EntityId{}) {
        fb = m_mainFramebuffer;
    }
    EntityId canvas = IREntity::createEntity<kVoxelPoolCanvas>(name, voxelPoolSize, trixelSize, fb);
    m_canvasMap[name] = canvas;
    return canvas;
}

bool RenderManager::hasCanvas(const std::string &name) const {
    return m_canvasMap.find(name) != m_canvasMap.end();
}

void RenderManager::setActiveCanvas(const std::string &name) {
    auto it = m_canvasMap.find(name);
    IR_ASSERT(it != m_canvasMap.end(), "Canvas not found: {}", name);
    m_activeCanvas = it->second;
}

EntityId RenderManager::getActiveCanvasEntity() const {
    return m_activeCanvas;
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
    IRRender::createNamedResource<VAO>("QuadVAO", vertexBuffer, indexBuffer, 1, &kAttrFloat2);

    Buffer *vertexBufferTextured = IRRender::createResource<Buffer>(
                                       IRShapes2D::k2DQuadTextured,
                                       sizeof(IRShapes2D::k2DQuadTextured),
                                       0
    )
                                       .second;
    IRRender::createNamedResource<VAO>(
        "QuadVAOArrays",
        vertexBufferTextured,
        static_cast<const Buffer *>(nullptr),
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
        return IRMath::max(
            ivec2(
                IRMath::min(
                    IRMath::floor(m_viewport.x / m_gameResolution.x),
                    IRMath::floor(m_viewport.y / m_gameResolution.y)
                )
            ),
            ivec2(1)
        );
    }
    if (m_fitMode == FitMode::STRETCH) {
        return IRMath::max(
            ivec2(
                IRMath::floor(m_viewport.x / m_gameResolution.x),
                IRMath::floor(m_viewport.y / m_gameResolution.y)
            ),
            ivec2(1)
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

void RenderManager::resizeGuiCanvas(ivec2 newSize) {
    if (IREntity::getComponent<C_SizeTriangles>(m_guiCanvas).size_ == newSize)
        return;
    auto &textures = IREntity::getComponent<C_TriangleCanvasTextures>(m_guiCanvas);
    textures.onDestroy();
    textures = C_TriangleCanvasTextures{newSize};
    IREntity::getComponent<C_SizeTriangles>(m_guiCanvas).size_ = newSize;
}

void RenderManager::setGuiScale(int scale) {
    scale = std::clamp(scale, 1, 8);
    if (scale == m_guiScale)
        return;
    if (m_guiFullResolution)
        return; // full-resolution sizing overrides the scale divisor; don't record the intent
    m_guiScale = scale;

    ivec2 mainSize = IREntity::getComponent<C_SizeTriangles>(m_mainCanvas).size_;
    ivec2 newSize = mainSize / ivec2(scale);
    resizeGuiCanvas(newSize);
    IRE_LOG_INFO("GUI scale set to {}x (canvas {}x{})", scale, newSize.x, newSize.y);
}

int RenderManager::getGuiScale() const {
    return m_guiScale;
}

void RenderManager::setGuiCanvasFullResolution() {
    m_guiFullResolution = true;
    // The main framebuffer is sized gameResolution + buffer pixels; matching it
    // makes one GUI trixel == one framebuffer pixel (1:1, no iso stretch).
    const ivec2 fbSize = m_gameResolution + IRConstants::kSizeExtraPixelBuffer;
    resizeGuiCanvas(fbSize);
    IRE_LOG_INFO("GUI canvas set to full resolution ({}x{})", fbSize.x, fbSize.y);
}

void RenderManager::setHoveredTrixelVisible(bool visible) {
    m_hoveredTrixelVisible = visible;
}

bool RenderManager::isHoveredTrixelVisible() const {
    return m_hoveredTrixelVisible;
}

void RenderManager::setSunDirection(vec3 dir) {
    const float len = glm::length(dir);
    m_sunDirection = len > 0.0f ? dir / len : vec3(-0.3f, -0.2f, -0.93f);
}

vec3 RenderManager::getSunDirection() const {
    return m_sunDirection;
}

void RenderManager::setSunIntensity(float intensity) {
    m_sunIntensity = IRMath::max(0.0f, intensity);
}

float RenderManager::getSunIntensity() const {
    return m_sunIntensity;
}

void RenderManager::setSunAmbient(float ambient) {
    m_sunAmbient = IRMath::clamp(ambient, 0.0f, 1.0f);
}

float RenderManager::getSunAmbient() const {
    return m_sunAmbient;
}

void RenderManager::setSunShadowsEnabled(bool enabled) {
    m_sunShadowsEnabled = enabled;
}

bool RenderManager::getSunShadowsEnabled() const {
    return m_sunShadowsEnabled;
}

void RenderManager::setAOEnabled(bool enabled) {
    m_aoEnabled = enabled;
}

bool RenderManager::getAOEnabled() const {
    return m_aoEnabled;
}

void RenderManager::setVoxelOcclusionCullEnabled(bool enabled) {
    m_voxelOcclusionCullEnabled = enabled;
}

bool RenderManager::getVoxelOcclusionCullEnabled() const {
    return m_voxelOcclusionCullEnabled;
}

void RenderManager::setVoxelPerVoxelOcclusionEnabled(bool enabled) {
    m_voxelPerVoxelOcclusionEnabled = enabled;
}

bool RenderManager::getVoxelPerVoxelOcclusionEnabled() const {
    return m_voxelPerVoxelOcclusionEnabled;
}

void RenderManager::setDebugOverlay(DebugOverlayMode mode) {
    m_debugOverlayMode = mode;
}

DebugOverlayMode RenderManager::getDebugOverlay() const {
    return m_debugOverlayMode;
}

void RenderManager::setDepthColorDebug(bool on, float extent) {
    m_depthColorDebugOn = on;
    m_depthColorDebugExtent = extent;
}

bool RenderManager::getDepthColorDebugMode() const {
    return m_depthColorDebugOn;
}

float RenderManager::getDepthColorDebugExtent() const {
    return m_depthColorDebugExtent;
}

void RenderManager::setHDREnabled(bool enabled) {
    m_hdrEnabled = enabled;
}

bool RenderManager::getHDREnabled() const {
    return m_hdrEnabled;
}

void RenderManager::setExposure(float exposure) {
    m_hdrExposure = IRMath::max(0.0f, exposure);
}

float RenderManager::getExposure() const {
    return m_hdrExposure;
}

void RenderManager::setSkyIntensity(float intensity) {
    m_skyIntensity = IRMath::max(0.0f, intensity);
}

float RenderManager::getSkyIntensity() const {
    return m_skyIntensity;
}

void RenderManager::setSkyColor(vec3 color) {
    m_skyColor = color;
}

vec3 RenderManager::getSkyColor() const {
    return m_skyColor;
}

} // namespace IRRender

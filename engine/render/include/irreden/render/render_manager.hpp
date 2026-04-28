#ifndef RENDER_MANAGER_H
#define RENDER_MANAGER_H

// #include <irreden/ir_input.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/buffer.hpp>
#include <irreden/render/renderer_impl.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_offset_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/voxel/components/component_voxel.hpp>

#include <tuple>
#include <string>
#include <span>

using namespace IRComponents;
using namespace IREntity;

namespace IRRender {

class RenderManager {
  public:
    RenderManager(ivec2 gameResolution, FitMode fitMode = FitMode::FIT);
    ~RenderManager();

    inline ivec2 getViewport() const {
        return m_viewport;
    }
    inline ivec2 getGameResolution() const {
        return m_gameResolution;
    }
    inline ivec2 getOutputResolution() const {
        return m_outputResolution;
    }
    inline ivec2 getOutputScaleFactor() const {
        return m_outputScaleFactor;
    }

    EntityId getCanvas(std::string canvasName);
    vec2 getCameraPosition2DIso() const;
    vec2 getCameraZoom() const;
    vec2 getTriangleStepSizeScreen() const;
    vec2 getTriangleStepSizeGameResolution() const;
    ivec2 getMainCanvasSizeTriangles() const;
    vec2 screenToOutputWindowOffset() const;
    void setSubdivisionMode(SubdivisionMode mode);
    SubdivisionMode getSubdivisionMode() const;
    void setVoxelRenderSubdivisions(int subdivisions);
    int getVoxelRenderSubdivisions() const;
    int getVoxelRenderEffectiveSubdivisions() const;
    void setCameraZoom(float zoom);
    void setCameraPosition2DIso(vec2 pos);
    void zoomMainBackgroundPatternIn();
    void zoomMainBackgroundPatternOut();

    void setGuiVisible(bool visible);
    void toggleGuiVisible();
    bool isGuiVisible() const;

    void setGuiScale(int scale);
    int getGuiScale() const;

    void setHoveredTrixelVisible(bool visible);
    bool isHoveredTrixelVisible() const;

    void setSunDirection(vec3 dir);
    vec3 getSunDirection() const;
    void setSunIntensity(float intensity);
    float getSunIntensity() const;
    void setSunAmbient(float ambient);
    float getSunAmbient() const;
    void setSunShadowsEnabled(bool enabled);
    bool getSunShadowsEnabled() const;

    void setDebugOverlay(DebugOverlayMode mode);
    DebugOverlayMode getDebugOverlay() const;

    void beginFrame();
    void renderFrame();
    void presentFrame();
    void printRenderInfo();

    std::tuple<
        std::span<C_Position3D>,
        std::span<C_PositionOffset3D>,
        std::span<C_PositionGlobal3D>,
        std::span<C_Voxel>>
    allocateVoxels(unsigned int size, std::string canvasName = "main");

    void deallocateVoxels(
        std::span<C_Position3D> positions,
        std::span<C_PositionOffset3D> positionsOffset,
        std::span<C_PositionGlobal3D> positionsGlobal,
        std::span<C_Voxel> voxels,
        std::string canvasName = "main"
    );

    EntityId createCanvas(
        std::string name, ivec3 voxelPoolSize, ivec2 trixelSize, EntityId framebuffer = EntityId{}
    );

    bool hasCanvas(const std::string &name) const;

    void setActiveCanvas(const std::string &name);
    EntityId getActiveCanvasEntity() const;

  private:
    std::unique_ptr<RenderImpl> m_renderImpl;
    // tmp
    GlobalConstantsGLSL m_globalConstantsGLSL;
    Buffer m_bufferUniformConstantsGLSL;
    // Buffer m_bufferVoxelPositions;
    // Buffer m_bufferVoxelColors;
    EntityId m_mainFramebuffer;
    EntityId m_mainCanvas;
    EntityId m_backgroundCanvas;
    int m_guiScale = 2;
    EntityId m_guiCanvas;
    // EntityId m_playerCanvas;
    EntityId m_camera;
    ivec2 m_viewport;
    ivec2 m_gameResolution;
    ivec2 m_outputResolution;
    ivec2 m_outputScaleFactor;
    std::unordered_map<std::string, EntityId> m_canvasMap;
    EntityId m_activeCanvas = kNullEntity;
    FitMode m_fitMode;
    SubdivisionMode m_subdivisionMode = SubdivisionMode::FULL;
    bool m_hoveredTrixelVisible = true;
    int m_voxelRenderSubdivisions = 1;
    bool m_guiVisible = false;
    // Unit vector pointing from surfaces toward the sun. Default is a
    // mostly-overhead pose with a small +X / +Y tilt so the three voxel
    // faces shade in the order Z > X > Y, recovering the visual feel of
    // the old hardcoded per-face brightness multiplier (Z=1.25, X=1.0,
    // Y=0.75). Creations override via setSunDirection() per frame or at
    // init. Consumed by the COMPUTE_SUN_SHADOW pass each frame.
    // Component breakdown after normalize: |z|≈0.93 (top brightest),
    // x≈0.30, y≈0.20 — every face has at least 0.4 ambient floor.
    vec3 m_sunDirection = vec3(0.3f, 0.2f, -0.93f);
    float m_sunIntensity = 1.0f;
    float m_sunAmbient = 0.4f;
    bool m_sunShadowsEnabled = true;
    DebugOverlayMode m_debugOverlayMode = DebugOverlayMode::NONE;

    void initRenderingSystems();
    void initRenderingResources();
    void updateOutputResolution();

    ivec2 calcOutputScaleByMode();
};

} // namespace IRRender

#endif /* RENDER_MANAGER_H */

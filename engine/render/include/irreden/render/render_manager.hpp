#ifndef RENDER_MANAGER_H
#define RENDER_MANAGER_H

// #include <irreden/ir_input.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/buffer.hpp>
#include <irreden/render/renderer_impl.hpp>
#include <irreden/render/voxel_pool_allocation.hpp>

#include <irreden/voxel/components/component_voxel.hpp>

#include <string>

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
    void setRotationPivotMode(RotationPivotMode mode);
    RotationPivotMode getRotationPivotMode() const;
    // Explicit world-space point of interest to rotate the camera Z-yaw about
    // (#1921). When set, CAMERA_CENTER pivots about this point at its true depth
    // — content there rotates in place instead of arcing about the z=0 world
    // point under screen center. When unset, the pivot falls back to that legacy
    // screen-center point (byte-identical to the pre-#1921 path). The choice of
    // focus (cursor / selection / scene centroid) is a creation-level policy;
    // the engine only consumes the point.
    void setRotationPivotFocus(vec3 focusWorld);
    void clearRotationPivotFocus();
    bool hasRotationPivotFocus() const;
    vec3 getRotationPivotFocus() const;
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
    // Opt-in (default off): resize the GUI canvas to the native framebuffer
    // pixel resolution (1 GUI trixel == 1 framebuffer pixel) so GUI text /
    // widgets render small and crisp instead of at the coarse iso-canvas
    // resolution. A creation that enables this owns laying its GUI out for
    // the finer coordinate space. Overrides guiScale-based sizing.
    void setGuiCanvasFullResolution();

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
    void setAOEnabled(bool enabled);
    bool getAOEnabled() const;
    void setVoxelOcclusionCullEnabled(bool enabled);
    bool getVoxelOcclusionCullEnabled() const;
    void setVoxelPerVoxelOcclusionEnabled(bool enabled);
    bool getVoxelPerVoxelOcclusionEnabled() const;

    void setDebugOverlay(DebugOverlayMode mode);
    DebugOverlayMode getDebugOverlay() const;

    void setDepthColorDebug(bool on, float extent);
    bool getDepthColorDebugMode() const;
    float getDepthColorDebugExtent() const;

    void setHDREnabled(bool enabled);
    bool getHDREnabled() const;
    void setExposure(float exposure);
    float getExposure() const;
    void setSkyIntensity(float intensity);
    float getSkyIntensity() const;
    void setSkyColor(vec3 color);
    vec3 getSkyColor() const;

    void beginFrame();
    void renderFrame();
    void presentFrame();
    void printRenderInfo();

    VoxelPoolAllocation allocateVoxels(unsigned int size, std::string canvasName = "main");

    void deallocateVoxels(size_t startIndex, size_t size, std::string canvasName = "main");

    void markVoxelPoolRangeActive(size_t startIndex, size_t count, std::string canvasName = "main");
    void
    markVoxelPoolRangeInactive(size_t startIndex, size_t count, std::string canvasName = "main");
    void markVoxelPoolVoxelActive(size_t voxelIndex, bool active, std::string canvasName = "main");
    void resyncVoxelPoolRangeFromColors(
        size_t startIndex, size_t count, std::string canvasName = "main"
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
    int m_guiScale = 1;
    bool m_guiFullResolution = false;
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
    RotationPivotMode m_rotationPivotMode = RotationPivotMode::CAMERA_CENTER;
    vec3 m_rotationPivotFocus = vec3(0.0f);
    bool m_hasRotationPivotFocus = false;
    bool m_hoveredTrixelVisible = true;
    int m_voxelRenderSubdivisions = 1;
    bool m_guiVisible = false;
    // Unit vector pointing from surfaces toward the sun. Default is a
    // mostly-overhead pose with a small -X / -Y tilt: those are the
    // outward-normal directions of the visible X_FACE / Y_FACE in iso
    // view (see `faceOutwardNormal` in ir_iso_common.glsl), so the dot
    // product produces brightness Z > X > Y, recovering the visual feel
    // of the old hardcoded per-face brightness multiplier (Z=1.25, X=1.0,
    // Y=0.75). Creations override via setSunDirection() per frame or at
    // init. Consumed by the COMPUTE_SUN_SHADOW pass each frame.
    // Component breakdown after normalize: |z|≈0.93 (top brightest),
    // |x|≈0.30, |y|≈0.20 — every face has at least 0.4 ambient floor.
    vec3 m_sunDirection = vec3(-0.3f, -0.2f, -0.93f);
    float m_sunIntensity = 1.0f;
    float m_sunAmbient = 0.4f;
    bool m_sunShadowsEnabled = true;
    bool m_aoEnabled = true;
    // Voxel-pool chunk-occlusion cull (#1294). Off by default — the pre-pass is
    // not dispatched unless this is set, so a default scene is byte-identical to
    // master. The gating heuristic + camera-cut disable land in child 3 (#1800).
    bool m_voxelOcclusionCullEnabled = false;
    // Per-voxel Hi-Z occlusion refine (#1812), layered on the chunk pre-pass
    // above. On by default, but only active when the chunk cull is enabled (the
    // per-voxel test shares getVoxelOcclusionCullEnabled()'s gate). Flip it off
    // (--no-per-voxel-occlusion) to isolate the chunk cull's contribution for the
    // #1812 marginal acceptance gate.
    bool m_voxelPerVoxelOcclusionEnabled = true;
    DebugOverlayMode m_debugOverlayMode = DebugOverlayMode::NONE;
    bool m_depthColorDebugOn = false;
    float m_depthColorDebugExtent = 0.0f;
    bool m_hdrEnabled = false;
    float m_hdrExposure = 1.0f;
    float m_skyIntensity = 0.0f;
    vec3 m_skyColor = vec3(0.5f, 0.7f, 1.0f);

    void initRenderingSystems();
    void initRenderingResources();
    void updateOutputResolution();

    ivec2 calcOutputScaleByMode();

    // Destroy + recreate the GUI canvas textures at `newSize` and update its
    // C_SizeTriangles. Shared by setGuiScale and setGuiCanvasFullResolution.
    void resizeGuiCanvas(ivec2 newSize);
};

} // namespace IRRender

#endif /* RENDER_MANAGER_H */

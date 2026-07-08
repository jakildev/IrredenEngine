#ifndef LIGHTING_DEMO_SCENE_H
#define LIGHTING_DEMO_SCENE_H

#include <irreden/ir_engine.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>

#include <irreden/common/command_suite_capture.hpp>
#include <irreden/render/camera_controls.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/math/sdf.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/systems/system_build_light_occlusion_grid.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>
#include <irreden/render/systems/system_compute_sun_shadow.hpp>
#include <irreden/render/systems/system_compute_voxel_ao.hpp>
#include <irreden/render/systems/system_fog_to_trixel.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_lighting_to_trixel.hpp>
#include <irreden/render/systems/system_perf_stats_overlay.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_text_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <list>
#include <vector>

namespace IRLightingDemo {

using namespace IRComponents;
using namespace IREntity;
using namespace IRMath;

struct DemoConfig {
    const char *name_ = "lighting";
    IRRender::DebugOverlayMode overlay_ = IRRender::DebugOverlayMode::NONE;
    bool addEmissive_ = false;
    bool addPoint_ = false;
    bool addSpot_ = false;
    bool addDirectional_ = false;
    bool enableFog_ = false;
    // Mostly-overhead pose with a small -X / -Y tilt so face shading
    // orders Z > X > Y, recovering the visual feel of the engine's old
    // hardcoded per-face brightness multiplier. The negated X/Y match
    // the outward-normal signs of the visible X_FACE / Y_FACE in iso
    // view (see `faceOutwardNormal` in ir_iso_common.glsl) so the
    // dot-product lambert is positive on the visible sides. Demos that
    // want a different sun (e.g. dramatic side-light) override this.
    vec3 sunDirection_ = vec3(-0.3f, -0.2f, -0.93f);
    float sunIntensity_ = 1.0f;
    float sunAmbient_ = 0.4f;
    bool sunShadowsEnabled_ = true;
    // When false, ambient-occlusion crease darkening is skipped — the AO
    // compute shader writes a constant 1.0. Useful for isolating shadow
    // contribution in lighting demos.
    bool aoEnabled_ = true;
    // C_LightSource{DIRECTIONAL} override. Only applied when
    // addDirectional_ is true. Defaults match the global sun so demos
    // that opt into a directional entity *without* customizing it see
    // no visible change. The lighting_directional demo overrides these
    // to make the override behavior visually obvious.
    vec3 directionalOverrideDirection_ = vec3(-0.3f, -0.2f, -0.93f);
    float directionalOverrideIntensity_ = 1.0f;
    float directionalOverrideAmbient_ = 0.4f;
    bool hdrEnabled_ = false;
    float exposure_ = 1.0f;
    float skyIntensity_ = 0.0f;
    vec3 skyColor_ = vec3(0.5f, 0.7f, 1.0f);

    // Optional override hooks. When `geometryFn_` is set, the demo's scene
    // geometry comes from the callback instead of the default voxel-pool /
    // SDF row layout — useful for SDF-only or animated-sun demos that need
    // a different layout. When `tickFn_` is set, it runs once per render
    // frame and may mutate the global sun, move entities, etc.
    std::function<void()> geometryFn_;
    std::function<void()> tickFn_;
};

namespace detail {

inline constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, vec2(0, 0), 0.0f, "zoom1_origin"},
    {2.0f, vec2(0, 0), 0.0f, "zoom2_origin"},
    {4.0f, vec2(0, 0), 0.0f, "zoom4_origin"},
    {4.0f, vec2(3, 5), 0.0f, "zoom4_offset_3_5"},
    // Higher-zoom shots make per-voxel-pool / SDF parity issues
    // (self-shadowing, AO mismatch from rounding-half-integer voxel
    // positions) immediately visible — they're how the rounding bug
    // fixed in commit `<this>` was found and how regressions on it
    // would surface.
    {8.0f, vec2(0, 0), 0.0f, "zoom8_origin"},
    {16.0f, vec2(0, 0), 0.0f, "zoom16_origin"},
};

inline int g_autoWarmupFrames = 0;
inline int g_autoProfileFrames = 0;
inline int g_autoProfileCount = 0;
inline float g_initialZoom = 0.0f;
// `--light-boundary-sweep` (see #2310): replaces kShots with a
// runtime-computed series that pans the camera anchor away from the emissive
// light in world-X steps, walking the light through the light-volume window
// boundary. Exercises the boundary-seeding path: contribution must fade
// continuously across the shots (in-window → clamped edge seed → out of
// reach), never pop or band.
inline bool g_lightBoundarySweep = false;
inline std::vector<IRVideo::AutoScreenshotShot> g_boundarySweepShots;
inline std::vector<std::array<char, 48>> g_boundarySweepLabels;
inline IRRender::DebugOverlayMode g_cliOverlay = IRRender::DebugOverlayMode::NONE;
// CLI flag for `--no-ao` (or `--ao-off`). Applied after the demo's own
// DemoConfig.aoEnabled_ so the flag wins. Lets validation runs flip AO
// off without rebuilding.
inline bool g_cliDisableAO = false;

inline void registerArgs() {
    IREngine::args().optionalInt("--auto-profile", "Run for N frames then exit (default 300)", 300);
    IREngine::args().number("--zoom", "Initial camera zoom", 0.0f);
    IREngine::args().string(
        "--debug-overlay",
        "Debug overlay mode (none, ao, light_level, shadow, peraxis_id, peraxis_origin, unlit)",
        ""
    );
    IREngine::args().flag("--no-ao", "Disable ambient-occlusion crease darkening");
    IREngine::args().flag("--ao-off", "Alias for --no-ao");
    IREngine::args().flag(
        "--light-boundary-sweep",
        "Auto-screenshot series panning the camera anchor away from the "
        "emissive light through the light-volume window boundary"
    );
}

inline void readArgs() {
    g_autoWarmupFrames = IREngine::args().autoScreenshotWarmupFrames();
    if (IREngine::args().wasProvided("--auto-profile"))
        g_autoProfileFrames = IREngine::args().getInt("--auto-profile");
    float zoom = IREngine::args().getFloat("--zoom");
    if (zoom > 0.0f)
        g_initialZoom = zoom;
    std::string overlayStr = IREngine::args().getString("--debug-overlay");
    if (!overlayStr.empty())
        g_cliOverlay = IRRender::debugOverlayModeFromString(overlayStr.c_str());
    g_cliDisableAO = IREngine::args().getFlag("--no-ao") || IREngine::args().getFlag("--ao-off");
    g_lightBoundarySweep = IREngine::args().getFlag("--light-boundary-sweep");
}

inline EntityId createVoxelPoolShape(
    vec3 position, IRRender::ShapeType type, vec4 shapeParams, Color color, ivec3 halfExtent
) {
    EntityId entity = IREntity::createEntity(
        C_LocalTransform{position},
        C_VoxelSetNew{halfExtent * 2 + ivec3(1), color, true}
    );
    auto &voxelSet = IREntity::getComponent<C_VoxelSetNew>(entity);

    auto sdfType = static_cast<IRMath::SDF::ShapeType>(type);
    vec4 sdfParams = IRMath::SDF::effectiveParams(sdfType, shapeParams);
    voxelSet.carve([&](vec3 pos) {
        return IRMath::SDF::evaluate(pos, sdfType, sdfParams) > IRMath::SDF::kSurfaceThreshold;
    });
    return entity;
}

inline EntityId
createSdfShape(vec3 position, IRRender::ShapeType type, vec4 shapeParams, Color color) {
    return IREntity::createEntity(
        C_LocalTransform{position},
        C_ShapeDescriptor{type, shapeParams, color}
    );
}

inline float sdfBottomZOffset(IRRender::ShapeType type, vec4 params) {
    auto sdfType = static_cast<IRMath::SDF::ShapeType>(type);
    const vec4 effectiveParams = IRMath::SDF::effectiveParams(sdfType, params);
    return IRMath::SDF::boundingHalf(sdfType, effectiveParams).z;
}

inline void configureCanvases(bool enableFog) {
    EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    const ivec2 canvasSize = IREntity::getComponent<C_TriangleCanvasTextures>(mainCanvas).size_;

    IREntity::setComponent(mainCanvas, C_CanvasAOTexture{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasSunShadow{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasLightVolume{});
    IREntity::setComponent(mainCanvas, C_TrixelCanvasRenderBehavior{});
    if (enableFog) {
        IREntity::setComponent(mainCanvas, C_CanvasFogOfWar{});
    }

    EntityId guiCanvas = IRRender::getCanvas("gui");
    IREntity::setComponent(
        guiCanvas,
        C_TrixelCanvasRenderBehavior{false, false, false, false, false, 0.0f, 0.0f, 0.0f, 0.0f}
    );
}

inline void createGeometry() {
    struct ShapeCase {
        IRRender::ShapeType type_;
        vec4 params_;
        ivec3 halfExtent_;
        Color color_;
    };

    constexpr float kSpacingX = 16.0f;
    constexpr float kSdfRowY = 12.0f;
    constexpr float kFloorCenterZ = 5.0f;
    const vec4 floorParams = vec4(4.0f * kSpacingX + 16.0f, kSdfRowY + 24.0f, 2.0f, 0.0f);
    const float kFloorTopZ =
        kFloorCenterZ - sdfBottomZOffset(IRRender::ShapeType::BOX, floorParams);
    const ShapeCase cases[] = {
        {IRRender::ShapeType::BOX, vec4(7, 7, 7, 0), ivec3(3, 3, 3), Color{100, 200, 220, 255}},
        {IRRender::ShapeType::SPHERE, vec4(4, 4, 4, 0), ivec3(5, 5, 5), Color{220, 180, 100, 255}},
        {IRRender::ShapeType::CONE, vec4(4, 4, 8, 0), ivec3(5, 5, 4), Color{220, 140, 100, 255}},
        {IRRender::ShapeType::TORUS, vec4(4, 2, 0, 0), ivec3(7, 7, 3), Color{100, 180, 220, 255}},
    };

    for (int i = 0; i < 4; ++i) {
        const float xPos = static_cast<float>(i) * kSpacingX;
        createVoxelPoolShape(
            vec3(xPos, 0.0f, kFloorTopZ - static_cast<float>(cases[i].halfExtent_.z)),
            cases[i].type_,
            cases[i].params_,
            cases[i].color_,
            cases[i].halfExtent_
        );
        createSdfShape(
            vec3(xPos, kSdfRowY, kFloorTopZ - sdfBottomZOffset(cases[i].type_, cases[i].params_)),
            cases[i].type_,
            cases[i].params_,
            cases[i].color_
        );
    }

    EntityId floor = createSdfShape(
        vec3(1.5f * kSpacingX, kSdfRowY * 0.5f, kFloorCenterZ),
        IRRender::ShapeType::BOX,
        floorParams,
        Color{150, 150, 160, 255}
    );
    IREntity::setComponent(floor, C_LightBlocker{false, false, 0.0f});
}

// Shared by createLights and the --light-boundary-sweep shot builder so the
// sweep pans relative to where the emissive light actually is.
inline constexpr vec3 kEmissiveLightPos{24.0f, 6.0f, -2.0f};

inline void createLights(const DemoConfig &config) {
    IRRender::setSunDirection(config.sunDirection_);
    IRRender::setSunIntensity(config.sunIntensity_);
    IRRender::setSunAmbient(config.sunAmbient_);
    IRRender::setSunShadowsEnabled(config.sunShadowsEnabled_);
    IRRender::setAOEnabled(config.aoEnabled_ && !g_cliDisableAO);

    if (config.addDirectional_) {
        IREntity::createEntity(
            C_LocalTransform{vec3(0.0f)},
            C_LightSource{
                LightType::DIRECTIONAL,
                IRColors::kWhite,
                config.directionalOverrideIntensity_,
                static_cast<std::uint8_t>(0),
                config.directionalOverrideDirection_,
                45.0f,
                config.directionalOverrideAmbient_
            }
        );
    }

    if (config.addEmissive_) {
        IREntity::createEntity(
            C_LocalTransform{kEmissiveLightPos},
            C_LightSource{
                LightType::EMISSIVE,
                Color{80, 210, 255, 255},
                2.0f,
                static_cast<std::uint8_t>(28)
            }
        );
    }

    if (config.addPoint_) {
        IREntity::createEntity(
            C_LocalTransform{vec3(34.0f, -7.0f, -1.0f)},
            C_LightSource{
                LightType::POINT,
                Color{255, 150, 80, 255},
                2.2f,
                static_cast<std::uint8_t>(34)
            }
        );
    }

    if (config.addSpot_) {
        IREntity::createEntity(
            C_LocalTransform{vec3(10.0f, -10.0f, -2.0f)},
            C_LightSource{
                LightType::SPOT,
                Color{170, 120, 255, 255},
                2.4f,
                static_cast<std::uint8_t>(42),
                vec3(0.75f, 0.85f, 0.25f),
                42.0f
            }
        );
    }
}

inline void initEntities(const DemoConfig &config) {
    configureCanvases(config.enableFog_);
    if (config.geometryFn_) {
        config.geometryFn_();
    } else {
        createGeometry();
    }
    createLights(config);

    if (config.enableFog_) {
        IRPrefab::Fog::revealRadius(24, 6, 42);
    }

    IRRender::setHDREnabled(config.hdrEnabled_);
    IRRender::setExposure(config.exposure_);
    IRRender::setSkyIntensity(config.skyIntensity_);
    IRRender::setSkyColor(config.skyColor_);

    IRRender::setDebugOverlay(
        g_cliOverlay == IRRender::DebugOverlayMode::NONE ? config.overlay_ : g_cliOverlay
    );
}

inline void initCommands() {
    IRPrefab::Camera::registerStandardKeyboardCommands();
    IRCommand::registerCaptureCommands();
}

inline void initSystems(const DemoConfig &config) {
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>()}
    );

    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );

    std::list<IRSystem::SystemId> renderPipeline = IRPrefab::Camera::standardControlSystems();
    renderPipeline.insert(
        renderPipeline.end(),
        {
            IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
            IRSystem::createSystem<IRSystem::BUILD_LIGHT_OCCLUSION_GRID>(),
            IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
            IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::COMPUTE_VOXEL_AO>(),
            IRSystem::createSystem<IRSystem::BAKE_SUN_SHADOW_MAP>(),
            IRSystem::createSystem<IRSystem::COMPUTE_SUN_SHADOW>(),
            IRSystem::createSystem<IRSystem::COMPUTE_LIGHT_VOLUME>(),
            IRSystem::createSystem<IRSystem::LIGHTING_TO_TRIXEL>(),
        }
    );

    // Demo-supplied tick (e.g. animated sun direction). Runs first in the
    // render pipeline so subsequent systems pick up the updated state.
    if (config.tickFn_) {
        IRSystem::SystemId tickId = IRSystem::createSystem<C_Name>(
            "LightingDemoTick",
            [](C_Name &) {},
            [tick = config.tickFn_]() { tick(); }
        );
        renderPipeline.push_front(tickId);
    }

    if (config.enableFog_) {
        renderPipeline.push_back(IRSystem::createSystem<IRSystem::FOG_TO_TRIXEL>());
    }

    // The perf-stats overlay burns live wall-clock FPS / GPU-stage timings
    // into the frame. Those digits are non-deterministic (RENDER deltaTime and
    // the GPU timer queries are wall-clock), so leaving the overlay on during an
    // --auto-screenshot capture would pollute a render-verify baseline and flake
    // its pixel-diff run-to-run. Suppress it only while capturing; interactive
    // and --auto-profile runs (g_autoWarmupFrames == 0) still get it. Mirrors
    // fog_demo, which omits the overlay entirely.
    if (g_autoWarmupFrames == 0) {
        renderPipeline.push_back(IRSystem::createSystem<IRSystem::PERF_STATS_OVERLAY>());
    }
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>());
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>());
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>());

    if (g_autoProfileFrames > 0) {
        IRSystem::SystemId autoProfileId = IRSystem::createSystem<C_Name>(
            "LightingAutoProfile",
            [](C_Name &) {},
            []() {
                ++g_autoProfileCount;
                if (g_autoProfileCount >= g_autoProfileFrames) {
                    IRWindow::closeWindow();
                }
            }
        );
        renderPipeline.push_back(autoProfileId);
    }

    if (g_autoWarmupFrames > 0) {
        IRVideo::AutoScreenshotConfig screenshotConfig{};
        screenshotConfig.warmupFrames_ = g_autoWarmupFrames;
        screenshotConfig.settleFrames_ = 3;
        if (g_lightBoundarySweep) {
            // World-X distances from the emissive light to the camera anchor.
            // With the light's radius r and the volume half-extent 64:
            // 0/40 stay in-window (identical full-strength field), 70 seeds
            // the clamped edge at residual 1 − 6·step, 88 at 1 − 24·step,
            // and 110 is out of residual reach (correctly dark).
            constexpr float kSweepDistances[] = {0.0f, 40.0f, 70.0f, 88.0f, 110.0f};
            const std::size_t n = sizeof(kSweepDistances) / sizeof(kSweepDistances[0]);
            g_boundarySweepShots.reserve(n);
            g_boundarySweepLabels.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                const vec3 anchorTarget =
                    vec3(kEmissiveLightPos.x + kSweepDistances[i], kEmissiveLightPos.y, 0.0f);
                auto &label = g_boundarySweepLabels.emplace_back();
                std::snprintf(
                    label.data(),
                    label.size(),
                    "light_boundary_d%03d",
                    static_cast<int>(kSweepDistances[i])
                );
                IRVideo::AutoScreenshotShot
                    shot{2.0f, -IRMath::pos3DtoPos2DIso(anchorTarget), 0.0f, label.data()};
                g_boundarySweepShots.push_back(shot);
            }
            screenshotConfig.shots_ = g_boundarySweepShots.data();
            screenshotConfig.numShots_ = static_cast<int>(g_boundarySweepShots.size());
        } else {
            screenshotConfig.shots_ = kShots;
            screenshotConfig.numShots_ = sizeof(kShots) / sizeof(kShots[0]);
        }
        renderPipeline.push_back(IRVideo::createAutoScreenshotSystem(screenshotConfig));
    }

    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}

} // namespace detail

inline int run(int argc, char **argv, const DemoConfig &config) {
    detail::registerArgs();
    IREngine::init(argc, argv);
    detail::readArgs();
    IR_LOG_INFO("Starting creation: {}", config.name_);
    IREngine::enableFrameTiming(true);
    detail::initSystems(config);
    detail::initCommands();
    detail::initEntities(config);
    if (detail::g_initialZoom > 0.0f) {
        IRRender::setCameraZoom(detail::g_initialZoom);
    }
    IREngine::gameLoop();
    return 0;
}

} // namespace IRLightingDemo

#endif /* LIGHTING_DEMO_SCENE_H */

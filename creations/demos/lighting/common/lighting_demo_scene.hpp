#ifndef LIGHTING_DEMO_SCENE_H
#define LIGHTING_DEMO_SCENE_H

#include <irreden/ir_engine.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>

#include <irreden/common/command_suite_camera.hpp>
#include <irreden/common/command_suite_capture.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/math/sdf.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/components/component_occupancy_grid.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/systems/system_build_occupancy_grid.hpp>
#include <irreden/render/systems/system_camera_mouse_pan.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
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
#include <irreden/update/systems/system_update_positions_global.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <list>

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
    // Mostly-overhead pose with a small +X / +Y tilt so face shading
    // orders Z > X > Y, recovering the visual feel of the engine's old
    // hardcoded per-face brightness multiplier. Demos that want a
    // different sun (e.g. dramatic side-light) override this.
    vec3 sunDirection_ = vec3(0.3f, 0.2f, -0.93f);
    float sunIntensity_ = 1.0f;
    float sunAmbient_ = 0.4f;
    bool sunShadowsEnabled_ = true;
    // C_LightSource{DIRECTIONAL} override. Only applied when
    // addDirectional_ is true. Defaults match the global sun so demos
    // that opt into a directional entity *without* customizing it see
    // no visible change. The lighting_directional demo overrides these
    // to make the override behavior visually obvious.
    vec3 directionalOverrideDirection_ = vec3(0.3f, 0.2f, -0.93f);
    float directionalOverrideIntensity_ = 1.0f;
    float directionalOverrideAmbient_ = 0.4f;

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
    {1.0f, vec2(0, 0), "zoom1_origin"},
    {2.0f, vec2(0, 0), "zoom2_origin"},
    {4.0f, vec2(0, 0), "zoom4_origin"},
    {4.0f, vec2(3, 5), "zoom4_offset_3_5"},
    // Higher-zoom shots make per-voxel-pool / SDF parity issues
    // (self-shadowing, AO mismatch from rounding-half-integer voxel
    // positions) immediately visible — they're how the rounding bug
    // fixed in commit `<this>` was found and how regressions on it
    // would surface.
    {8.0f, vec2(0, 0), "zoom8_origin"},
    {16.0f, vec2(0, 0), "zoom16_origin"},
};

inline int g_autoWarmupFrames = 0;
inline int g_autoProfileFrames = 0;
inline int g_autoProfileCount = 0;
inline float g_initialZoom = 0.0f;
inline IRRender::DebugOverlayMode g_cliOverlay = IRRender::DebugOverlayMode::NONE;

inline void parseArgs(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &g_autoWarmupFrames);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--auto-profile") == 0) {
            g_autoProfileFrames = 300;
            if (i + 1 < argc) {
                int frames = std::atoi(argv[i + 1]);
                if (frames > 0) {
                    g_autoProfileFrames = frames;
                    ++i;
                }
            }
        } else if (std::strcmp(argv[i], "--zoom") == 0) {
            if (i + 1 < argc) {
                float zoom = static_cast<float>(std::atof(argv[i + 1]));
                if (zoom > 0.0f) {
                    g_initialZoom = zoom;
                    ++i;
                }
            }
        } else if (std::strcmp(argv[i], "--debug-overlay") == 0) {
            if (i + 1 < argc) {
                g_cliOverlay = IRRender::debugOverlayModeFromString(argv[i + 1]);
                ++i;
            }
        }
    }
}

inline EntityId createVoxelPoolShape(
    vec3 position, IRRender::ShapeType type, vec4 shapeParams, Color color, ivec3 halfExtent
) {
    EntityId entity = IREntity::createEntity(
        C_Position3D{position},
        C_VoxelSetNew{halfExtent * 2 + ivec3(1), color, true}
    );
    auto &voxelSet = IREntity::getComponent<C_VoxelSetNew>(entity);

    auto sdfType = static_cast<IRMath::SDF::ShapeType>(type);
    vec4 sdfParams = IRMath::SDF::effectiveParams(sdfType, shapeParams);
    for (int i = 0; i < voxelSet.numVoxels_; ++i) {
        if (IRMath::SDF::evaluate(voxelSet.positions_[i].pos_, sdfType, sdfParams) >
            IRMath::SDF::kSurfaceThreshold) {
            voxelSet.voxels_[i].deactivate();
        }
    }
    return entity;
}

inline EntityId
createSdfShape(vec3 position, IRRender::ShapeType type, vec4 shapeParams, Color color) {
    return IREntity::createEntity(
        C_Position3D{position},
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

    IREntity::setComponent(mainCanvas, C_OccupancyGrid{256});
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

inline void createLights(const DemoConfig &config) {
    IRRender::setSunDirection(config.sunDirection_);
    IRRender::setSunIntensity(config.sunIntensity_);
    IRRender::setSunAmbient(config.sunAmbient_);
    IRRender::setSunShadowsEnabled(config.sunShadowsEnabled_);

    if (config.addDirectional_) {
        IREntity::createEntity(
            C_Position3D{vec3(0.0f)},
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
            C_Position3D{vec3(24.0f, 6.0f, -2.0f)},
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
            C_Position3D{vec3(34.0f, -7.0f, -1.0f)},
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
            C_Position3D{vec3(10.0f, -10.0f, -2.0f)},
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

    IRRender::setDebugOverlay(
        g_cliOverlay == IRRender::DebugOverlayMode::NONE ? config.overlay_ : g_cliOverlay
    );
}

inline void initCommands() {
    IRCommand::registerCameraCommands();
    IRCommand::registerCaptureCommands();
}

inline void initSystems(const DemoConfig &config) {
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::GLOBAL_POSITION_3D>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>()}
    );

    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );

    std::list<IRSystem::SystemId> renderPipeline = {
        IRSystem::createSystem<IRSystem::CAMERA_MOUSE_PAN>(),
        IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
        IRSystem::createSystem<IRSystem::BUILD_OCCUPANCY_GRID>(),
        IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
        IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_2>(),
        IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
        IRSystem::createSystem<IRSystem::COMPUTE_VOXEL_AO>(),
        IRSystem::createSystem<IRSystem::COMPUTE_SUN_SHADOW>(),
        IRSystem::createSystem<IRSystem::COMPUTE_LIGHT_VOLUME>(),
        IRSystem::createSystem<IRSystem::LIGHTING_TO_TRIXEL>(),
    };

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

    renderPipeline.push_back(IRSystem::createSystem<IRSystem::PERF_STATS_OVERLAY>());
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
        screenshotConfig.shots_ = kShots;
        screenshotConfig.numShots_ = sizeof(kShots) / sizeof(kShots[0]);
        renderPipeline.push_back(IRVideo::createAutoScreenshotSystem(screenshotConfig));
    }

    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}

} // namespace detail

inline int run(int argc, char **argv, const DemoConfig &config) {
    detail::parseArgs(argc, argv);
    IR_LOG_INFO("Starting creation: {}", config.name_);
    IREngine::init(argv[0]);
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

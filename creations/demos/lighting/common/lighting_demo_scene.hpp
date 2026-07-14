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
#include <irreden/render/commands/command_toggle_culling_minimap.hpp>
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
#include <irreden/render/cull_viewport_state.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/systems/system_build_light_occlusion_grid.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>
#include <irreden/render/systems/system_compute_sun_shadow.hpp>
#include <irreden/render/systems/system_compute_voxel_ao.hpp>
#include <irreden/render/systems/system_debug_culling_minimap.hpp>
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

#include <cstdint>
#include <cstdio>
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

    // Optional per-demo auto-screenshot shot table. When `shots_` is non-null
    // it replaces the shared `kShots` (still overridden by
    // `--light-boundary-sweep`) — used by the spot demo to add yaw shots that
    // prove the winning-light-ID cone stays world-oriented across camera yaw
    // (#2318). Points at a static array; the config is copied by value but the
    // pointee outlives it.
    const IRVideo::AutoScreenshotShot *shots_ = nullptr;
    int numShots_ = 0;

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
inline IRVideo::IndexedSweepShots<48> g_boundarySweepShots;
// `--light-domain-matrix` (V3, #2317): zoom x yaw x pan-distance shot matrix
// over the same emissive-light-relative pan axis as the boundary sweep, for
// light-verify.py's zoom/yaw/pan domain assertions.
inline bool g_lightDomainMatrix = false;
inline IRVideo::IndexedSweepShots<48> g_domainMatrixShots;
// `--hover-sweep` (V3, #2317): see kHoverSweepHeights above.
inline bool g_hoverSweep = false;
inline IRVideo::IndexedSweepShots<32> g_hoverSweepShots;
inline IRRender::DebugOverlayMode g_cliOverlay = IRRender::DebugOverlayMode::NONE;
// CLI flag for `--no-ao` (or `--ao-off`). Applied after the demo's own
// DemoConfig.aoEnabled_ so the flag wins. Lets validation runs flip AO
// off without rebuilding.
inline bool g_cliDisableAO = false;

// DOMAIN-STATE instrumentation (#2315, V1). The COMPUTE_LIGHT_VOLUME
// SystemId, captured at pipeline registration, so the DOMAIN-STATE
// emission hook can read back its per-light gather records
// (`IRSystem::lightGatherRecords`) — the system stores that state on
// itself (`engine/system/CLAUDE.md` "System-owned state"), not in a
// globally-queryable component.
inline IRSystem::SystemId g_computeLightVolumeSystemId{};
// BAKE_SUN_SHADOW_MAP SystemId (#2316, V2) — the culling minimap's caster
// domain reads world-placed casters back via
// `IRSystem::worldPlacedCasters(g_bakeSunShadowMapSystemId)`, mirroring
// `g_computeLightVolumeSystemId` above.
inline IRSystem::SystemId g_bakeSunShadowMapSystemId{};
// The shot table actually wired into AutoScreenshotConfig this run (kShots or
// one of the runtime-built series: --light-boundary-sweep,
// --light-domain-matrix, --hover-sweep) — the DOMAIN-STATE hook only receives
// a shot index, so it needs this to recover the shot's label.
inline const IRVideo::AutoScreenshotShot *g_activeShots = nullptr;

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
    IREngine::args().flag(
        "--light-domain-matrix",
        "Auto-screenshot series covering the zoom x yaw x pan-distance domain "
        "matrix relative to the emissive light (V3 light-verify harness)"
    );
    IREngine::args().flag(
        "--hover-sweep",
        "Auto-screenshot series raising a single cube through increasing "
        "hover heights above the floor, for the shadow-footprint truncation "
        "curve (V3 light-verify harness / S2 baseline)"
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
    g_lightDomainMatrix = IREngine::args().getFlag("--light-domain-matrix");
    g_hoverSweep = IREngine::args().getFlag("--hover-sweep");
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
        IRPrefab::Fog::attachToCanvas(mainCanvas);
    }

    EntityId guiCanvas = IRRender::getCanvas("gui");
    IREntity::setComponent(
        guiCanvas,
        C_TrixelCanvasRenderBehavior{false, false, false, false, false, 0.0f, 0.0f, 0.0f, 0.0f}
    );
}

inline constexpr float kSpacingX = 16.0f;
inline constexpr float kSdfRowY = 12.0f;
inline constexpr float kFloorCenterZ = 5.0f;

inline vec4 floorSdfParams() {
    return vec4(4.0f * kSpacingX + 16.0f, kSdfRowY + 24.0f, 2.0f, 0.0f);
}

// Shared by createGeometry and the --hover-sweep cube (V3, #2317) so a
// hover-sweep shot's "grounded" height lands exactly on the same floor
// surface the row-case shapes rest on.
inline float floorTopZ() {
    return kFloorCenterZ - sdfBottomZOffset(IRRender::ShapeType::BOX, floorSdfParams());
}

inline void createGeometry() {
    struct ShapeCase {
        IRRender::ShapeType type_;
        vec4 params_;
        ivec3 halfExtent_;
        Color color_;
    };

    const vec4 floorParams = floorSdfParams();
    const float kFloorTopZ = floorTopZ();
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

// --hover-sweep (V3, #2317): a single cube raised through increasing hover
// heights above the floor, so S2's shadow-throw unification work has a
// hover-height shadow-footprint-truncation-curve baseline to diff against.
// Positioned off to the side of the row-case shapes (negative X) so its
// floor shadow isn't occluded by them. Assumes the default createGeometry()
// floor — a geometryFn_-overridden demo's floor may not be at floorTopZ().
inline constexpr IRRender::ShapeType kHoverSweepShapeType = IRRender::ShapeType::BOX;
inline constexpr vec4 kHoverSweepShapeParams{7.0f, 7.0f, 7.0f, 0.0f};
inline constexpr ivec3 kHoverSweepVoxelHalfExtent{3, 3, 3};
// x=-8 keeps clear of both the floor's x=-16 edge and the row shapes at
// x=0/y={0,kSdfRowY}; y=kSdfRowY*0.5 is the floor's own Y-center.
inline constexpr vec2 kHoverSweepXY{-8.0f, kSdfRowY * 0.5f};
inline constexpr float kHoverSweepHeights[] = {0.0f, 8.0f, 16.0f, 24.0f, 32.0f};
inline EntityId g_hoverSweepCube{};

// World Z is -up (see the row-case loop above: `kFloorTopZ - halfExtent.z`
// lifts a shape off the floor), so hover height subtracts further.
inline float hoverSweepZ(float height) {
    return floorTopZ() - sdfBottomZOffset(kHoverSweepShapeType, kHoverSweepShapeParams) - height;
}

inline void createHoverSweepCube() {
    g_hoverSweepCube = createVoxelPoolShape(
        vec3(kHoverSweepXY.x, kHoverSweepXY.y, hoverSweepZ(kHoverSweepHeights[0])),
        kHoverSweepShapeType,
        kHoverSweepShapeParams,
        Color{200, 90, 90, 255},
        kHoverSweepVoxelHalfExtent
    );
}

// Shared by createLights and the --light-boundary-sweep / --light-domain-matrix
// shot builders so their world-space pans stay relative to where the emissive
// light actually is.
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
        // Mounted just above the shapes (up is -z here; floor top ≈ z 3, shape
        // tops ≈ z -3) and aimed straight down, so the winning-light-ID cone
        // (#2318) casts a clean circular pool with a soft rim onto the floor —
        // the unambiguous "cone, not a sphere" demonstration. Mounted close
        // (~14 voxels up) so the Manhattan-falloff residual is still strong at
        // the floor (alpha ≈ 0.55) instead of decaying to near-dark over the
        // 32-step propagate budget.
        IREntity::createEntity(
            C_LocalTransform{vec3(24.0f, 6.0f, -11.0f)},
            C_LightSource{
                LightType::SPOT,
                Color{210, 90, 255, 255},
                1.6f,
                static_cast<std::uint8_t>(32),
                vec3(0.0f, 0.0f, 1.0f),
                44.0f
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
    if (g_hoverSweep) {
        createHoverSweepCube();
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
    // Culling-minimap visibility toggle (#2316, V2). F11 matches shape_debug's
    // binding.
    IRCommand::createCommand<IRCommand::TOGGLE_CULLING_MINIMAP>(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonF11
    );
}

// DOMAIN-STATE emission hook (#2315, V1): `AutoScreenshotConfig::onCaptureFrame_`
// callback wired below. Fires once per shot, on the settled capture frame,
// after the frame's render systems (including COMPUTE_LIGHT_VOLUME) have
// already run — so every value read here reflects what was actually
// rendered. Emits one machine-readable log line per shot (the same
// `IR_LOG_INFO` precedent as the `GUI-ASSERT` lines in
// `gui_test_assertions.hpp`) for the V3 light-verify harness (#2317) to
// parse; format is the contract — see issue #2315's plan "Sibling
// reconciliation" note before changing it.
//
// Main canvas only for V1 (matches the minimap's V2 scope, #2314 plan
// "Per-canvas gather scope" gotcha) — `lightGatherRecords` already reads
// back COMPUTE_LIGHT_VOLUME's own per-canvas gather, so a multi-canvas demo
// would need one call per canvas; none of the lighting demos have more than
// the main canvas today.
inline void logDomainState(int shotIndex) {
    const char *label = (g_activeShots != nullptr) ? g_activeShots[shotIndex].label_ : "unknown";

    const ivec3 anchor = IRRender::getLightAnchorFreeze().anchor_;
    const ivec3 windowLo = anchor - ivec3(kLightVolumeHalfExtent);
    const ivec3 windowHi = anchor + ivec3(kLightVolumeHalfExtent - 1);

    std::string lights = "[";
    const auto &records = IRSystem::lightGatherRecords(g_computeLightVolumeSystemId);
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto &r = records[i];
        const char *state = r.state_ == IRSystem::LightGatherState::SEEDED_FULL ? "SEEDED_FULL"
                            : r.state_ == IRSystem::LightGatherState::BOUNDARY_DISCOUNTED
                                ? "BOUNDARY_DISCOUNTED"
                                : "SKIPPED";
        char entry[64];
        std::snprintf(
            entry,
            sizeof(entry),
            "%s%llu:%s:%.3f",
            i == 0 ? "" : ",",
            static_cast<unsigned long long>(r.entity_),
            state,
            r.residual_
        );
        lights += entry;
    }
    lights += "]";

    const auto &gpu = IRRender::gpuStageTiming();
    IR_LOG_INFO(
        "DOMAIN-STATE shot={} anchor={},{},{} window={},{},{}..{},{},{} lights={} "
        "feeder={:.1f},{:.1f}..{:.1f},{:.1f} casters={}",
        label,
        anchor.x,
        anchor.y,
        anchor.z,
        windowLo.x,
        windowLo.y,
        windowLo.z,
        windowHi.x,
        windowHi.y,
        windowHi.z,
        lights,
        gpu.shadowFeederMin_.x,
        gpu.shadowFeederMin_.y,
        gpu.shadowFeederMax_.x,
        gpu.shadowFeederMax_.y,
        gpu.worldPlacedCasterCount_
    );

    // --hover-sweep (V3, #2317): reposition the cube for the NEXT shot here,
    // not the current one — this hook fires right after the current shot's
    // screenshot is requested, and the cycling system takes one more tick to
    // advance currentShot_ before it starts the next shot's settle window
    // (engine/video/src/auto_screenshot.cpp), so the move lands a full settle
    // window ahead of the next capture.
    if (g_hoverSweep) {
        constexpr int n = sizeof(kHoverSweepHeights) / sizeof(kHoverSweepHeights[0]);
        const int nextIndex = shotIndex + 1;
        if (nextIndex < n) {
            IREntity::setComponent(
                g_hoverSweepCube,
                C_LocalTransform{vec3(
                    kHoverSweepXY.x,
                    kHoverSweepXY.y,
                    hoverSweepZ(kHoverSweepHeights[nextIndex])
                )}
            );
        }
    }
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
            // BUILD_LIGHT_OCCLUSION_GRID must create() before COMPUTE_LIGHT_VOLUME —
            // the latter looks up "LightOcclusionGridBuffer" (a named resource the
            // former creates) at init time.
            IRSystem::createSystem<IRSystem::BUILD_LIGHT_OCCLUSION_GRID>(),
            IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
            IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::COMPUTE_VOXEL_AO>(),
            // Captured for the culling minimap's caster domain (#2316, V2) —
            // see g_bakeSunShadowMapSystemId's doc comment above.
            (g_bakeSunShadowMapSystemId = IRSystem::createSystem<IRSystem::BAKE_SUN_SHADOW_MAP>()),
            IRSystem::createSystem<IRSystem::COMPUTE_SUN_SHADOW>(),
            // Captured for the DOMAIN-STATE emission hook (#2315, V1) — the hook
            // reads this system's per-light gather records back via
            // `IRSystem::lightGatherRecords(g_computeLightVolumeSystemId)`.
            (g_computeLightVolumeSystemId =
                 IRSystem::createSystem<IRSystem::COMPUTE_LIGHT_VOLUME>()),
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
    renderPipeline.push_back(
        IRSystem::System<IRSystem::DEBUG_CULLING_MINIMAP>::create({
            .lightVolumeSystemId_ = g_computeLightVolumeSystemId,
            .bakeSunShadowSystemId_ = g_bakeSunShadowMapSystemId,
        })
    );
    // Off during --auto-screenshot captures — the minimap is a live debug
    // aid, not part of the render-verify golden image (#2316, V2 plan
    // "Verification": map off during reference captures). Interactive runs
    // (g_autoWarmupFrames == 0) default it visible; F11 toggles it either way.
    IRRender::setCullingMinimapEnabled(g_autoWarmupFrames == 0);
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
        if (g_hoverSweep) {
            constexpr std::size_t n = sizeof(kHoverSweepHeights) / sizeof(kHoverSweepHeights[0]);
            const vec2 hoverCameraIso =
                -IRMath::pos3DtoPos2DIso(vec3(kHoverSweepXY.x, kHoverSweepXY.y, 0.0f));
            g_hoverSweepShots.build(
                n,
                [&](std::size_t) {
                    return IRVideo::AutoScreenshotShot{4.0f, hoverCameraIso, 0.0f};
                },
                [](std::size_t i, char *buf, std::size_t size) {
                    std::snprintf(
                        buf,
                        size,
                        "hover_h%03d",
                        static_cast<int>(kHoverSweepHeights[i])
                    );
                }
            );
            screenshotConfig.shots_ = g_hoverSweepShots.shots_.data();
            screenshotConfig.numShots_ = static_cast<int>(g_hoverSweepShots.shots_.size());
        } else if (g_lightDomainMatrix) {
            // Zoom x yaw x pan-distance domain matrix. Pan reuses the boundary
            // sweep's calibration (see kSweepDistances below): in-window,
            // clamped-edge band, and out-of-reach. Bounded to 4x3x3 = 36 shots
            // (light-verify's own budget note: keep the matrix under ~45).
            constexpr float kMatrixZooms[] = {1.0f, 2.0f, 4.0f, 8.0f};
            constexpr float kMatrixYawDegrees[] = {0.0f, 30.0f, 45.0f};
            constexpr float kMatrixPanDistances[] = {0.0f, 70.0f, 110.0f};
            constexpr const char *kMatrixPanNames[] = {"inwin", "band", "beyond"};
            constexpr std::size_t kNumYaws =
                sizeof(kMatrixYawDegrees) / sizeof(kMatrixYawDegrees[0]);
            constexpr std::size_t kNumPans =
                sizeof(kMatrixPanDistances) / sizeof(kMatrixPanDistances[0]);
            constexpr std::size_t kNumZooms = sizeof(kMatrixZooms) / sizeof(kMatrixZooms[0]);
            g_domainMatrixShots.build(
                kNumZooms * kNumYaws * kNumPans,
                [&](std::size_t i) {
                    const std::size_t zoomIdx = i / (kNumYaws * kNumPans);
                    const std::size_t rem = i % (kNumYaws * kNumPans);
                    const std::size_t yawIdx = rem / kNumPans;
                    const std::size_t panIdx = rem % kNumPans;
                    const vec3 anchorTarget = vec3(
                        kEmissiveLightPos.x + kMatrixPanDistances[panIdx],
                        kEmissiveLightPos.y,
                        0.0f
                    );
                    return IRVideo::AutoScreenshotShot{
                        kMatrixZooms[zoomIdx],
                        -IRMath::pos3DtoPos2DIso(anchorTarget),
                        kMatrixYawDegrees[yawIdx] * IRMath::kPi / 180.0f
                    };
                },
                [&](std::size_t i, char *buf, std::size_t size) {
                    const std::size_t zoomIdx = i / (kNumYaws * kNumPans);
                    const std::size_t rem = i % (kNumYaws * kNumPans);
                    const std::size_t yawIdx = rem / kNumPans;
                    const std::size_t panIdx = rem % kNumPans;
                    std::snprintf(
                        buf,
                        size,
                        "domain_z%d_yaw%d_%s",
                        static_cast<int>(kMatrixZooms[zoomIdx]),
                        static_cast<int>(kMatrixYawDegrees[yawIdx]),
                        kMatrixPanNames[panIdx]
                    );
                }
            );
            screenshotConfig.shots_ = g_domainMatrixShots.shots_.data();
            screenshotConfig.numShots_ = static_cast<int>(g_domainMatrixShots.shots_.size());
        } else if (g_lightBoundarySweep) {
            // World-X distances from the emissive light to the camera anchor.
            // With the light's radius r and the volume half-extent 64:
            // 0/40 stay in-window (identical full-strength field), 70 seeds
            // the clamped edge at residual 1 − 6·step, 88 at 1 − 24·step,
            // and 110 is out of residual reach (correctly dark).
            constexpr float kSweepDistances[] = {0.0f, 40.0f, 70.0f, 88.0f, 110.0f};
            g_boundarySweepShots.build(
                sizeof(kSweepDistances) / sizeof(kSweepDistances[0]),
                [&](std::size_t i) {
                    const vec3 anchorTarget =
                        vec3(kEmissiveLightPos.x + kSweepDistances[i], kEmissiveLightPos.y, 0.0f);
                    return IRVideo::AutoScreenshotShot{
                        2.0f,
                        -IRMath::pos3DtoPos2DIso(anchorTarget),
                        0.0f
                    };
                },
                [&](std::size_t i, char *buf, std::size_t size) {
                    std::snprintf(
                        buf,
                        size,
                        "light_boundary_d%03d",
                        static_cast<int>(kSweepDistances[i])
                    );
                }
            );
            screenshotConfig.shots_ = g_boundarySweepShots.shots_.data();
            screenshotConfig.numShots_ = static_cast<int>(g_boundarySweepShots.shots_.size());
        } else if (config.shots_ != nullptr) {
            // Per-demo shot table (e.g. the spot demo's yaw sweep, #2318).
            screenshotConfig.shots_ = config.shots_;
            screenshotConfig.numShots_ = config.numShots_;
        } else {
            screenshotConfig.shots_ = kShots;
            screenshotConfig.numShots_ = sizeof(kShots) / sizeof(kShots[0]);
        }
        g_activeShots = screenshotConfig.shots_;
        screenshotConfig.onCaptureFrame_ = &logDomainState;
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

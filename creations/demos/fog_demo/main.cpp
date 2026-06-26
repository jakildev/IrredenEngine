// fog_demo — dedicated demo + cross-host smoke coverage for the fog-of-war
// render pass (FOG_TO_TRIXEL). The fog-of-war feature shipped with a render
// pass but no demo of its own; shape_debug carried the only end-to-end fog
// wiring, which made fog regressions invisible once that wiring moved on.
//
// The scene is deliberately small — a flat floor plus a handful of SDF shapes
// — but it exercises ALL THREE fog states so a cross-host (OpenGL vs Metal)
// auto-screenshot diff can catch a per-platform fog discrepancy:
//
//   * VISIBLE   (255) — a reveal circle around the world origin. The shapes
//                       and floor inside it render at full color.
//   * EXPLORED  (128) — a "memory" band of cells just outside the visible
//                       circle, set explicitly with `setCell`. The fog pass
//                       desaturates + darkens these, so the band is the
//                       diagnostic surface for the explored-state shader math.
//   * UNEXPLORED (0)  — everything beyond the band: black.
//
// The three --auto-screenshot shots (zoom 2 / 4 / 8 at origin) frame the
// visible→explored→unexplored boundary at increasing magnification so the
// per-state transitions are sampled at multiple pixel scales.
//
// `--moving-observer` (#2009) swaps the static reveal for a per-frame float
// reveal that orbits the origin in sub-cell steps and drives the FEATHERED
// `revealRadius(float,float,float,float)` overload. It is the vehicle for
// inspecting the soft, non-vibrating reveal edge (render-debug-loop / a
// move-capture sequence) — a smoothly-moving float center reveals without the
// per-cell popping an integer-snapping reveal would show. The default (no
// flag) static scene is unchanged and owns the committed render-verify refs.
//
// Fog wiring (canvas component + pipeline position) is faithful to the
// pre-removal shape_debug setup; the rest of the skeleton follows the smaller
// day_cycle demo.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>

#include <irreden/render/camera.hpp>
#include <irreden/render/camera_controls.hpp>

// Scene components.
#include <irreden/common/command_suite_capture.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

// Fog driver-side API (revealRadius / setCell).
#include <irreden/render/fog_of_war.hpp>

// Scene systems.
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>
#include <irreden/render/systems/system_build_light_occlusion_grid.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_compute_sun_shadow.hpp>
#include <irreden/render/systems/system_compute_voxel_ao.hpp>
#include <irreden/render/systems/system_fog_to_trixel.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_lighting_to_trixel.hpp>
#include <irreden/render/systems/system_lod_update.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>

#include <list>
#include <string_view>

using namespace IRComponents;
using IRMath::Color;
using IRMath::vec2;
using IRMath::vec3;
using IRMath::vec4;

namespace {

// The visible reveal radius (Euclidean) and the explored memory band sit just
// outside it. `revealRadius` reveals a circle of `kRevealRadius`; the band
// occupies the next `kBandWidth` Euclidean rings so the desaturate+darken
// explored shading appears as a ring immediately bordering the bright circle.
constexpr int kRevealRadius = 18;
constexpr int kBandWidth = 8;

// --moving-observer feathered reveal (#2009). The center orbits the origin in
// sub-cell per-frame steps (orbit × angular-step ≈ 0.4 cell/frame < 1 cell) so
// successive frames land on distinct sub-cell offsets — the case that exposes
// per-cell popping if the reveal snapped to integers. kFeather is the width of
// the smooth ramp at the disc edge (a third of the radius reads clearly).
constexpr float kFeather = 6.0f;
constexpr float kObserverOrbit = 8.0f;
constexpr float kObserverAngularStep = 0.05f;

// Three shots straddling the fog boundary at increasing magnification. All at
// the origin so the visible→explored→unexplored rings stay centered; the
// climbing zoom samples the per-state transitions at multiple pixel scales.
constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {2.0f, vec2(0, 0), 0.0f, "fog_zoom2_origin"},
    {4.0f, vec2(0, 0), 0.0f, "fog_zoom4_origin"},
    {8.0f, vec2(0, 0), 0.0f, "fog_zoom8_origin"},
};

int g_autoWarmupFrames = 0; // 0 = --auto-screenshot not requested

bool g_movingObserver = false; // --moving-observer: per-frame feathered reveal
int g_observerFrame = 0;       // deterministic frame index for the orbit

// Per-frame hook for --moving-observer: wipe the fog and re-reveal a feathered
// disc at a smoothly-advancing float center. Re-uploading the whole grid each
// frame is the documented single-moving-observer pattern (component header).
void driveMovingObserver() {
    const float theta = static_cast<float>(g_observerFrame) * kObserverAngularStep;
    const float cx = kObserverOrbit * IRMath::cos(theta);
    const float cy = kObserverOrbit * IRMath::sin(theta);
    IRPrefab::Fog::clear();
    IRPrefab::Fog::revealRadius(cx, cy, static_cast<float>(kRevealRadius), kFeather);
    ++g_observerFrame;
}

} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &g_autoWarmupFrames);
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--moving-observer") {
            g_movingObserver = true;
        }
    }

    IR_LOG_INFO("Starting creation: fog_demo");
    IREngine::init(argv[0]);
    initSystems();
    initCommands();
    initEntities();
    IREngine::gameLoop();
    return 0;
}

void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::LOD_UPDATE>(),
         IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
         IRSystem::createSystem<IRSystem::REBUILD_GRID_VOXELS>()}
    );

    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );

    // The lighting passes feed the trixel textures that FOG_TO_TRIXEL then
    // masks. FOG_TO_TRIXEL must sit immediately AFTER LIGHTING_TO_TRIXEL and
    // BEFORE TRIXEL_TO_FRAMEBUFFER — that order is the load-bearing part of the
    // fog wiring (faithful to shape_debug's pre-removal pipeline).
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
            IRSystem::createSystem<IRSystem::FOG_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
        }
    );

    // --moving-observer: a once-per-frame beginTick hook (same idiom as the
    // day_cycle sun hook) that wipes + re-reveals the feathered disc at the
    // advancing float center. Pushed to the front so the new fog is current
    // before VOXEL_TO_TRIXEL / FOG_TO_TRIXEL run this frame.
    if (g_movingObserver) {
        IRSystem::SystemId observerTickId = IRSystem::createSystem<C_Name>(
            "FogMovingObserverTick",
            [](C_Name &) {},
            []() { driveMovingObserver(); }
        );
        renderPipeline.push_front(observerTickId);
    }

    if (g_autoWarmupFrames > 0) {
        IRVideo::AutoScreenshotConfig cfg{};
        cfg.warmupFrames_ = g_autoWarmupFrames;
        cfg.settleFrames_ = 3;
        cfg.shots_ = kShots;
        cfg.numShots_ = sizeof(kShots) / sizeof(kShots[0]);
        renderPipeline.push_back(IRVideo::createAutoScreenshotSystem(cfg));
    }

    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}

void initCommands() {
    IRPrefab::Camera::registerStandardKeyboardCommands();
    IRCommand::registerCaptureCommands();
}

// Spawn one SDF shape at a ground-plane position. +Z is downward in this iso
// convention, so a shape of half-height h has its base at z ≈ +h and the floor
// sits just beyond.
void createShape(vec3 position, IRRender::ShapeType type, vec4 params, Color color) {
    IREntity::createEntity(C_LocalTransform{position}, C_ShapeDescriptor{type, params, color});
}

void initEntities() {
    // A wide thin floor so the fog mask has a continuous surface that fades
    // visible → explored → unexplored across the screen. Centered on origin.
    constexpr float kFloorZ = 5.0f;
    createShape(
        vec3(0.0f, 0.0f, kFloorZ),
        IRRender::ShapeType::BOX,
        vec4(96.0f, 96.0f, 2.0f, 0.0f),
        Color{150, 150, 160, 255}
    );

    // A few simple SDF primitives sitting on the floor inside the visible
    // circle, so the bright (visible) region has recognizable content.
    createShape(
        vec3(0.0f, 0.0f, 0.0f),
        IRRender::ShapeType::BOX,
        vec4(7, 7, 7, 0),
        Color{100, 200, 220, 255}
    );
    createShape(
        vec3(-12.0f, 8.0f, 0.0f),
        IRRender::ShapeType::SPHERE,
        vec4(4, 4, 4, 0),
        Color{220, 180, 100, 255}
    );
    createShape(
        vec3(12.0f, -8.0f, 0.0f),
        IRRender::ShapeType::CYLINDER,
        vec4(3, 3, 7, 0),
        Color{100, 220, 140, 255}
    );
    createShape(
        vec3(10.0f, 10.0f, 0.0f),
        IRRender::ShapeType::CONE,
        vec4(4, 4, 8, 0),
        Color{220, 140, 100, 255}
    );

    // #2008 column-cull regression canary: a TALL voxel pillar standing on an
    // UNEXPLORED column (XY = (-22,-22), Euclidean distance ~31 > the reveal+band
    // radius of 26). It is a voxel set (not an SDF shape) so it travels the
    // voxel-pool path — VOXEL_TO_TRIXEL_STAGE_1 → c_voxel_visibility_compact —
    // which is exactly where #2008 culls unexplored-column voxels.
    //
    // -X-Y projects DOWN-screen in this iso (the +X+Y cone sits at the top of
    // the disk), so the pillar's base sits below the bright disk and its 44-tall
    // extent projects its top straight up OVER the visible disk. Before #2008
    // the whole pillar rasterized and FOG_TO_TRIXEL hard-blacked every pixel
    // (its column is unexplored), painting a black silhouette across the lit
    // disk — the reported bug. With the cull the pillar's voxels never
    // rasterize, so the disk stays clean.
    //
    // The canary is therefore a NEGATIVE one: in the fixed state the pillar is
    // invisible and the zoom-2 disk is unmarred; if the cull regresses, the
    // black silhouette reappears over the disk and the shot diff catches it.
    IREntity::createEntity(
        C_LocalTransform{vec3(-22.0f, -22.0f, -19.0f)},
        C_VoxelSetNew{IRMath::ivec3{5, 5, 44}, Color{220, 70, 200, 255}, true}
    );

    // Canvas lighting attachments + fog. The voxel-pool canvas prefab doesn't
    // bundle these, so the AO / sun-shadow / light-volume / fog systems'
    // archetype filters wouldn't otherwise match the main canvas and they'd
    // silently skip it. (Copied from shape_debug's canvas-setup block.)
    const IREntity::EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    const IRMath::ivec2 canvasSize =
        IREntity::getComponent<C_TriangleCanvasTextures>(mainCanvas).size_;
    IREntity::setComponent(mainCanvas, C_CanvasAOTexture{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasSunShadow{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasLightVolume{});
    IREntity::setComponent(mainCanvas, C_TrixelCanvasRenderBehavior{});
    IREntity::setComponent(mainCanvas, C_CanvasFogOfWar{});

    // High, slightly off-axis sun so each shape casts a visible shadow.
    IRRender::setSunDirection(vec3(0.35f, 0.85f, -0.4f));

    // --moving-observer drives the reveal per-frame instead (see
    // driveMovingObserver); leave the fog all-unexplored at init so the first
    // frame's clear+reveal owns the whole grid.
    if (g_movingObserver) {
        return;
    }

    // VISIBLE state: a reveal circle around the origin. Everything inside
    // renders at full color.
    IRPrefab::Fog::revealRadius(0, 0, kRevealRadius);

    // EXPLORED state: a "memory" band of cells in the Euclidean rings just
    // outside the visible circle. revealRadius leaves these untouched, so
    // setting them to kFogStateExplored produces the desaturate+darken band
    // the fog pass applies to remembered-but-not-visible terrain. Beyond the
    // band, cells stay kFogStateUnexplored (black) — the third state.
    const int innerSq = kRevealRadius * kRevealRadius;
    const int outerSq = (kRevealRadius + kBandWidth) * (kRevealRadius + kBandWidth);
    for (int wy = -(kRevealRadius + kBandWidth); wy <= kRevealRadius + kBandWidth; ++wy) {
        for (int wx = -(kRevealRadius + kBandWidth); wx <= kRevealRadius + kBandWidth; ++wx) {
            const int distSq = wx * wx + wy * wy;
            if (distSq > innerSq && distSq <= outerSq) {
                IRPrefab::Fog::setCell(wx, wy, IRComponents::kFogStateExplored);
            }
        }
    }
}

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
// `--moving-observer` (#2009) swaps the static grid reveal for a per-frame
// analytic VISION CIRCLE (`Fog::setVisionCircle`) orbiting the origin in
// sub-cell steps. This is the vehicle for inspecting the SMOOTH reveal: the
// disc is evaluated per pixel from the continuous world column, so its edge is
// crisp at render resolution and slides smoothly across voxels (partial-voxel
// reveal) as the float center moves — no grid write, no per-frame texture
// upload, no per-cell popping. The default (no flag) static scene drives the
// VOXELIZED grid reveal instead and owns the committed render-verify refs, so
// the two reveal styles sit side by side in one demo.
//
// `--player-walk` (#2009) is the detached-player payoff: an SDF pillar "player"
// walks a straight line in sub-voxel per-frame steps while `setVisionCircle`
// keeps its analytic disc centered on the moving float position. Six fixed-
// camera shots capture the glide; flat floor tiles in the player's path resolve
// as partial CRESCENTS at the smooth leading edge — a partial reveal the
// cell-snapped `revealRadius` path cannot produce. This is the proof that the
// smooth reveal tracks a real moving entity with sub-voxel fidelity, not just a
// synthetic orbit.
//
// Fog wiring (canvas component + pipeline position) is faithful to the
// pre-removal shape_debug setup; the rest of the skeleton follows the smaller
// day_cycle demo.

#include <irreden/ir_args.hpp>
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

// --moving-observer analytic vision circle (#2009). The center orbits the
// origin in sub-cell per-frame steps (orbit × angular-step ≈ 0.4 cell/frame
// < 1 cell) so successive frames land on distinct sub-cell offsets — the case
// that exposes per-cell popping if the reveal snapped to integers. The disc
// edge uses the default antialiased softness (kFogVisionEdgeDefault).
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

bool g_movingObserver = false; // --moving-observer: per-frame analytic vision circle
int g_observerFrame = 0;       // deterministic frame index for the orbit

// Per-frame hook for --moving-observer: point the single analytic vision
// circle at a smoothly-advancing float center. No grid write and no texture
// upload — the shader evaluates the disc per pixel, so the reveal tracks the
// sub-cell motion with a crisp, smoothly-sliding edge.
void driveMovingObserver() {
    const float theta = static_cast<float>(g_observerFrame) * kObserverAngularStep;
    const float cx = kObserverOrbit * IRMath::cos(theta);
    const float cy = kObserverOrbit * IRMath::sin(theta);
    IRPrefab::Fog::setVisionCircle(cx, cy, static_cast<float>(kRevealRadius));
    ++g_observerFrame;
}

// --player-walk (#2009 proof): a "detached player" — an SDF pillar marker —
// walks in a straight line at a SUB-VOXEL per-frame step while its analytic
// vision circle (radius kWalkVisionRadius) tracks its float position. The camera
// is fixed (origin), so across the captured sequence the disc + marker slide
// smoothly to the side, the disc edge advances by fractional-voxel amounts, the
// floor reveals (and re-hides behind the trailing edge), and the flat floor
// tiles in its path resolve as crisp partial CRESCENTS that never snap to the
// cell grid — the property a single still can't show and the voxel-grid
// `revealRadius` path cannot produce.
constexpr float kWalkVisionRadius = 8.0f;
constexpr float kWalkStepPerFrame = 0.22f; // sub-voxel per-render-frame advance (< 1 cell)
constexpr float kWalkStartX = -3.0f;
constexpr float kWalkY = 0.0f;
constexpr float kWalkGroundZ = 0.0f;

// Fixed-camera sequence: identical shots so the auto-screenshot captures the
// SAME view at successive frames. The player advances kWalkStepPerFrame (a
// sub-voxel step) every render frame; the capture cadence samples roughly every
// fifth frame, so consecutive stills show the disc + marker shifted ~1 cell —
// but the underlying motion is sub-voxel, and the disc edge slices the low
// landmarks into smooth crescents (revealing one, re-hiding another). A partial
// crescent is something a cell-snapped grid reveal cannot produce; that plus the
// smoothly-sliding edge are the movement+shape fidelity proof.
constexpr IRVideo::AutoScreenshotShot kWalkShots[] = {
    {7.0f, vec2(0, 0), 0.0f, "fog_walk_0"},
    {7.0f, vec2(0, 0), 0.0f, "fog_walk_1"},
    {7.0f, vec2(0, 0), 0.0f, "fog_walk_2"},
    {7.0f, vec2(0, 0), 0.0f, "fog_walk_3"},
    {7.0f, vec2(0, 0), 0.0f, "fog_walk_4"},
    {7.0f, vec2(0, 0), 0.0f, "fog_walk_5"},
};

bool g_playerWalk = false; // --player-walk: walking detached player + tracking vision circle
int g_walkFrame = 0;       // deterministic per-frame index for the walk
IREntity::EntityId g_playerEntity{}; // the moving marker, repositioned each frame

// Per-frame RENDER-front hook for --player-walk: advance the player's float
// position a sub-voxel step, move the marker entity there (UPDATE's
// PROPAGATE_TRANSFORM re-places it within a frame), and re-point the analytic
// vision circle at it. Render-driven so the walk advances in lockstep with the
// render-frame-counted auto-screenshot rather than the wall-clock UPDATE step.
void drivePlayerWalk() {
    // This hook fires every frame, INCLUDING the warmup frames that run before
    // the first capture. Hold the marker at the start through warmup so the
    // captured sequence begins at kWalkStartX (otherwise the warmup frames walk
    // the player past the framed region before shot 0). The vision circle is
    // still set during warmup so the disc is present in the very first capture.
    if (g_walkFrame < g_autoWarmupFrames) {
        IRPrefab::Fog::setVisionCircle(kWalkStartX, kWalkY, kWalkVisionRadius);
        ++g_walkFrame;
        return;
    }
    const int walkedFrames = g_walkFrame - g_autoWarmupFrames;
    const float px = kWalkStartX + static_cast<float>(walkedFrames) * kWalkStepPerFrame;
    IREntity::setComponent(g_playerEntity, C_LocalTransform{vec3(px, kWalkY, kWalkGroundZ)});
    IRPrefab::Fog::setVisionCircle(px, kWalkY, kWalkVisionRadius);
    ++g_walkFrame;
}

} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    IRArgs::Parser args(
        "fog_demo — fog-of-war render-pass demo (FOG_TO_TRIXEL) + cross-host smoke coverage."
    );
    args.flag(
        "--moving-observer",
        "Per-frame analytic vision circle orbiting the origin (smooth reveal) "
        "instead of the static grid reveal"
    );
    args.flag(
        "--player-walk",
        "Walking detached-player marker with a tracking analytic vision circle "
        "(sub-voxel crescent reveal proof); skips the static grid reveal"
    );
    args.parse(argc, argv);
    g_autoWarmupFrames = args.autoScreenshotWarmupFrames();
    g_movingObserver = args.getFlag("--moving-observer");
    g_playerWalk = args.getFlag("--player-walk");

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
    // day_cycle sun hook) that re-points the analytic vision circle at the
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

    // --player-walk: same render-front placement as --moving-observer. The walk
    // MUST advance per render frame, not per UPDATE tick: the UPDATE pipeline
    // runs on a wall-clock fixed timestep, so a walk hook there races ahead of
    // the auto-screenshot's render-frame warmup/settle/capture counting and the
    // captured disc overshoots its framing. Driven here, one walk step lands per
    // render frame, in lockstep with the capture counter. The marker entity's
    // C_WorldTransform is refreshed by UPDATE's PROPAGATE_TRANSFORM at most one
    // frame later — invisible at the sub-voxel per-frame step.
    if (g_playerWalk) {
        IRSystem::SystemId walkTickId = IRSystem::createSystem<C_Name>(
            "FogPlayerWalkTick",
            [](C_Name &) {},
            []() { drivePlayerWalk(); }
        );
        renderPipeline.push_front(walkTickId);
    }

    if (g_autoWarmupFrames > 0) {
        IRVideo::AutoScreenshotConfig cfg{};
        cfg.warmupFrames_ = g_autoWarmupFrames;
        cfg.settleFrames_ = 3;
        // --player-walk captures a fixed-camera sequence (the walking reveal);
        // the default captures the three static fog-boundary shots.
        if (g_playerWalk) {
            cfg.shots_ = kWalkShots;
            cfg.numShots_ = sizeof(kWalkShots) / sizeof(kWalkShots[0]);
        } else {
            cfg.shots_ = kShots;
            cfg.numShots_ = sizeof(kShots) / sizeof(kShots[0]);
        }
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

    // The default + --moving-observer scenes dress the floor with SDF primitives
    // and the #2008 column-cull pillar canary. --player-walk skips ALL of them:
    // it wants a clean floor so the gliding disc + marker read clearly, and the
    // tall shapes' iso-projected tops poke through the disc edge in confusing
    // ways. The walk supplies its own low landmarks below.
    if (!g_playerWalk) {
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
    }

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

    // --player-walk: spawn the moving "player" marker — a bright vertical pillar
    // that reads clearly above the floor as it walks. The per-frame walk hook
    // repositions it + re-points its analytic vision circle each tick. The grid
    // stays all-unexplored, so only the disc tracking the player reveals the
    // floor + the low landmarks it sweeps over — the cleanest read of a crisp
    // edge tracking a smoothly-moving entity.
    if (g_playerWalk) {
        // Flat colored floor tiles in the player's FORWARD path (the walk runs
        // toward +X). The smoothly-advancing leading edge sweeps over them one by
        // one, slicing each into a growing crescent before fully revealing it: the
        // center tile is lit from the first frame for stable content, the +X tiles
        // reveal mid- and late-walk. Placed ahead (never behind) so each only
        // ever reveals — the trailing edge re-hiding a tile's floor footprint
        // produces a lighting seam we don't want competing with the reveal. Flat-
        // on-the-floor (not floating) so they neither occlude floor behind them
        // nor cast offset shadows; the disc edge is the only thing shaping them.
        // A partial crescent is impossible for a cell-snapped grid reveal — that
        // plus the smoothly-sliding edge is the shape+movement fidelity proof.
        constexpr float kTileZ = 2.7f; // flush on the floor surface (top at z≈3)
        createShape(
            vec3(2.0f, 4.0f, kTileZ),
            IRRender::ShapeType::CYLINDER,
            vec4(2.0f, 2.0f, 0.3f, 0.0f),
            Color{110, 150, 230, 255}
        );
        createShape(
            vec3(7.0f, -2.0f, kTileZ),
            IRRender::ShapeType::CYLINDER,
            vec4(2.5f, 2.5f, 0.3f, 0.0f),
            Color{90, 210, 130, 255}
        );
        createShape(
            vec3(10.0f, 2.0f, kTileZ),
            IRRender::ShapeType::CYLINDER,
            vec4(2.5f, 2.5f, 0.3f, 0.0f),
            Color{220, 180, 90, 255}
        );

        g_playerEntity = IREntity::createEntity(
            C_LocalTransform{vec3(kWalkStartX, kWalkY, kWalkGroundZ)},
            C_ShapeDescriptor{
                IRRender::ShapeType::CYLINDER,
                vec4(1.5f, 1.5f, 5.0f, 0.0f),
                Color{240, 80, 80, 255}
            }
        );
        return;
    }

    // --moving-observer drives an analytic vision circle per-frame instead
    // (see driveMovingObserver). Leave the grid all-unexplored: everything
    // outside the moving disc reads as black, so the smooth circle stands
    // alone against unrevealed terrain — the cleanest read of the sub-voxel
    // edge with no grid memory competing.
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

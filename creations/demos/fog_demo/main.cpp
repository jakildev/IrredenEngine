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
#include <irreden/render/entity_canvas.hpp>

// Scene components.
#include <irreden/common/command_suite_capture.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
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
#include <irreden/render/systems/system_entity_canvas_to_framebuffer.hpp>
#include <irreden/render/systems/system_propagate_canvas_rotation.hpp>
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
#include <irreden/voxel/systems/system_rebuild_detached_voxels.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>

#include <array>
#include <cstdio>
#include <list>
#include <vector>

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

// --edge-zoom (#2125 filled cross-section; #2124 P1): a STATIC analytic vision
// circle at the origin with VOXEL objects straddling its boundary, zoomed in so
// the cut edge fills the frame. Validates the cut-face cross-section: a
// boundary-cut voxel object caps with a FILLED interior wall (not a see-through
// hole or black wedge). Two mechanisms compose in VOXEL_TO_TRIXEL_STAGE_1/2:
// columns fully outside the disc are dropped (#2102 own-column clip — the hidden
// half), and a revealed boundary voxel emits the interior VERTICAL face toward a
// fog-hidden neighbor column (#2125 cut face — caps the revealed half). Cut faces
// show only on CAMERA-VISIBLE cut surfaces, so at cardinal yaw 0 the -X-facing
// cut (the green slab) shows its filled wall while the +X/+Y-facing cuts (the
// pillars) cut on back faces and read as a clean object end. The grid stays
// all-unexplored, so only the disc reveals — the cleanest read of the cut wall
// against the floor edge.
bool g_edgeZoom = false; // --edge-zoom
constexpr float kEdgeVisionRadius = 9.0f;

// ROI crop over the green slab's cut face at zoom9 (engine/render/CLAUDE.md
// "Verifying render changes" asks for an ROI-crop pair alongside full-frame
// shots on render PRs touching the trixel pipeline). Bounds are a per-host
// iteration point (see the shape_debug kCrops* note); tuned against this
// host's HiDPI 2560x1440 framebuffer.
constexpr IRVideo::RoiCrop kCropsEdgeZoom9[] = {
    {1200, 700, 480, 350, "cutface_slab"},
};

// Origin-centered shots at climbing magnification so the disc boundary (and the
// voxel objects straddling it) is sampled at multiple pixel scales — the
// before/after surface for the cross-section fix.
constexpr IRVideo::AutoScreenshotShot kEdgeShots[] = {
    {5.0f, vec2(0, 0), 0.0f, "fog_edge_zoom5"},
    {9.0f,
     vec2(0, 0),
     0.0f,
     "fog_edge_zoom9",
     kCropsEdgeZoom9,
     sizeof(kCropsEdgeZoom9) / sizeof(kCropsEdgeZoom9[0])},
    {14.0f, vec2(0, 0), 0.0f, "fog_edge_zoom14"},
};

// --detached-edge (#2127 filled cross-section on a DETACHED canvas; #2124 P3):
// the SAME static origin vision circle as --edge-zoom, but the boundary-
// straddling object is a WORLD-PLACED DETACHED_REVOXELIZE solid (its own canvas +
// pool, composited by ENTITY_CANVAS_TO_FRAMEBUFFER) instead of a GRID voxel set. A
// detached canvas carries no fog of its own, so this validates that the WORLD fog
// + observers thread into its STAGE_1/STAGE_2 dispatch and each voxel's WORLD
// column is recovered from worldCellOffset (detachedWorldReceive) — the solid
// cross-sections against the world boundary exactly like the GRID twin, instead of
// rendering whole (no fog) or fully black. Identity rotation keeps the re-voxelize
// raster on its deterministic SOURCE path (a spinning solid round-to-cell
// speckles, #1557); the cut-face code is rotation-agnostic, so the static pose
// proves the mechanism deterministically.
bool g_detachedEdge = false; // --detached-edge
constexpr float kDetachedVisionRadius = 9.0f;
constexpr IRMath::ivec2 kDetachedCanvasSize{200, 200};
constexpr IRMath::ivec3 kDetachedPoolSize{24, 24, 24};
constexpr IRMath::ivec3 kDetachedSolidSize{16, 8, 4};

// Origin-centered, matching the --edge-zoom framing so the detached cut wall reads
// against the same floor edge as the GRID twin.
constexpr IRVideo::AutoScreenshotShot kDetachedEdgeShots[] = {
    {5.0f, vec2(0, 0), 0.0f, "fog_detached_edge_zoom5"},
    {9.0f, vec2(0, 0), 0.0f, "fog_detached_edge_zoom9"},
};

// --edge-smooth (#2126 Mode B): the SAME boundary-straddling voxel scene as
// --edge-zoom, but the vision circle carries a wide edge softness so the reveal
// has a SMOOTH analytic band, not a hard column-quantized edge. P1's per-voxel
// own-column drop (#2102) culls any column with reveal < 0.5, which pins the
// object silhouette to the binary radius while the floor fades past it on the
// soft band; P2 drops only FULLY-hidden columns (reveal <= 0) so the partially-
// revealed boundary columns rasterize and FOG_TO_TRIXEL fades the object's
// silhouette + cut wall on the same curve as the floor. This is the before/after
// surface for the P2 fix; same geometry + camera as --edge-zoom so a side-by-side
// reads the smooth silhouette directly.
bool g_edgeSmooth = false;              // --edge-smooth
constexpr float kEdgeSmoothEdge = 3.0f; // world-unit soft-band half-width (≫ the 1-cell lattice)

constexpr IRVideo::AutoScreenshotShot kEdgeSmoothShots[] = {
    {5.0f, vec2(0, 0), 0.0f, "fog_edge_smooth5"},
    {9.0f, vec2(0, 0), 0.0f, "fog_edge_smooth9"},
    {14.0f, vec2(0, 0), 0.0f, "fog_edge_smooth14"},
};

// --edge-yaw-sweep (#2128 P4): the edge-zoom cross-section under CONTINUOUS
// camera yaw. Reuses the static --edge-zoom scene (same boundary voxel objects +
// origin vision circle) but steps the camera Z-yaw in fine increments inside one
// cardinal quadrant (residual yaw 0.05..0.70 rad < π/4, constant visible-face
// triplet) so the per-axis rotation route (perAxisRoute 1/2/3) drives the raster.
// The headline P4 check: a boundary-cut object must keep its FILLED interior
// cross-section through the whole sweep — no holes opening/closing, no flicker —
// the same wall the cardinal --edge-zoom shots show at yaw 0. Built dynamically
// (one shot per step) because the auto-screenshot harness applies shot.yawRadians_
// via Camera::setYaw per shot. jitter_probe the sequence to score temporal
// stability. Implies --edge-zoom (it owns the scene).
bool g_edgeYawSweep = false; // --edge-yaw-sweep
std::vector<IRVideo::AutoScreenshotShot> g_edgeYawSweepShots;
std::vector<std::array<char, 40>> g_edgeYawSweepShotLabels;

} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    // Register this demo's custom flags on the engine-owned parser BEFORE init;
    // init parses (engine-common args + these flags) as its first action, so
    // --help / --auto-screenshot / --config-preset come for free and --help
    // exits before any window/GL/Metal init.
    IREngine::args().flag(
        "--moving-observer",
        "Per-frame analytic vision circle orbiting the origin (smooth reveal) "
        "instead of the static grid reveal"
    );
    IREngine::args().flag(
        "--player-walk",
        "Walking detached-player marker with a tracking analytic vision circle "
        "(sub-voxel crescent reveal proof); skips the static grid reveal"
    );
    IREngine::args().flag(
        "--edge-zoom",
        "Static analytic vision circle with VOXEL objects straddling its "
        "boundary, zoomed on the cut edge (#2125 filled cross-section); "
        "skips the static grid reveal"
    );
    IREngine::args().flag(
        "--detached-edge",
        "Like --edge-zoom but the boundary-straddling object is a world-placed "
        "DETACHED_REVOXELIZE solid (#2127 detached cross-section); skips the "
        "static grid reveal"
    );
    IREngine::args().flag(
        "--edge-smooth",
        "Like --edge-zoom but with a wide soft vision-circle band (#2126 Mode B "
        "smooth cross-section) so the cut wall follows the analytic disc edge"
    );
    IREngine::args().flag(
        "--edge-yaw-sweep",
        "The --edge-zoom cross-section under a continuous camera-yaw sweep "
        "(per-axis rotation route, #2128 P4); implies --edge-zoom"
    );
    IREngine::init(argc, argv);
    g_autoWarmupFrames = IREngine::args().autoScreenshotWarmupFrames();
    g_movingObserver = IREngine::args().getFlag("--moving-observer");
    g_playerWalk = IREngine::args().getFlag("--player-walk");
    g_edgeZoom = IREngine::args().getFlag("--edge-zoom");
    g_detachedEdge = IREngine::args().getFlag("--detached-edge");
    g_edgeSmooth = IREngine::args().getFlag("--edge-smooth");
    g_edgeYawSweep = IREngine::args().getFlag("--edge-yaw-sweep");
    // --edge-yaw-sweep owns the same scene as --edge-zoom (boundary objects +
    // origin vision circle); it only swaps the static climbing-zoom shots for a
    // yaw sweep, so turn the edge scene on.
    if (g_edgeYawSweep) {
        g_edgeZoom = true;
    }
    // The reveal modes are mutually exclusive; precedence: --detached-edge, then
    // --edge-zoom / --edge-yaw-sweep, then --edge-smooth, then --player-walk, then
    // --moving-observer (each owns its own scene + shots).
    if (g_detachedEdge) {
        if (g_edgeZoom || g_edgeYawSweep || g_edgeSmooth || g_playerWalk || g_movingObserver) {
            IR_LOG_INFO(
                "--detached-edge overrides --edge-zoom / --edge-yaw-sweep / --edge-smooth / "
                "--player-walk / --moving-observer"
            );
        }
        g_edgeZoom = false;
        g_edgeYawSweep = false;
        g_edgeSmooth = false;
        g_playerWalk = false;
        g_movingObserver = false;
    } else if (g_edgeZoom) {
        if (g_edgeSmooth || g_playerWalk || g_movingObserver) {
            IR_LOG_INFO("--edge-zoom overrides --edge-smooth / --player-walk / --moving-observer");
        }
        g_edgeSmooth = false;
        g_playerWalk = false;
        g_movingObserver = false;
    } else if (g_edgeSmooth) {
        if (g_playerWalk || g_movingObserver) {
            IR_LOG_INFO("--edge-smooth overrides --player-walk / --moving-observer");
        }
        g_playerWalk = false;
        g_movingObserver = false;
    } else if (g_playerWalk && g_movingObserver) {
        IR_LOG_INFO(
            "--player-walk and --moving-observer are mutually exclusive; ignoring --moving-observer"
        );
        g_movingObserver = false;
    }

    IR_LOG_INFO("Starting creation: fog_demo");
    initSystems();
    initCommands();
    initEntities();
    IREngine::gameLoop();
    return 0;
}

void initSystems() {
    std::list<IRSystem::SystemId> updatePipeline = {
        IRSystem::createSystem<IRSystem::LOD_UPDATE>(),
        IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
        IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
        IRSystem::createSystem<IRSystem::REBUILD_GRID_VOXELS>(),
    };
    // --detached-edge adds the world-placed DETACHED_REVOXELIZE path (#2127):
    // PROPAGATE_CANVAS_ROTATION publishes worldPlaced_ + worldCellOffset_ onto the
    // canvas (so STAGE_1/2 recover each detached voxel's world column), and
    // REBUILD_DETACHED_VOXELS fills the private pool. Must run AFTER
    // UPDATE_VOXEL_SET_CHILDREN. Added only for that scene so the other reveal
    // modes keep their committed render-verify refs byte-identical.
    if (g_detachedEdge) {
        updatePipeline.push_back(IRSystem::createSystem<IRSystem::PROPAGATE_CANVAS_ROTATION>());
        updatePipeline.push_back(IRSystem::createSystem<IRSystem::REBUILD_DETACHED_VOXELS>());
    }
    IRSystem::registerPipeline(IRTime::Events::UPDATE, updatePipeline);

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
        }
    );
    // --detached-edge composites the world-placed detached canvas (with its
    // cross-sectioned voxels from STAGE_1/2) onto the main framebuffer between
    // TRIXEL_TO_FRAMEBUFFER and FRAMEBUFFER_TO_SCREEN (#2127). Added only for that
    // scene so the other reveal modes keep their committed refs byte-identical.
    if (g_detachedEdge) {
        renderPipeline.push_back(IRSystem::createSystem<IRSystem::ENTITY_CANVAS_TO_FRAMEBUFFER>());
    }
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>());

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
        // --detached-edge zooms on a detached-canvas cross-section; --edge-yaw-sweep
        // sweeps the GRID cross-section through the per-axis rotation route;
        // --edge-zoom / --edge-smooth zoom on the GRID cross-section clip edge (hard
        // vs smooth disc); --player-walk captures the walking reveal sequence; the
        // default captures the three static fog-boundary shots.
        if (g_detachedEdge) {
            cfg.shots_ = kDetachedEdgeShots;
            cfg.numShots_ = sizeof(kDetachedEdgeShots) / sizeof(kDetachedEdgeShots[0]);
        } else if (g_edgeYawSweep) {
            // Fixed zoom + origin, step yaw across [0.05, 0.70] rad — one cardinal
            // quadrant (< π/4), constant visible-face triplet — so the cut-face
            // cross-section is exercised purely through the per-axis rotation route
            // (#2128). Mirror of shape_debug's --yaw-sweep shot construction.
            constexpr float kEdgeSweepZoom = 9.0f;
            constexpr int kEdgeSweepShots = 24;
            constexpr float kYawLo = 0.05f;
            constexpr float kYawHi = 0.70f;
            g_edgeYawSweepShotLabels.reserve(kEdgeSweepShots);
            g_edgeYawSweepShots.reserve(kEdgeSweepShots);
            for (int i = 0; i < kEdgeSweepShots; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(kEdgeSweepShots - 1);
                auto &label = g_edgeYawSweepShotLabels.emplace_back();
                std::snprintf(
                    label.data(),
                    label.size(),
                    "fog_edge_yaw_%03d_of_%03d",
                    i,
                    kEdgeSweepShots
                );
                IRVideo::AutoScreenshotShot shot{};
                shot.zoom_ = kEdgeSweepZoom;
                shot.cameraIso_ = vec2(0.0f, 0.0f);
                shot.yawRadians_ = kYawLo + (kYawHi - kYawLo) * t;
                shot.label_ = label.data();
                g_edgeYawSweepShots.push_back(shot);
            }
            cfg.shots_ = g_edgeYawSweepShots.data();
            cfg.numShots_ = static_cast<int>(g_edgeYawSweepShots.size());
            IR_LOG_INFO(
                "Edge yaw-sweep: {} shots, yaw {}->{} rad at origin zoom={}",
                cfg.numShots_,
                kYawLo,
                kYawHi,
                kEdgeSweepZoom
            );
        } else if (g_edgeZoom) {
            cfg.shots_ = kEdgeShots;
            cfg.numShots_ = sizeof(kEdgeShots) / sizeof(kEdgeShots[0]);
        } else if (g_edgeSmooth) {
            cfg.shots_ = kEdgeSmoothShots;
            cfg.numShots_ = sizeof(kEdgeSmoothShots) / sizeof(kEdgeSmoothShots[0]);
        } else if (g_playerWalk) {
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

// The cross-section scenes' shared VOXEL ground slab. centerAroundOrigin
// places the 60×60×3 cells at [-29,30]²×[4,6], so the radius-9 vision disc is
// fully interior and the whole cut arc runs through pool voxels the occlusion
// bitfield knows — an off-origin slab covers only part of the arc and the
// uncovered segments cut to black.
void createEdgeGroundSlab() {
    IREntity::createEntity(
        C_LocalTransform{vec3(0.0f, 0.0f, 5.0f)},
        C_VoxelSetNew{IRMath::ivec3{60, 60, 3}, Color{90, 100, 120, 255}, true}
    );
}

void initEntities() {
    // A wide thin floor so the fog mask has a continuous surface that fades
    // visible → explored → unexplored across the screen. Centered on origin.
    // The cross-section scenes (--edge-zoom / --edge-smooth / --detached-edge)
    // skip it and bring their own VOXEL ground slab instead: the analytic fog
    // cut (#2124) decides solid-vs-air from the light-occlusion VOXEL bitfield,
    // which SDF geometry never enters, so an SDF surface straddling the disc
    // renders the cut band BLACK along whatever arc segments cross it (the
    // two-black-bands artifact) instead of capping with the toned cut colour.
    constexpr float kFloorZ = 5.0f;
    if (!g_edgeZoom && !g_edgeSmooth && !g_detachedEdge) {
        createShape(
            vec3(0.0f, 0.0f, kFloorZ),
            IRRender::ShapeType::BOX,
            vec4(96.0f, 96.0f, 2.0f, 0.0f),
            Color{150, 150, 160, 255}
        );
    }

    // The default + --moving-observer scenes dress the floor with SDF primitives
    // and the #2008 column-cull pillar canary. --player-walk / --edge-zoom /
    // --edge-smooth / --detached-edge skip ALL of them: each wants a clean floor so
    // its own content (the gliding disc + marker / the boundary-straddling voxel
    // objects) reads clearly without the tall shapes' iso-projected tops poking
    // through the disc.
    if (!g_playerWalk && !g_edgeZoom && !g_edgeSmooth && !g_detachedEdge) {
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

    // The BOUNDARY scenes (--edge-zoom / --edge-smooth / --detached-edge)
    // override to a STRAIGHT-DOWN sun. Their render-verify refs exist to
    // inspect the fog reveal boundary, and an angled sun drives shadow
    // TERMINATORS across the exact band under test — the pillar's terminator
    // crossing the slab-top fog arc composites into a kinked dark curve that
    // reads as a fog artifact (it was reported as one: the "sharp turn" that
    // appears to connect the elevated face's fade to the floor rim at the
    // wrong height). Fog x shadow composition stays covered by the default
    // grid scene's refs, which keep the angled sun.
    if (g_edgeZoom || g_edgeSmooth || g_detachedEdge) {
        IRRender::setSunDirection(vec3(0.0f, 0.0f, -1.0f));
    }

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

    // --edge-zoom (#2125 filled cross-section; #2124 P1) and --edge-smooth (#2126
    // P2 Mode B): a STATIC analytic vision circle at the origin with VOXEL objects
    // straddling its boundary. Set once here — nothing re-clears it, so it persists
    // across warmup/settle/capture — and leave the grid all-unexplored so ONLY the
    // disc reveals. Validates the cut-face cross-section: the hidden half of each
    // object is dropped (#2102 own-column clip) and the revealed half caps with a
    // FILLED interior wall (#2125 cut face) wherever a revealed boundary voxel faces
    // a fog-hidden neighbor column — no see-through hole, no black wedge. Cut faces
    // appear only on CAMERA-VISIBLE cut surfaces (cardinal yaw 0 sees -X/-Y/-Z), so
    // the green slab's -X cut shows its wall while the pillars' +X/+Y cuts fall on
    // back faces and read as a clean end.
    if (g_edgeZoom || g_edgeSmooth) {
        // --edge-zoom is the hard-disc binary cut (Mode A, edgeSoftness 0);
        // --edge-smooth uses a wide soft band (Mode B, #2126) over the IDENTICAL
        // geometry so the cut wall follows the analytic disc instead of stair-
        // stepping at the column boundary. The soft band is the only difference.
        const float edge = g_edgeSmooth ? kEdgeSmoothEdge : kFogVisionEdgeDefault;
        IRPrefab::Fog::setVisionCircle(0.0f, 0.0f, kEdgeVisionRadius, edge);

        // The ground slab's cut EDGE — the vertical rim where the disc crosses
        // it, camera-visible all around the near side — is the headline test
        // for the per-pixel silhouette. Before the nearest-cell keep, the
        // own-column drop ended this rim on the voxel lattice (a stair-stepped
        // ring); now the boundary cells are kept and FOG_TO_TRIXEL trims the
        // rim to the smooth analytic disc, matching the slab's top face at
        // game resolution.
        createEdgeGroundSlab();

        // Tall voxel pillar straddling the +X boundary: columns x∈[7,11] cross
        // the radius-9 disc, so its near half renders and its far half is dropped.
        // The cut is on the +X face (toward the hidden far columns), which is a
        // BACK face at cardinal yaw 0 — so the revealed half reads as a clean end
        // (no hole), and the cut wall itself would show after a +90°/180° yaw.
        // Iso +Z is downward, so center it below the floor surface to stand it up
        // on the floor (base near z≈4, top up-screen).
        IREntity::createEntity(
            C_LocalTransform{vec3(9.0f, 0.0f, -6.0f)},
            C_VoxelSetNew{IRMath::ivec3{4, 4, 20}, Color{120, 200, 240, 255}, true}
        );
        // A second pillar straddling the +Y boundary (up-screen side), a cut
        // angle the iso projection lays out differently from the +X pillar (also
        // a back-face cut at yaw 0).
        IREntity::createEntity(
            C_LocalTransform{vec3(0.0f, 9.0f, -6.0f)},
            C_VoxelSetNew{IRMath::ivec3{4, 4, 20}, Color{240, 160, 90, 255}, true}
        );
        // Low wide voxel slab straddling the -X boundary: columns x∈[-16,-2], so
        // the revealed boundary voxels face hidden columns across their -X face —
        // which IS camera-visible at yaw 0. This is the headline cut-face test:
        // its -X interior wall fills the cut instead of leaving a see-through hole.
        IREntity::createEntity(
            C_LocalTransform{vec3(-9.0f, 0.0f, 2.0f)},
            C_VoxelSetNew{IRMath::ivec3{14, 6, 3}, Color{130, 230, 150, 255}, true}
        );
        return;
    }

    // --detached-edge (#2127 / #2124 P3): the SAME static origin vision circle, but
    // the boundary-straddling object is a WORLD-PLACED DETACHED_REVOXELIZE solid on
    // its OWN canvas + pool — a canvas that carries no fog of its own. The
    // cross-section it shows is proof the WORLD fog + observers thread into the
    // detached STAGE_1/2 dispatch and each voxel's world column is recovered from
    // worldCellOffset. Mirrors the GRID green slab's headline -X cut (camera-visible
    // at yaw 0) so the two scenes read against the same floor edge.
    if (g_detachedEdge) {
        IRPrefab::Fog::setVisionCircle(0.0f, 0.0f, kDetachedVisionRadius);

        // The same ground slab as the GRID twin, created before the detached
        // canvas so it allocates from the MAIN canvas pool.
        createEdgeGroundSlab();

        // The solid lives in MODEL space centered on its pool (origin); the
        // entity's world position is its CENTER (-9 on X), shifting its world
        // columns to x∈[-17,-1]. With the radius-9 disc the x<-9 half is fog-hidden
        // (own-column drop) and the revealed boundary voxels face hidden columns
        // across their -X face (camera-visible at yaw 0) → a FILLED interior cut
        // wall, exactly like the GRID twin (no see-through hole, no black wedge).
        C_EntityCanvas canvas = IRPrefab::EntityCanvas::createWithVoxelPool(
            "fog_detached_solid",
            kDetachedCanvasSize,
            kDetachedPoolSize
        );
        // World-placed (the engine default since #1624) — NOT screen-locked — so
        // PROPAGATE_CANVAS_ROTATION publishes worldCellOffset/worldPlaced and the
        // detached STAGE_1/2 dispatch world-receives the fog.
        canvas.screenLocked_ = false;
        IREntity::createEntity(
            C_LocalTransform{vec3(0.0f)},
            C_VoxelSetNew{kDetachedSolidSize, Color{130, 230, 150, 255}, true, canvas.canvasEntity_}
        );
        // Identity rotation keeps the re-voxelize raster on its deterministic SOURCE
        // path (a spinning solid round-to-cell speckles, #1557); the cut-face code
        // is rotation-agnostic, so this static pose proves the world-column recovery.
        IREntity::createEntity(
            C_LocalTransform{vec3(-9.0f, 0.0f, 2.0f)},
            C_RotationMode{RotationMode::DETACHED_REVOXELIZE},
            canvas
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

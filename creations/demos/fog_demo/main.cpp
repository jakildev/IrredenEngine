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
// `--moving-observer` swaps the static grid reveal for a per-frame
// analytic VISION CIRCLE (`Fog::setVisionCircle`) orbiting the origin in
// sub-cell steps. This is the vehicle for inspecting the SMOOTH reveal: the
// disc is evaluated per pixel from the continuous world column, so its edge is
// crisp at render resolution and slides smoothly across voxels (partial-voxel
// reveal) as the float center moves — no grid write, no per-frame texture
// upload, no per-cell popping. The default (no flag) static scene drives the
// VOXELIZED grid reveal instead and owns the committed render-verify refs, so
// the two reveal styles sit side by side in one demo.
//
// `--player-walk` is the detached-player payoff: an SDF pillar "player"
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
#include <irreden/render/buffer.hpp>
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
#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
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
#include <irreden/render/systems/system_fog_reveal_eval.hpp>
#include <irreden/render/systems/system_fog_los_build.hpp>
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
#include <cstdlib>
#include <list>
#include <string>
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

// --moving-observer analytic vision circle. The center orbits the
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
bool g_luaFogSelftest = false;
bool g_luaFogCapSelftestDone = false;
bool g_luaFogSetupSelftestDone = false;
int g_luaFogProbePhase = 0;
IREntity::EntityId g_luaFogProbeEntity = IREntity::kNullEntity;

void requireLuaFogSelftest(bool condition, const char *message) {
    if (condition) {
        return;
    }
    IR_LOG_ERROR("LUA-FOG-PROBE FAIL: {}", message);
    std::exit(1);
}

void probeLuaFogUpload() {
    if (g_luaFogProbePhase > 1) {
        return;
    }
    IRRender::Buffer *buffer = IRRender::getNamedResource<IRRender::Buffer>("FogObserverData");
    FrameDataFogObservers observers{};
    buffer->getSubData(0, sizeof(observers), &observers);

    if (g_luaFogProbePhase == 0) {
        requireLuaFogSelftest(
            g_luaFogCapSelftestDone,
            "cap script did not complete all Lua assertions"
        );
        requireLuaFogSelftest(
            observers.visionCircleCount_ == kMaxFogVisionCircles,
            "IRFog Lua cap probe must upload exactly eight sources"
        );
        requireLuaFogSelftest(
            observers.visionCircles_[7].x == 160.0f,
            "IRFog Lua cap probe must retain the eighth source"
        );
        IREngine::getWorld().runScript(
            IREngine::resolveScriptPath("fog_binding_selftest.lua").c_str()
        );
        g_luaFogProbePhase = 1;
        return;
    }
    requireLuaFogSelftest(
        g_luaFogSetupSelftestDone,
        "setup script did not complete all Lua assertions"
    );
    requireLuaFogSelftest(
        observers.visionCircleCount_ == 2,
        "IRFog Lua two-source probe must upload exactly two sources"
    );
    requireLuaFogSelftest(
        observers.visionCircles_[0] == vec4(-10.0f, 0.0f, 4.0f, 0.0f) &&
            observers.visionCircles_[1] == vec4(10.0f, 0.0f, 4.0f, 0.0f),
        "IRFog Lua two-source probe uploaded unexpected circle records"
    );
    requireLuaFogSelftest(
        observers.visionCircleHeights_[0] == vec4(3.0f, 0.5f, 0.5f, 1.0f) &&
            observers.visionCircleHeights_[1] == vec4(3.0f, 0.5f, 0.5f, 1.0f),
        "IRFog Lua two-source probe uploaded unexpected height records"
    );
    requireLuaFogSelftest(
        observers.losSourceMask_ == (1 << 1) && observers.losSoftness(1) == 0.75f &&
            observers.losSoftness(0) == kFogLosHardGate,
        "IRFog Lua line-of-sight entry uploaded an unexpected gate"
    );
    IR_LOG_INFO(
        "LUA-FOG-PROBE sources={} centers={},{};{},{} observerZ={} zCostUp={} "
        "zCostDown={} freeBand={} PASS",
        observers.visionCircleCount_,
        observers.visionCircles_[0].x,
        observers.visionCircles_[0].y,
        observers.visionCircles_[1].x,
        observers.visionCircles_[1].y,
        observers.visionCircleHeights_[0].x,
        observers.visionCircleHeights_[0].y,
        observers.visionCircleHeights_[0].z,
        observers.visionCircleHeights_[0].w
    );
    g_luaFogProbePhase = 2;
}

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

// --player-walk: a "detached player" — an SDF pillar marker —
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

// --edge-zoom (filled cross-section): a STATIC analytic vision
// circle at the origin with VOXEL objects straddling its boundary, zoomed in so
// the cut edge fills the frame. Validates the cut-face cross-section: a
// boundary-cut voxel object caps with a FILLED interior wall (not a see-through
// hole or black wedge). Two mechanisms compose in VOXEL_TO_TRIXEL_STAGE_1/2:
// columns fully outside the disc are dropped (own-column clip — the hidden
// half), and a revealed boundary voxel emits the interior VERTICAL face toward a
// fog-hidden neighbor column (cut face — caps the revealed half). Cut faces
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

// --edge-sdf-blocker: the SAME --edge-zoom scene (hard-disc vision
// circle + boundary VOXEL objects) PLUS one SDF BOX carrying
// C_LightBlocker{blocksLOS_=true}, standing on the floor and straddling the -Y
// disc arc (an arc the voxel objects leave clear). The fog cut is the
// geometric cap: it is content-independent —
// any surface that rasterizes caps at the disc boundary, so the box's fog-hidden
// -Y half fills with the toned cut wall exactly like the voxel green slab on -X,
// with no light-occlusion-bitfield read. This scene is the permanent enabled-path
// regression gate that a blocksLOS_ SDF straddling the disc still caps (the
// geometric cap does not special-case or exclude blockers). -Y is camera-visible
// at cardinal yaw 0, so the cut face shows directly. The committed zoom5 + zoom9
// refs are the gate.
bool g_edgeSdfBlocker = false; // --edge-sdf-blocker
// The blocker BOX: centered on the -Y disc arc (y == -kEdgeVisionRadius at x=0)
// and standing up off the floor (iso +Z is downward; base ≈ floor top z=5, top
// up-screen). Its near (+Y) half falls inside the disc and renders; its far
// (-Y) half is fog-hidden and is where the cut wall must fill.
constexpr vec3 kSdfBlockerCenter{0.0f, -kEdgeVisionRadius, -1.0f};
constexpr vec4 kSdfBlockerHalfExtents{4.0f, 5.0f, 6.0f, 0.0f};
constexpr Color kSdfBlockerColor{200, 120, 220, 255};

// ROI crop over the SDF blocker's -Y cut face at zoom9 (per engine/render/
// CLAUDE.md's ROI-crop request). -Y projects DOWN-screen in this iso, so the
// crop sits below the frame center. Bounds are a per-host iteration point
// (see the shape_debug kCrops* note); tuned against this host's 2560x1440
// framebuffer.
constexpr IRVideo::RoiCrop kCropsEdgeSdfBlocker9[] = {
    {800, 640, 440, 380, "cutface_sdf_blocker"},
};

// Origin-centered shots matching the --edge-zoom framing so the SDF blocker's
// cut wall reads against the same floor edge as the voxel twins.
constexpr IRVideo::AutoScreenshotShot kEdgeSdfBlockerShots[] = {
    {5.0f, vec2(0, 0), 0.0f, "fog_edge_sdf_blocker_zoom5"},
    {9.0f,
     vec2(0, 0),
     0.0f,
     "fog_edge_sdf_blocker_zoom9",
     kCropsEdgeSdfBlocker9,
     sizeof(kCropsEdgeSdfBlocker9) / sizeof(kCropsEdgeSdfBlocker9[0])},
};

// --detached-edge (filled cross-section on a DETACHED canvas):
// the SAME static origin vision circle as --edge-zoom, but the boundary-
// straddling object is a WORLD-PLACED DETACHED_REVOXELIZE solid (its own canvas +
// pool, composited by ENTITY_CANVAS_TO_FRAMEBUFFER) instead of a GRID voxel set. A
// detached canvas carries no fog of its own, so this validates that the WORLD fog
// + observers thread into its STAGE_1/STAGE_2 dispatch and each voxel's WORLD
// column is recovered from worldCellOffset (detachedWorldReceive) — the solid
// cross-sections against the world boundary exactly like the GRID twin, instead of
// rendering whole (no fog) or fully black. Identity rotation keeps the re-voxelize
// raster on its deterministic SOURCE path (a spinning solid round-to-cell
// speckles); the cut-face code is rotation-agnostic, so the static pose
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

// --edge-smooth (Mode B): the SAME boundary-straddling voxel scene as
// --edge-zoom, but the vision circle carries a wide edge softness so the reveal
// has a SMOOTH analytic band, not a hard column-quantized edge. A per-voxel
// own-column drop that culls any column with reveal < 0.5 pins the
// object silhouette to the binary radius while the floor fades past it on the
// soft band; the cull drops only FULLY-hidden columns (reveal <= 0) so the
// partially-revealed boundary columns rasterize and FOG_TO_TRIXEL fades the
// object's silhouette + cut wall on the same curve as the floor. Same geometry
// + camera as --edge-zoom so a side-by-side reads the smooth silhouette
// directly.
bool g_edgeSmooth = false;              // --edge-smooth
constexpr float kEdgeSmoothEdge = 3.0f; // world-unit soft-band half-width (≫ the 1-cell lattice)

constexpr IRVideo::AutoScreenshotShot kEdgeSmoothShots[] = {
    {5.0f, vec2(0, 0), 0.0f, "fog_edge_smooth5"},
    {9.0f, vec2(0, 0), 0.0f, "fog_edge_smooth9"},
    {14.0f, vec2(0, 0), 0.0f, "fog_edge_smooth14"},
};

// --edge-zcost: the vision disc as an XY radius with a Z-COST HEIGHT
// PENALTY. The observer sits at floor level (kEdgeZCostObserverZ); a central
// TALL voxel pillar at the observer's XY has its base at the floor (revealed —
// |z - observerZ| small) and its top far above (penalized past the disc radius
// → faded then dropped), while a LOW cube a few cells over stays fully revealed
// at the same XY band. The pure height readout: matter far from the observer's
// height reveals less even at equal XY distance. The effective reveal distance
// is dist_xy + zCost * |z - observerZ|, evaluated per pixel in FOG_TO_TRIXEL and
// (own-column drop) per voxel in VOXEL_TO_TRIXEL_STAGE_1. Static, straight-down
// sun, all-unexplored grid (only the disc reveals) → deterministic refs. zCost 0
// would render byte-identical to --edge-zoom; the point of this scene is zCost>0.
bool g_edgeZCost = false; // --edge-zcost
constexpr float kEdgeZCostRadius = 14.0f;
constexpr float kEdgeZCostObserverZ = 4.5f; // ~floor level (iso +Z is downward)
constexpr float kEdgeZCostFactor = 1.0f;    // extra reach per world unit of |z - observerZ|

// ROI crop over the central pillar's height-fade transition at zoom9 (per
// engine/render/CLAUDE.md's ROI-crop request). Bounds are a per-host iteration
// point (see the shape_debug kCrops* note); tuned against this host's HiDPI
// 2560x1440 framebuffer.
constexpr IRVideo::RoiCrop kCropsEdgeZCost9[] = {
    {1000, 360, 560, 560, "zcost_pillar_fade"},
};

// Origin-centered shots matching the other edge scenes' framing so the pillar's
// height fade reads against the same floor edge.
constexpr IRVideo::AutoScreenshotShot kEdgeZCostShots[] = {
    {5.0f, vec2(0, 0), 0.0f, "fog_edge_zcost5"},
    {9.0f,
     vec2(0, 0),
     0.0f,
     "fog_edge_zcost9",
     kCropsEdgeZCost9,
     sizeof(kCropsEdgeZCost9) / sizeof(kCropsEdgeZCost9[0])},
};

// --edge-zcost-asym: the same XY-radius vision disc as --edge-zcost,
// generalized to ASYMMETRIC up/down height costs (freeBand 0). A pillar
// standing UP from the observer (top at z ≈ -24, same geometry as the
// --edge-zcost pillar) fades fast under kEdgeZCostAsymUpCost. A SHORT block
// standing DOWN from the observer at a separate X offset stays mostly
// revealed under the much smaller kEdgeZCostAsymDownCost — the asymmetry
// readout, each against its own achievable reach.
//
// The down side is deliberately SHORT, not a mirror of the up pillar's
// length: at this camera pose the render cull's usable reach is NOT
// symmetric in Z — content extending "up" (toward smaller Z / the camera)
// gets generous headroom, but content extending "down" (toward larger Z /
// away from the camera) is culled past roughly 4 world units. Reaching for
// the same ~20-unit magnitude the up pillar uses would silently render as an
// inert stub.
//
// The mechanism is NOT known, and the shadow-feeder sweep is NOT it. That
// inference is tempting — the sweep really does widen the cull only toward
// the sun, which for this scene's straight-up sun is the up side — but it is
// ruled out twice over. (a) Measured: disabling sun shadows zeroes
// `sweepDistance_` (`frameShadowFeederParams`), which returns
// `shadowFeederIsoBounds` to the un-widened viewport and changes nothing else
// about the cull — an isolating variant for the sweep alone — and the up
// pillar still rendered in full. (b) By construction: the sweep widens cull
// ADMISSION, not visibility; stage 2 returns before the colour +
// entity-id taps for any voxel in the widened region (`isShadowFeederIso`,
// c_voxel_to_trixel_stage_2_body.glsl, on this scene's yaw-0 path), so a
// feeder is never displayed and the widening cannot lengthen on-screen
// geometry. The open question is unresolved — treat it as unanswered.
//
// `kEdgeZCostAsymDownXOffset` sits close enough to the disc
// radius that the small achievable |Δz| still produces a measurable reveal
// delta between the shipped low cost and a mirrored (zCostUp-equal) cost.
bool g_edgeZCostAsym = false; // --edge-zcost-asym
constexpr float kEdgeZCostAsymUpCost = 1.0f;
constexpr float kEdgeZCostAsymDownCost = 0.25f;
constexpr float kEdgeZCostAsymFreeBand = 0.0f;
// X offset for the up pillar (mirrored to -this) — comfortably inside the
// radius-14 disc, matching --edge-zcost's own pillar distance budget.
constexpr float kEdgeZCostAsymUpXOffset = 6.0f;
// X offset for the down block — deliberately closer to the radius than the
// up pillar's offset (see the scene comment above): the achievable |Δz| on
// the down side is small, so distXY needs to already consume most of the
// radius budget for a modest cost difference to cross the reveal boundary.
constexpr float kEdgeZCostAsymDownXOffset = 12.0f;

constexpr IRVideo::RoiCrop kCropsEdgeZCostAsym9[] = {
    {700, 580, 400, 250, "zcost_asym_pillar_pair"},
};

constexpr IRVideo::AutoScreenshotShot kEdgeZCostAsymShots[] = {
    {5.0f, vec2(0, 0), 0.0f, "fog_edge_zcost_asym5"},
    {9.0f,
     vec2(0, 0),
     0.0f,
     "fog_edge_zcost_asym9",
     kCropsEdgeZCostAsym9,
     sizeof(kCropsEdgeZCostAsym9) / sizeof(kCropsEdgeZCostAsym9[0])},
};

// --edge-zcost-ceiling: the same XY-radius vision disc and pillar
// geometry as --edge-zcost, but with a penalty-free BAND around the observer's
// height (freeBand) and a near-radius zCostUp so the reveal collapses within
// ~1 unit past the band — a hard ceiling instead of a linear fade. Matter
// within freeBand units above the observer reads fully revealed; past it, the
// pillar cuts off sharply.
bool g_edgeZCostCeiling = false; // --edge-zcost-ceiling
constexpr float kEdgeZCostCeilingFreeBand = 8.0f;
// >= radius per unit so the reveal collapses within ~1 unit past the band —
// the "hard ceiling" readout rather than a linear fade.
constexpr float kEdgeZCostCeilingUpCost = kEdgeZCostRadius;

constexpr IRVideo::RoiCrop kCropsEdgeZCostCeiling9[] = {
    {1000, 360, 560, 560, "zcost_ceiling_pillar_cutoff"},
};

constexpr IRVideo::AutoScreenshotShot kEdgeZCostCeilingShots[] = {
    {5.0f, vec2(0, 0), 0.0f, "fog_edge_zcost_ceiling5"},
    {9.0f,
     vec2(0, 0),
     0.0f,
     "fog_edge_zcost_ceiling9",
     kCropsEdgeZCostCeiling9,
     sizeof(kCropsEdgeZCostCeiling9) / sizeof(kCropsEdgeZCostCeiling9[0])},
};

// --fog-debug-color: paint fully unexplored matter magenta instead of black, so
// matter the fog pass paints reads apart from matter the raster removed (the
// black background shows through both otherwise). Under --edge-zcost-ceiling it
// also arms the FOG-PAINT-PROBE: one readback after warmup that counts the
// central pillar's texels (entity-id low word) and how many carry the debug
// colour within kFogPaintProbeTolerance per channel. A pillar whose above-ceiling
// voxels are painted rather than dropped reads a large painted fraction.
bool g_fogDebugColor = false; // --fog-debug-color
bool g_perAxisOverflow = false; // --peraxis-overflow
constexpr Color kFogDebugUnexploredColor{255, 0, 255, 255};
// Same framing as kEdgeZCostCeilingShots; own labels so both variants gate.
// The last shot parks a non-cardinal yaw, so the scene renders through the
// per-axis rotation route, which FOG_TO_TRIXEL paints per occupied face: the
// above-ceiling pillar keeps its full height in the magenta unexplored colour.
constexpr float kEdgeZCostCeilingPaintYaw = 0.35f;
constexpr IRVideo::RoiCrop kCropsEdgeZCostCeilingPaintYaw9[] = {
    {1100, 0, 300, 920, "zcost_ceiling_yaw_pillar"},
};
constexpr IRVideo::AutoScreenshotShot kEdgeZCostCeilingPaintShots[] = {
    {5.0f, vec2(0, 0), 0.0f, "fog_edge_zcost_ceiling_paint5"},
    {9.0f,
     vec2(0, 0),
     0.0f,
     "fog_edge_zcost_ceiling_paint9",
     kCropsEdgeZCostCeiling9,
     sizeof(kCropsEdgeZCostCeiling9) / sizeof(kCropsEdgeZCostCeiling9[0])},
    {9.0f,
     vec2(0, 0),
     kEdgeZCostCeilingPaintYaw,
     "fog_edge_zcost_ceiling_paint_yaw9",
     kCropsEdgeZCostCeilingPaintYaw9,
     sizeof(kCropsEdgeZCostCeilingPaintYaw9) / sizeof(kCropsEdgeZCostCeilingPaintYaw9[0])},
};
constexpr IRVideo::AutoScreenshotShot kPerAxisOverflowShots[] = {
    {9.0f, vec2(0, 0), kEdgeZCostCeilingPaintYaw, "fog_peraxis_overflow_yaw9"},
};
constexpr int kFogPaintProbeTolerance = 2;
IREntity::EntityId g_ceilingPillar = IREntity::kNullEntity;
int g_fogPaintProbeFrame = 0;
bool g_fogPerAxisProbeDone = false;

bool matchesFogDebugColor(Color color) {
    const auto near = [](std::uint8_t channel, std::uint8_t target) {
        return IRMath::abs(static_cast<int>(channel) - static_cast<int>(target)) <=
               kFogPaintProbeTolerance;
    };
    return near(color.red_, kFogDebugUnexploredColor.red_) &&
           near(color.green_, kFogDebugUnexploredColor.green_) &&
           near(color.blue_, kFogDebugUnexploredColor.blue_);
}

// Runs at the render front, so it reads the previous frame's completed colour
// (post-FOG_TO_TRIXEL) and entity-id planes.
void probeCeilingPillarPaint() {
    if (++g_fogPaintProbeFrame != g_autoWarmupFrames) {
        return;
    }
    const auto &textures =
        IREntity::getComponent<C_TriangleCanvasTextures>(IRRender::getActiveCanvasEntity());
    std::vector<IRMath::uvec2> carriers;
    std::vector<Color> colors;
    textures.readEntityIdCarriers(carriers);
    textures.readColors(colors);

    const auto expected = static_cast<std::uint32_t>(g_ceilingPillar);
    int texels = 0;
    int painted = 0;
    for (std::size_t i = 0; i < carriers.size(); ++i) {
        if (carriers[i].x != expected) {
            continue;
        }
        ++texels;
        if (matchesFogDebugColor(colors[i])) {
            ++painted;
        }
    }
    IR_LOG_INFO("FOG-PAINT-PROBE pillar={} texels={} painted={}", g_ceilingPillar, texels, painted);
}

void probePerAxisPaint() {
    if (g_fogPerAxisProbeDone ||
        IRMath::abs(IRPrefab::Camera::getYaw() - kEdgeZCostCeilingPaintYaw) > 0.001f) {
        return;
    }
    auto perAxis =
        IREntity::getComponentOptional<C_PerAxisTrixelCanvases>(IRRender::getCanvas("main"));
    if (!perAxis.has_value() || !perAxis.value()->isAllocated()) {
        return;
    }

    g_fogPerAxisProbeDone = true;
    const auto expected = static_cast<std::uint32_t>(g_ceilingPillar);
    int pillarCells = 0;
    int pillarPainted = 0;
    int painted = 0;
    const C_PerAxisTrixelCanvases &axes = *perAxis.value();
    const std::size_t cellCount =
        static_cast<std::size_t>(axes.size_.x) * static_cast<std::size_t>(axes.size_.y);
    for (const auto &axis : axes.axes_) {
        std::vector<IRMath::uvec2> carriers(cellCount);
        std::vector<Color> colors(cellCount);
        axis.entityIds_.second->getSubImage2D(
            0,
            0,
            axes.size_.x,
            axes.size_.y,
            PixelDataFormat::RG_INTEGER,
            PixelDataType::UINT32,
            carriers.data()
        );
        axis.colors_.second->getSubImage2D(
            0,
            0,
            axes.size_.x,
            axes.size_.y,
            PixelDataFormat::RGBA,
            PixelDataType::UNSIGNED_BYTE,
            colors.data()
        );
        for (std::size_t i = 0; i < cellCount; ++i) {
            const bool isPainted = matchesFogDebugColor(colors[i]);
            if (isPainted) {
                ++painted;
            }
            if (carriers[i].x != expected) {
                continue;
            }
            ++pillarCells;
            if (isPainted) {
                ++pillarPainted;
            }
        }
    }
    IR_LOG_INFO(
        "FOG-PERAXIS-PROBE pillar={} pillarCells={} pillarPainted={} painted={}",
        g_ceilingPillar,
        pillarCells,
        pillarPainted,
        painted
    );
}

// --entity-reveal: whole-body fog reveal under the --edge-zcost-ceiling hard
// ceiling. One screen row (x + y = 0) of equal-height bodies rising
// past the ceiling, each pair side by side so its crops compare like for like:
// an untagged and a governed voxel pillar, a flagged and an unflagged SDF box,
// and a governed pillar whose anchor is inside the disc while its outer
// columns cross the XY rim. A governed pillar outside every circle stays
// hidden. Row offsets are along (1, -1); kEntityRevealSpacing leaves a
// 2-unit gap between the 4-wide footprints. Slot 0 lands screen-right, and
// each crop frames one whole body (base to top) at the 2560x1440 zoom-6 shot.
bool g_entityReveal = false; // --entity-reveal
constexpr float kEntityRevealRadius = 20.0f;
constexpr float kEntityRevealSpacing = 5.0f;
constexpr int kEntityRevealBodyHeight = 16;
constexpr std::uint32_t kEntityRevealShapeFlag = IRRender::SHAPE_FLAG_FOG_WHOLE_BODY_EXEMPT;
IREntity::EntityId g_entityRevealProbe = IREntity::kNullEntity;
int g_entityRevealProbeFrame = 0;
constexpr IRVideo::RoiCrop kCropsEntityReveal[] = {
    {1580, 240, 300, 680, "untagged_pillar"},
    {1260, 240, 300, 680, "governed_pillar"},
    {940, 240, 300, 680, "flagged_shape"},
    {620, 240, 300, 680, "unflagged_shape"},
    {210, 220, 410, 720, "governed_rim_pillar"},
};
constexpr IRVideo::AutoScreenshotShot kEntityRevealShots[] = {
    {6.0f,
     vec2(0, 0),
     0.0f,
     "fog_entity_reveal",
     kCropsEntityReveal,
     sizeof(kCropsEntityReveal) / sizeof(kCropsEntityReveal[0])},
};

// One-shot picking probe for the fog whole-body carrier bit: after warmup,
// read the canvas entity-id channel, count the governed pillar's texels (raw
// low word match) and how many carry the bit and decode back to the bare id,
// then pick its topmost carrying texel — above the ceiling — through
// readEntityIdAt. Runs at the render front, so it reads the previous frame's
// completed ids.
void probeEntityRevealIds() {
    if (++g_entityRevealProbeFrame != g_autoWarmupFrames) {
        return;
    }
    const auto &textures =
        IREntity::getComponent<C_TriangleCanvasTextures>(IRRender::getActiveCanvasEntity());
    std::vector<IRMath::uvec2> carriers;
    textures.readEntityIdCarriers(carriers);

    const auto expected = static_cast<std::uint32_t>(g_entityRevealProbe);
    int texels = 0;
    int flagged = 0;
    int mismatched = 0;
    IRMath::ivec2 top{-1, -1};
    for (int y = 0; y < textures.size_.y; ++y) {
        for (int x = 0; x < textures.size_.x; ++x) {
            const IRMath::uvec2 raw = carriers[static_cast<std::size_t>(y) * textures.size_.x + x];
            if (raw.x != expected) {
                continue;
            }
            ++texels;
            if (IRRender::decodeCarrierEntityId(raw) != g_entityRevealProbe) {
                ++mismatched;
            }
            if ((raw.y & IRRender::kEntityIdFogWholeBodyMaskInHighWord) == 0u) {
                continue;
            }
            ++flagged;
            if (top.y < 0) {
                top = IRMath::ivec2(x, y);
            }
        }
    }
    const IREntity::EntityId read =
        top.y < 0 ? IREntity::kNullEntity : textures.readEntityIdAt(top);
    IR_LOG_INFO(
        "FOG-ID-PROBE expected={} read={} texel=({}, {}) texels={} flagged={} mismatched={}",
        g_entityRevealProbe,
        read,
        top.x,
        top.y,
        texels,
        flagged,
        mismatched
    );
}

// --occlusion=<scene>: line-of-sight fog. Static, marker-free scenes on the
// shared ground slab (top voxel centre T = 4) with every vision circle gated
// at eye height kOcclusionEyeHeight above kOcclusionGroundZ (E.z = 3, one unit
// above the slab top). The ridge is a voxel wall on the slab — cells x 0..1,
// y -7..8, z 0..3 (top T = 0, four voxels above the ground) — the only
// occluder in the ground scenes; the grid stays unexplored so only the discs
// reveal:
//   ground           observer on the ground at -X: the near ground reveals,
//                    the ground behind the ridge (+X) stays black
//   high-ground      the observer stands on the ridge top: the far ground
//                    reveals (the strip in the ridge's own shadow stays dark)
//   blocker          the ridge is an SDF box with C_LightBlocker{blocksLOS_}
//   blocker-inert    the same box with blocksLOS_ = false: nothing occludes
//   two-sources      a long wall between two gated sources offset in y: each
//                    reveals only its own side
//   flat             the slab alone: the gated disc reveals exactly as an
//                    ungated one would
//   ground-los-off   the ground scene with LOS off (the img_diff control)
enum class OcclusionScene {
    NONE,
    GROUND,
    HIGH_GROUND,
    BLOCKER,
    BLOCKER_INERT,
    TWO_SOURCES,
    FLAT,
    GROUND_LOS_OFF,
};
OcclusionScene g_occlusion = OcclusionScene::NONE;
// --los-softness: every gated --occlusion source takes the smooth gate with this
// band; absent keeps the hard gate.
float g_occlusionLosSoftness = kFogLosHardGate;
constexpr float kOcclusionRadius = 12.0f;
constexpr float kOcclusionGroundZ = 4.5f;
constexpr float kOcclusionEyeHeight = 1.5f;
constexpr vec2 kOcclusionGroundObserver{-6.0f, 0.0f};
// Standing on the ridge top voxel (centre z 0), mirroring the ground
// observer's half-cell offset from the slab top.
constexpr vec3 kOcclusionRidgeObserver{0.0f, 0.0f, 0.5f};
constexpr vec3 kOcclusionRidgeCenter{0.0f, 0.0f, 1.0f};
constexpr IRMath::ivec3 kOcclusionRidgeSize{2, 16, 4};
constexpr vec3 kOcclusionBlockerCenter{0.5f, 0.5f, 1.5f};
constexpr vec4 kOcclusionBlockerSize{2.0f, 16.0f, 4.0f, 0.0f};
constexpr IRMath::ivec3 kOcclusionLongWallSize{2, 30, 4};
constexpr float kOcclusionTwoSourcesRadius = 10.0f;
constexpr vec2 kOcclusionSourceA{-6.0f, -6.0f};
constexpr vec2 kOcclusionSourceB{7.0f, 6.0f};
constexpr Color kOcclusionRidgeColor{200, 170, 120, 255};
constexpr Color kOcclusionBlockerColor{200, 120, 220, 255};

constexpr IRVideo::AutoScreenshotShot kOcclusionGroundShots[] = {
    {6.0f, vec2(0, 0), 0.0f, "fog_occlusion_ground"},
};
constexpr IRVideo::AutoScreenshotShot kOcclusionHighGroundShots[] = {
    {6.0f, vec2(0, 0), 0.0f, "fog_occlusion_high_ground"},
};
constexpr IRVideo::AutoScreenshotShot kOcclusionBlockerShots[] = {
    {6.0f, vec2(0, 0), 0.0f, "fog_occlusion_blocker"},
};
constexpr IRVideo::AutoScreenshotShot kOcclusionBlockerInertShots[] = {
    {6.0f, vec2(0, 0), 0.0f, "fog_occlusion_blocker_inert"},
};
constexpr IRVideo::AutoScreenshotShot kOcclusionTwoSourcesShots[] = {
    {5.0f, vec2(0, 0), 0.0f, "fog_occlusion_two_sources"},
};
constexpr IRVideo::AutoScreenshotShot kOcclusionFlatShots[] = {
    {6.0f, vec2(0, 0), 0.0f, "fog_occlusion_flat"},
};
constexpr IRVideo::AutoScreenshotShot kOcclusionGroundLosOffShots[] = {
    {6.0f, vec2(0, 0), 0.0f, "fog_occlusion_ground_los_off"},
};
// The same poses under --los-softness, named apart from the hard-gate rows.
constexpr IRVideo::AutoScreenshotShot kOcclusionGroundSmoothShots[] = {
    {6.0f, vec2(0, 0), 0.0f, "fog_occlusion_smooth"},
};
constexpr IRVideo::AutoScreenshotShot kOcclusionHighGroundSmoothShots[] = {
    {6.0f, vec2(0, 0), 0.0f, "fog_occlusion_high_ground_smooth"},
};
constexpr IRVideo::AutoScreenshotShot kOcclusionBlockerSmoothShots[] = {
    {6.0f, vec2(0, 0), 0.0f, "fog_occlusion_blocker_smooth"},
};

// One-shot point-query probe for the --occlusion scenes: after warmup, ask
// `IRPrefab::Fog::lineOfSight` — which rebuilds its own column view and needs
// no registered vision circle — whether the ground observer's eye sees the
// ground in front of the ridge and behind it, and log both verdicts.
int g_occlusionProbeFrame = 0;
constexpr IRMath::ivec3 kOcclusionNearProbe{-3, 0, 4};
constexpr IRMath::ivec3 kOcclusionFarProbe{6, 0, 4};

void probeOcclusionLineOfSight() {
    if (++g_occlusionProbeFrame != g_autoWarmupFrames) {
        return;
    }
    const vec3 eye(kOcclusionGroundObserver, kOcclusionGroundZ - kOcclusionEyeHeight);
    IR_LOG_INFO(
        "FOG-LOS-PROBE near={} far={}",
        IRPrefab::Fog::lineOfSight(eye, vec3(kOcclusionNearProbe)) ? 1 : 0,
        IRPrefab::Fog::lineOfSight(eye, vec3(kOcclusionFarProbe)) ? 1 : 0
    );
}

OcclusionScene parseOcclusionScene(const std::string &name) {
    if (name == "ground")
        return OcclusionScene::GROUND;
    if (name == "high-ground")
        return OcclusionScene::HIGH_GROUND;
    if (name == "blocker")
        return OcclusionScene::BLOCKER;
    if (name == "blocker-inert")
        return OcclusionScene::BLOCKER_INERT;
    if (name == "two-sources")
        return OcclusionScene::TWO_SOURCES;
    if (name == "flat")
        return OcclusionScene::FLAT;
    if (name == "ground-los-off")
        return OcclusionScene::GROUND_LOS_OFF;
    return OcclusionScene::NONE;
}

// --edge-yaw-sweep: the edge-zoom cross-section under CONTINUOUS
// camera yaw. Reuses the static --edge-zoom scene (same boundary voxel objects +
// origin vision circle) but steps the camera Z-yaw in fine increments inside one
// cardinal quadrant (residual yaw 0.05..0.70 rad < π/4, constant visible-face
// triplet) so the per-axis rotation route (perAxisRoute 1/2/3) drives the raster.
// The headline check: a boundary-cut object must keep its FILLED interior
// cross-section through the whole sweep — no holes opening/closing, no flicker —
// the same wall the cardinal --edge-zoom shots show at yaw 0. Built dynamically
// (one shot per step) because the auto-screenshot harness applies shot.yawRadians_
// via Camera::setYaw per shot. jitter_probe the sequence to score temporal
// stability. Implies --edge-zoom (it owns the scene).
// --auto-profile [N]: N render frames (default 300) with per-system frame
// timing and GPU stage timing on, then exit; the World writes
// save_files/profile_report.txt on shutdown.
int g_autoProfileFrames = 0;
int g_autoProfileCount = 0;

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
        "--edge-sdf-blocker",
        "Like --edge-zoom but adds an SDF box with C_LightBlocker{blocksLOS_} "
        "straddling the -Y disc arc — its fog-hidden half must cap with the cut "
        "wall, not a black band (#2247 SDF-blocker cut parity)"
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
    IREngine::args().flag(
        "--edge-zcost",
        "XY-radius vision disc with a Z-cost HEIGHT penalty (#2260): a central "
        "tall pillar's base reveals while its top fades past the disc, a low cube "
        "at the same XY stays revealed; skips the static grid reveal"
    );
    IREngine::args().flag(
        "--edge-zcost-asym",
        "Like --edge-zcost but with ASYMMETRIC up/down height costs (#2557): an "
        "up pillar and a mirrored-XY down block at equal |Δz| fade at different "
        "rates; skips the static grid reveal"
    );
    IREngine::args().flag(
        "--edge-zcost-ceiling",
        "Like --edge-zcost but with a penalty-free height BAND + near-radius "
        "up-cost (#2557): matter within the band reveals fully, then cuts off "
        "within ~1 unit past it — a hard ceiling; skips the static grid reveal"
    );
    IREngine::args().optionalInt(
        "--auto-profile",
        "Run N frames (default 300) with frame + GPU stage timing, then exit",
        300
    );
    IREngine::args().flag(
        "--fog-debug-color",
        "Paint fully unexplored matter magenta instead of black; with "
        "--edge-zcost-ceiling, also log FOG-PAINT-PROBE (the central pillar's "
        "painted texel count)"
    );
    IREngine::args().flag(
        "--peraxis-overflow",
        "Add the fog-hidden keep-ring overflow fixture and capture its rotated paint shot"
    );
    IREngine::args().flag(
        "--entity-reveal",
        "Whole-body fog reveal under the --edge-zcost-ceiling hard ceiling: governed "
        "voxel pillars and a flagged SDF box render whole beside clipped untagged twins"
    );
    IREngine::args().enumValue(
        "--occlusion",
        "Line-of-sight fog scene "
        "(none|ground|high-ground|blocker|blocker-inert|two-sources|flat|ground-los-off); "
        "overrides every other reveal mode",
        {"none",
         "ground",
         "high-ground",
         "blocker",
         "blocker-inert",
         "two-sources",
         "flat",
         "ground-los-off"},
        "none"
    );
    IREngine::args().number(
        "--los-softness",
        "With --occlusion: gate every source with the smooth line-of-sight gate, "
        "this band wide in voxels (absent = the hard gate)",
        kFogLosHardGate
    );
    IREngine::args().flag(
        "--lua-fog-selftest",
        "Drive the engine-owned IRFog binding and verify its observer UBO upload"
    );
    IREngine::registerLuaBindings([](IRScript::LuaScript &script) {
        script.bindLuaFog();
        script.lua()["fogSelftestEntity"] = []() {
            return static_cast<double>(g_luaFogProbeEntity);
        };
        script.lua()["fogCapSelftestDone"] = []() { g_luaFogCapSelftestDone = true; };
        script.lua()["fogSetupSelftestDone"] = []() { g_luaFogSetupSelftestDone = true; };
    });
    IREngine::init(argc, argv);
    g_autoWarmupFrames = IREngine::args().autoScreenshotWarmupFrames();
    g_movingObserver = IREngine::args().getFlag("--moving-observer");
    g_playerWalk = IREngine::args().getFlag("--player-walk");
    g_edgeZoom = IREngine::args().getFlag("--edge-zoom");
    g_edgeSdfBlocker = IREngine::args().getFlag("--edge-sdf-blocker");
    g_detachedEdge = IREngine::args().getFlag("--detached-edge");
    g_edgeSmooth = IREngine::args().getFlag("--edge-smooth");
    g_edgeYawSweep = IREngine::args().getFlag("--edge-yaw-sweep");
    g_edgeZCost = IREngine::args().getFlag("--edge-zcost");
    g_edgeZCostAsym = IREngine::args().getFlag("--edge-zcost-asym");
    g_edgeZCostCeiling = IREngine::args().getFlag("--edge-zcost-ceiling");
    g_entityReveal = IREngine::args().getFlag("--entity-reveal");
    g_fogDebugColor = IREngine::args().getFlag("--fog-debug-color");
    g_perAxisOverflow = IREngine::args().getFlag("--peraxis-overflow");
    if (g_perAxisOverflow) {
        g_edgeZCostCeiling = true;
        g_fogDebugColor = true;
    }
    if (IREngine::args().wasProvided("--auto-profile")) {
        g_autoProfileFrames = IREngine::args().getInt("--auto-profile");
    }
    g_luaFogSelftest = IREngine::args().getFlag("--lua-fog-selftest");
    g_occlusion = parseOcclusionScene(IREngine::args().getEnum("--occlusion"));
    g_occlusionLosSoftness = IREngine::args().getFloat("--los-softness");
    if (g_luaFogSelftest) {
        g_occlusion = OcclusionScene::NONE;
    }
    if (g_luaFogSelftest || g_occlusion != OcclusionScene::NONE) {
        g_entityReveal = false;
        g_perAxisOverflow = false;
    }
    if (g_luaFogSelftest || g_entityReveal || g_occlusion != OcclusionScene::NONE) {
        g_movingObserver = false;
        g_playerWalk = false;
        g_edgeZoom = false;
        g_edgeSdfBlocker = false;
        g_detachedEdge = false;
        g_edgeSmooth = false;
        g_edgeYawSweep = false;
        g_edgeZCost = false;
        g_edgeZCostAsym = false;
        g_edgeZCostCeiling = false;
    }
    // --edge-yaw-sweep owns the same scene as --edge-zoom (boundary objects +
    // origin vision circle); it only swaps the static climbing-zoom shots for a
    // yaw sweep, so turn the edge scene on.
    if (g_edgeYawSweep) {
        g_edgeZoom = true;
    }
    // The reveal modes are mutually exclusive; precedence: --edge-zcost-asym, then
    // --edge-zcost-ceiling, then --edge-zcost, then --detached-edge, then
    // --edge-sdf-blocker, then --edge-zoom / --edge-yaw-sweep, then --edge-smooth,
    // then --player-walk, then --moving-observer (each owns its own scene + shots).
    if (g_edgeZCostAsym) {
        if (g_edgeZCostCeiling || g_edgeZCost || g_detachedEdge || g_edgeSdfBlocker || g_edgeZoom ||
            g_edgeYawSweep || g_edgeSmooth || g_playerWalk || g_movingObserver) {
            IR_LOG_INFO(
                "--edge-zcost-asym overrides --edge-zcost-ceiling / --edge-zcost / "
                "--detached-edge / --edge-sdf-blocker / --edge-zoom / --edge-yaw-sweep / "
                "--edge-smooth / --player-walk / --moving-observer"
            );
        }
        g_edgeZCostCeiling = false;
        g_edgeZCost = false;
        g_detachedEdge = false;
        g_edgeSdfBlocker = false;
        g_edgeZoom = false;
        g_edgeYawSweep = false;
        g_edgeSmooth = false;
        g_playerWalk = false;
        g_movingObserver = false;
    } else if (g_edgeZCostCeiling) {
        if (g_edgeZCost || g_detachedEdge || g_edgeSdfBlocker || g_edgeZoom || g_edgeYawSweep ||
            g_edgeSmooth || g_playerWalk || g_movingObserver) {
            IR_LOG_INFO(
                "--edge-zcost-ceiling overrides --edge-zcost / --detached-edge / "
                "--edge-sdf-blocker / --edge-zoom / --edge-yaw-sweep / --edge-smooth / "
                "--player-walk / --moving-observer"
            );
        }
        g_edgeZCost = false;
        g_detachedEdge = false;
        g_edgeSdfBlocker = false;
        g_edgeZoom = false;
        g_edgeYawSweep = false;
        g_edgeSmooth = false;
        g_playerWalk = false;
        g_movingObserver = false;
    } else if (g_edgeZCost) {
        if (g_detachedEdge || g_edgeSdfBlocker || g_edgeZoom || g_edgeYawSweep || g_edgeSmooth ||
            g_playerWalk || g_movingObserver) {
            IR_LOG_INFO(
                "--edge-zcost overrides --detached-edge / --edge-sdf-blocker / --edge-zoom / "
                "--edge-yaw-sweep / --edge-smooth / --player-walk / --moving-observer"
            );
        }
        g_detachedEdge = false;
        g_edgeSdfBlocker = false;
        g_edgeZoom = false;
        g_edgeYawSweep = false;
        g_edgeSmooth = false;
        g_playerWalk = false;
        g_movingObserver = false;
    } else if (g_detachedEdge) {
        if (g_edgeSdfBlocker || g_edgeZoom || g_edgeYawSweep || g_edgeSmooth || g_playerWalk ||
            g_movingObserver) {
            IR_LOG_INFO(
                "--detached-edge overrides --edge-sdf-blocker / --edge-zoom / --edge-yaw-sweep / "
                "--edge-smooth / --player-walk / --moving-observer"
            );
        }
        g_edgeSdfBlocker = false;
        g_edgeZoom = false;
        g_edgeYawSweep = false;
        g_edgeSmooth = false;
        g_playerWalk = false;
        g_movingObserver = false;
    } else if (g_edgeSdfBlocker) {
        if (g_edgeZoom || g_edgeYawSweep || g_edgeSmooth || g_playerWalk || g_movingObserver) {
            IR_LOG_INFO(
                "--edge-sdf-blocker overrides --edge-zoom / --edge-yaw-sweep / --edge-smooth / "
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
    if (g_autoProfileFrames > 0) {
        IREngine::enableFrameTiming(true);
        IRRender::gpuStageTiming().enabled_ = true;
    }
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
        IRSystem::createSystem<IRSystem::FOG_REVEAL_EVAL>(),
        IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
        IRSystem::createSystem<IRSystem::REBUILD_GRID_VOXELS>(),
        IRSystem::createSystem<IRSystem::REBUILD_GRID_VOXELS_IMPLICIT>(),
    };
    // --detached-edge adds the world-placed DETACHED_REVOXELIZE path:
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
            IRSystem::createSystem<IRSystem::FOG_LOS_BUILD>(),
            IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
            IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::COMPUTE_VOXEL_AO>(),
            IRSystem::createSystem<IRSystem::BAKE_SUN_SHADOW_MAP>(),
            IRSystem::createSystem<IRSystem::COMPUTE_SUN_SHADOW>(),
            IRSystem::createSystem<IRSystem::COMPUTE_LIGHT_VOLUME>(),
            IRSystem::createSystem<IRSystem::LIGHTING_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::FOG_TO_TRIXEL>(),
        }
    );
    if (g_luaFogSelftest) {
        renderPipeline.push_back(
            IRSystem::createSystem<C_Name>(
                "FogLuaBindingProbe",
                [](C_Name &) {},
                []() { probeLuaFogUpload(); }
            )
        );
    }
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>());
    // --detached-edge composites the world-placed detached canvas (with its
    // cross-sectioned voxels from STAGE_1/2) onto the main framebuffer between
    // TRIXEL_TO_FRAMEBUFFER and FRAMEBUFFER_TO_SCREEN. Added only for that
    // scene so the other reveal modes keep their committed refs byte-identical.
    if (g_detachedEdge) {
        renderPipeline.push_back(IRSystem::createSystem<IRSystem::ENTITY_CANVAS_TO_FRAMEBUFFER>());
    }
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>());

    if (g_autoProfileFrames > 0) {
        IRSystem::SystemId autoProfileId = IRSystem::createSystem<C_Name>(
            "FogAutoProfile",
            [](C_Name &) {},
            []() {
                if (++g_autoProfileCount >= g_autoProfileFrames) {
                    IR_LOG_INFO("Auto-profile: {} frames collected, exiting", g_autoProfileFrames);
                    IRWindow::closeWindow();
                }
            }
        );
        renderPipeline.push_back(autoProfileId);
    }

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

    if (g_occlusion != OcclusionScene::NONE && g_autoWarmupFrames > 0) {
        renderPipeline.push_front(
            IRSystem::createSystem<C_Name>(
                "FogOcclusionLineOfSightProbe",
                [](C_Name &) {},
                []() { probeOcclusionLineOfSight(); }
            )
        );
    }

    if (g_entityReveal && g_autoWarmupFrames > 0) {
        IRSystem::SystemId probeTickId = IRSystem::createSystem<C_Name>(
            "FogEntityRevealIdProbe",
            [](C_Name &) {},
            []() { probeEntityRevealIds(); }
        );
        renderPipeline.push_front(probeTickId);
    }

    if (g_edgeZCostCeiling && g_fogDebugColor && g_autoWarmupFrames > 0) {
        IRSystem::SystemId probeTickId = IRSystem::createSystem<C_Name>(
            "FogPaintProbe",
            [](C_Name &) {},
            []() { probeCeilingPillarPaint(); }
        );
        renderPipeline.push_front(probeTickId);
        IRSystem::SystemId perAxisProbeTickId = IRSystem::createSystem<C_Name>(
            "FogPerAxisPaintProbe",
            [](C_Name &) {},
            []() { probePerAxisPaint(); }
        );
        renderPipeline.push_front(perAxisProbeTickId);
    }

    if (g_autoWarmupFrames > 0) {
        IRVideo::AutoScreenshotConfig cfg{};
        cfg.warmupFrames_ = g_autoWarmupFrames;
        cfg.settleFrames_ = 3;
        // --edge-zcost-asym / --edge-zcost-ceiling capture the asymmetric /
        // hard-ceiling height-penalty readouts; --detached-edge zooms on a
        // detached-canvas cross-section; --edge-yaw-sweep sweeps the GRID
        // cross-section through the per-axis rotation route; --edge-zoom /
        // --edge-smooth zoom on the GRID cross-section clip edge (hard vs smooth
        // disc); --player-walk captures the walking reveal sequence; the
        // default captures the three static fog-boundary shots.
        const bool smoothOcclusion = g_occlusionLosSoftness >= 0.0f;
        if (smoothOcclusion &&
            (g_occlusion == OcclusionScene::GROUND || g_occlusion == OcclusionScene::HIGH_GROUND ||
             g_occlusion == OcclusionScene::BLOCKER)) {
            if (g_occlusion == OcclusionScene::GROUND) {
                IRVideo::setAutoScreenshotShots(cfg, kOcclusionGroundSmoothShots);
            } else if (g_occlusion == OcclusionScene::HIGH_GROUND) {
                IRVideo::setAutoScreenshotShots(cfg, kOcclusionHighGroundSmoothShots);
            } else {
                IRVideo::setAutoScreenshotShots(cfg, kOcclusionBlockerSmoothShots);
            }
        } else if (g_occlusion != OcclusionScene::NONE) {
            switch (g_occlusion) {
            case OcclusionScene::GROUND:
                IRVideo::setAutoScreenshotShots(cfg, kOcclusionGroundShots);
                break;
            case OcclusionScene::HIGH_GROUND:
                IRVideo::setAutoScreenshotShots(cfg, kOcclusionHighGroundShots);
                break;
            case OcclusionScene::BLOCKER:
                IRVideo::setAutoScreenshotShots(cfg, kOcclusionBlockerShots);
                break;
            case OcclusionScene::BLOCKER_INERT:
                IRVideo::setAutoScreenshotShots(cfg, kOcclusionBlockerInertShots);
                break;
            case OcclusionScene::TWO_SOURCES:
                IRVideo::setAutoScreenshotShots(cfg, kOcclusionTwoSourcesShots);
                break;
            case OcclusionScene::FLAT:
                IRVideo::setAutoScreenshotShots(cfg, kOcclusionFlatShots);
                break;
            case OcclusionScene::GROUND_LOS_OFF:
                IRVideo::setAutoScreenshotShots(cfg, kOcclusionGroundLosOffShots);
                break;
            case OcclusionScene::NONE:
                break;
            }
        } else if (g_perAxisOverflow) {
            IRVideo::setAutoScreenshotShots(cfg, kPerAxisOverflowShots);
        } else if (g_entityReveal) {
            IRVideo::setAutoScreenshotShots(cfg, kEntityRevealShots);
        } else if (g_edgeZCostAsym) {
            IRVideo::setAutoScreenshotShots(cfg, kEdgeZCostAsymShots);
        } else if (g_edgeZCostCeiling && g_fogDebugColor) {
            IRVideo::setAutoScreenshotShots(cfg, kEdgeZCostCeilingPaintShots);
        } else if (g_edgeZCostCeiling) {
            IRVideo::setAutoScreenshotShots(cfg, kEdgeZCostCeilingShots);
        } else if (g_edgeZCost) {
            IRVideo::setAutoScreenshotShots(cfg, kEdgeZCostShots);
        } else if (g_detachedEdge) {
            IRVideo::setAutoScreenshotShots(cfg, kDetachedEdgeShots);
        } else if (g_edgeYawSweep) {
            // Fixed zoom + origin, step yaw across [0.05, 0.70] rad — one cardinal
            // quadrant (< π/4), constant visible-face triplet — so the cut-face
            // cross-section is exercised purely through the per-axis rotation route.
            // Mirror of shape_debug's --yaw-sweep shot construction.
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
        } else if (g_edgeSdfBlocker) {
            IRVideo::setAutoScreenshotShots(cfg, kEdgeSdfBlockerShots);
        } else if (g_edgeZoom) {
            IRVideo::setAutoScreenshotShots(cfg, kEdgeShots);
        } else if (g_edgeSmooth) {
            IRVideo::setAutoScreenshotShots(cfg, kEdgeSmoothShots);
        } else if (g_playerWalk) {
            IRVideo::setAutoScreenshotShots(cfg, kWalkShots);
        } else {
            IRVideo::setAutoScreenshotShots(cfg, kShots);
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

// A gated vision circle for the --occlusion scenes: the plain disc (no height
// cost) so line of sight is the only term that differs between scenes.
void addOcclusionSource(vec2 center, float observerZ, float radius, bool lineOfSight) {
    const int slot = IRPrefab::Fog::addVisionCircle(
        center.x,
        center.y,
        radius,
        kFogVisionEdgeDefault,
        observerZ
    );
    IR_ASSERT(slot >= 0, "occlusion scene vision circle was rejected");
    if (lineOfSight) {
        IRPrefab::Fog::setVisionCircleLineOfSight(
            slot,
            kOcclusionEyeHeight,
            g_occlusionLosSoftness
        );
    }
}

void createOcclusionWall(IRMath::ivec3 size) {
    IREntity::createEntity(
        C_LocalTransform{kOcclusionRidgeCenter},
        C_VoxelSetNew{size, kOcclusionRidgeColor, true}
    );
}

void initOcclusionScene() {
    createEdgeGroundSlab();
    IRPrefab::Fog::clearVisionCircles();
    switch (g_occlusion) {
    case OcclusionScene::GROUND:
    case OcclusionScene::GROUND_LOS_OFF:
        createOcclusionWall(kOcclusionRidgeSize);
        addOcclusionSource(
            kOcclusionGroundObserver,
            kOcclusionGroundZ,
            kOcclusionRadius,
            g_occlusion == OcclusionScene::GROUND
        );
        break;
    case OcclusionScene::HIGH_GROUND:
        createOcclusionWall(kOcclusionRidgeSize);
        addOcclusionSource(
            vec2(kOcclusionRidgeObserver),
            kOcclusionRidgeObserver.z,
            kOcclusionRadius,
            true
        );
        break;
    case OcclusionScene::BLOCKER:
    case OcclusionScene::BLOCKER_INERT: {
        const IREntity::EntityId blocker = IREntity::createEntity(
            C_LocalTransform{kOcclusionBlockerCenter},
            C_ShapeDescriptor{
                IRRender::ShapeType::BOX,
                kOcclusionBlockerSize,
                kOcclusionBlockerColor
            }
        );
        IREntity::setComponent(
            blocker,
            C_LightBlocker{g_occlusion == OcclusionScene::BLOCKER, false, 1.0f}
        );
        addOcclusionSource(kOcclusionGroundObserver, kOcclusionGroundZ, kOcclusionRadius, true);
        break;
    }
    case OcclusionScene::TWO_SOURCES:
        createOcclusionWall(kOcclusionLongWallSize);
        addOcclusionSource(kOcclusionSourceA, kOcclusionGroundZ, kOcclusionTwoSourcesRadius, true);
        addOcclusionSource(kOcclusionSourceB, kOcclusionGroundZ, kOcclusionTwoSourcesRadius, true);
        break;
    case OcclusionScene::FLAT:
        addOcclusionSource(vec2(0.0f), kOcclusionGroundZ, kOcclusionRadius, true);
        break;
    case OcclusionScene::NONE:
        break;
    }
}

void initEntities() {
    // A wide thin floor so the fog mask has a continuous surface that fades
    // visible → explored → unexplored across the screen. Centered on origin.
    // The cross-section scenes (--edge-zoom / --edge-smooth / --edge-sdf-blocker
    // / --detached-edge) skip it and bring their own VOXEL ground slab instead:
    // the analytic fog cut decides solid-vs-air from the light-occlusion
    // VOXEL bitfield, which SDF geometry never enters, so an SDF surface
    // straddling the disc renders the cut band BLACK along whatever arc segments
    // cross it (the two-black-bands artifact) instead of capping with the toned
    // cut colour.
    constexpr float kFloorZ = 5.0f;
    const bool occlusionScene = g_occlusion != OcclusionScene::NONE;
    if (!occlusionScene && !g_entityReveal && !g_edgeZoom && !g_edgeSmooth && !g_edgeSdfBlocker &&
        !g_detachedEdge && !g_edgeZCost && !g_edgeZCostAsym && !g_edgeZCostCeiling) {
        createShape(
            vec3(0.0f, 0.0f, kFloorZ),
            IRRender::ShapeType::BOX,
            vec4(96.0f, 96.0f, 2.0f, 0.0f),
            Color{150, 150, 160, 255}
        );
    }

    // The default + --moving-observer scenes dress the floor with SDF primitives
    // and the column-cull pillar canary. --player-walk / --edge-zoom /
    // --edge-smooth / --detached-edge skip ALL of them: each wants a clean floor so
    // its own content (the gliding disc + marker / the boundary-straddling voxel
    // objects) reads clearly without the tall shapes' iso-projected tops poking
    // through the disc.
    if (!occlusionScene && !g_entityReveal && !g_playerWalk && !g_edgeZoom && !g_edgeSmooth &&
        !g_edgeSdfBlocker && !g_detachedEdge && !g_edgeZCost && !g_edgeZCostAsym &&
        !g_edgeZCostCeiling) {
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

        // Column-cull regression canary: a TALL voxel pillar standing on an
        // UNEXPLORED column (XY = (-22,-22), Euclidean distance ~31 > the reveal+band
        // radius of 26). It is a voxel set (not an SDF shape) so it travels the
        // voxel-pool path — VOXEL_TO_TRIXEL_STAGE_1 → c_voxel_visibility_compact —
        // which is exactly where the cull removes unexplored-column voxels.
        //
        // -X-Y projects DOWN-screen in this iso (the +X+Y cone sits at the top of
        // the disk), so the pillar's base sits below the bright disk and its 44-tall
        // extent projects its top straight up OVER the visible disk. Without the
        // cull the whole pillar would rasterize and FOG_TO_TRIXEL would hard-black
        // every pixel (its column is unexplored), painting a black silhouette
        // across the lit disk. With the cull the pillar's voxels never
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
    IRPrefab::Fog::attachToCanvas(mainCanvas);
    if (g_luaFogSelftest) {
        g_luaFogProbeEntity = IREntity::createEntity(
            C_LocalTransform{vec3(0.0f, 0.0f, 4.0f)},
            C_VoxelSetNew{IRMath::ivec3{1, 1, 1}, Color{255, 255, 255, 255}, true}
        );
        IREngine::getWorld().runScript(
            IREngine::resolveScriptPath("fog_binding_cap_selftest.lua").c_str()
        );
    }

    // High, slightly off-axis sun so each shape casts a visible shadow.
    IRRender::setSunDirection(vec3(0.35f, 0.85f, -0.4f));

    // The BOUNDARY scenes (--edge-zoom / --edge-smooth / --edge-sdf-blocker /
    // --detached-edge) override to a STRAIGHT-DOWN sun. Their render-verify refs
    // exist to inspect the fog reveal boundary, and an angled sun drives shadow
    // TERMINATORS across the exact band under test — the pillar's terminator
    // crossing the slab-top fog arc composites into a kinked dark curve that
    // reads as a fog artifact (a "sharp turn" that appears to connect the
    // elevated face's fade to the floor rim at the wrong height).
    // --edge-sdf-blocker especially: the blocker box's -Y cut
    // face IS the band under test, so an angled sun's terminator across it would
    // masquerade as a cut defect. Fog x shadow composition stays covered by the
    // default grid scene's refs, which keep the angled sun.
    if (occlusionScene || g_entityReveal || g_edgeZoom || g_edgeSmooth || g_edgeSdfBlocker ||
        g_detachedEdge || g_edgeZCost || g_edgeZCostAsym || g_edgeZCostCeiling) {
        IRRender::setSunDirection(vec3(0.0f, 0.0f, -1.0f));
    }
    if (g_fogDebugColor) {
        IRPrefab::Fog::setUnexploredColor(kFogDebugUnexploredColor);
    }

    if (g_luaFogSelftest) {
        return;
    }
    if (occlusionScene) {
        initOcclusionScene();
        return;
    }

    if (g_entityReveal) {
        IRPrefab::Fog::setVisionCircle(
            0.0f,
            0.0f,
            kEntityRevealRadius,
            kFogVisionEdgeDefault,
            kEdgeZCostObserverZ,
            kEdgeZCostCeilingUpCost,
            IRComponents::kFogVisionZCostMirrorUp,
            kEdgeZCostCeilingFreeBand
        );
        createEdgeGroundSlab();

        const auto rowPos = [](int slot, float z) {
            const float offset = -7.0f + kEntityRevealSpacing * static_cast<float>(slot);
            return vec3(offset, -offset, z);
        };
        const auto createPillar = [](vec3 pos, Color color, int footprint = 4) {
            return IREntity::createEntity(
                C_LocalTransform{pos},
                C_VoxelSetNew{
                    IRMath::ivec3{footprint, footprint, kEntityRevealBodyHeight},
                    color,
                    IRComponents::EntityAnchor::GROUND
                }
            );
        };
        // The SDF twins span the pillars' z range: box params are full extents,
        // centred half a body height above the ground surface (+Z is down).
        const auto createBox = [](vec3 pos, Color color, std::uint32_t extraFlags) {
            C_ShapeDescriptor shape{
                IRRender::ShapeType::BOX,
                vec4(4.0f, 4.0f, static_cast<float>(kEntityRevealBodyHeight), 0.0f),
                color
            };
            shape.flags_ |= extraFlags;
            IREntity::createEntity(
                C_LocalTransform{pos - vec3(0.0f, 0.0f, 0.5f * kEntityRevealBodyHeight + 0.5f)},
                shape
            );
        };

        createPillar(rowPos(0, 4.0f), Color{245, 155, 75, 255});
        g_entityRevealProbe = createPillar(rowPos(1, 4.0f), Color{80, 210, 245, 255});
        IRPrefab::Fog::setEntityRevealGoverned(g_entityRevealProbe);
        createBox(rowPos(2, 4.0f), Color{120, 235, 140, 255}, kEntityRevealShapeFlag);
        createBox(rowPos(3, 4.0f), Color{235, 225, 110, 255}, 0u);
        // A 6-wide body with its anchor ~19.1 from the observer and its outer
        // corner ~23.3: the anchor reveals the body while its outer columns sit
        // up to ~3 units past the radius-20 rim.
        IRPrefab::Fog::setEntityRevealGoverned(
            createPillar(rowPos(4, 4.0f) + vec3(0.5f, -0.5f, 0.0f), Color{190, 140, 245, 255}, 6)
        );
        // Governed but outside every circle: its anchor never reveals, so the
        // whole body stays hidden. Voxels appended to a governed set after
        // setEntityRevealGoverned would not inherit the tag.
        IRPrefab::Fog::setEntityRevealGoverned(
            createPillar(vec3(18.0f, 18.0f, 4.0f), Color{235, 80, 170, 255})
        );
        return;
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

    // --edge-zoom (filled cross-section) and --edge-smooth
    // (Mode B): a STATIC analytic vision circle at the origin with VOXEL objects
    // straddling its boundary. Set once here — nothing re-clears it, so it persists
    // across warmup/settle/capture — and leave the grid all-unexplored so ONLY the
    // disc reveals. Validates the cut-face cross-section: the hidden half of each
    // object is dropped (own-column clip) and the revealed half caps with a
    // FILLED interior wall (cut face) wherever a revealed boundary voxel faces
    // a fog-hidden neighbor column — no see-through hole, no black wedge. Cut faces
    // appear only on CAMERA-VISIBLE cut surfaces (cardinal yaw 0 sees -X/-Y/-Z), so
    // the green slab's -X cut shows its wall while the pillars' +X/+Y cuts fall on
    // back faces and read as a clean end.
    if (g_edgeZoom || g_edgeSmooth || g_edgeSdfBlocker) {
        // --edge-zoom / --edge-sdf-blocker are the hard-disc binary cut (Mode A,
        // edgeSoftness 0); --edge-smooth uses a wide soft band (Mode B)
        // over the IDENTICAL geometry so the cut wall follows the analytic disc
        // instead of stair-stepping at the column boundary. The soft band is the
        // only difference among the voxel objects.
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

        // --edge-sdf-blocker: one SDF BOX (C_ShapeDescriptor, NOT a voxel
        // set) carrying C_LightBlocker{blocksLOS_=true}, standing on the floor and
        // straddling the -Y disc arc — an arc the voxel objects above leave clear.
        // It is an SDF, so its matter lives only in the light-occlusion blocker
        // bitfield, never the voxel-existence bitfield. The geometric cut cap
        // is content-independent: the box's rendered surface caps at the
        // disc boundary regardless of any bitfield, so its fog-hidden -Y half fills
        // with the toned cut wall like the -X voxel slab. The C_LightBlocker is
        // retained so this stays the enabled-path proof that a blocksLOS_ SDF is
        // not special-cased out of the cut. castsShadow_=false keeps the test to
        // the cut (no floor shadow competing with the ROI crop). Gated to
        // --edge-sdf-blocker ONLY so the --edge-zoom / --edge-smooth refs stay
        // byte-identical (no new geometry in those scenes).
        if (g_edgeSdfBlocker) {
            const IREntity::EntityId blocker = IREntity::createEntity(
                C_LocalTransform{kSdfBlockerCenter},
                C_ShapeDescriptor{
                    IRRender::ShapeType::BOX,
                    kSdfBlockerHalfExtents,
                    kSdfBlockerColor
                }
            );
            IREntity::setComponent(blocker, C_LightBlocker{true, false, 1.0f});
        }
        return;
    }

    // --detached-edge: the SAME static origin vision circle, but
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
        // World-placed (the engine default) — NOT screen-locked — so
        // PROPAGATE_CANVAS_ROTATION publishes worldCellOffset/worldPlaced and the
        // detached STAGE_1/2 dispatch world-receives the fog. createWithVoxelPool
        // also attaches the AO/lighting-behavior archetype pair by default for a
        // non-screen-locked canvas.
        C_EntityCanvas canvas = IRPrefab::EntityCanvas::createWithVoxelPool(
            "fog_detached_solid",
            kDetachedCanvasSize,
            kDetachedPoolSize
        );
        IREntity::createEntity(
            C_LocalTransform{vec3(0.0f)},
            C_VoxelSetNew{kDetachedSolidSize, Color{130, 230, 150, 255}, true, canvas.canvasEntity_}
        );
        // Identity rotation keeps the re-voxelize raster on its deterministic SOURCE
        // path (a spinning solid round-to-cell speckles); the cut-face code
        // is rotation-agnostic, so this static pose proves the world-column recovery.
        IREntity::createEntity(
            C_LocalTransform{vec3(-9.0f, 0.0f, 2.0f)},
            C_RotationMode{RotationMode::DETACHED_REVOXELIZE},
            canvas
        );
        return;
    }

    // --edge-zcost-asym: the vision disc as an XY radius with ASYMMETRIC
    // up/down height costs (freeBand 0). Set once at the origin with the observer
    // at floor level — nothing re-clears it, so it persists across
    // warmup/settle/capture — and leave the grid all-unexplored so ONLY the disc
    // reveals.
    if (g_edgeZCostAsym) {
        IRPrefab::Fog::setVisionCircle(
            0.0f,
            0.0f,
            kEdgeZCostRadius,
            kFogVisionEdgeDefault,
            kEdgeZCostObserverZ,
            kEdgeZCostAsymUpCost,
            kEdgeZCostAsymDownCost,
            kEdgeZCostAsymFreeBand
        );

        // LOCAL floor patch under the UP side only (X < 0), NOT the shared
        // full-disc createEdgeGroundSlab(): the DOWN counterpart's near-observer
        // cap sits at observer height, the SAME z-range the floor's own thin
        // slab occupies, so a floor sharing its XY footprint would win the
        // depth test and occlude it outright (empirically confirmed — the same
        // reason the up pillar's own base merges invisibly into the floor
        // rather than poking through it). Leaving the down side floor-less
        // makes the down block's own near face the exposed "ground" there.
        IREntity::createEntity(
            C_LocalTransform{vec3(-10.0f, 0.0f, 5.0f)},
            C_VoxelSetNew{IRMath::ivec3{20, 60, 3}, Color{90, 100, 120, 255}, true}
        );

        // UP pillar (identical shape to --edge-zcost's): base near the floor
        // reveals, top at z ≈ -24 (|dzUp| ≈ 28.5) fades fast under
        // kEdgeZCostAsymUpCost.
        IREntity::createEntity(
            C_LocalTransform{vec3(-kEdgeZCostAsymUpXOffset, 0.0f, -10.0f)},
            C_VoxelSetNew{IRMath::ivec3{4, 4, 28}, Color{120, 200, 240, 255}, true}
        );

        // DOWN counterpart, floor-less footprint, short (see the scene comment
        // above for why): spans observer height (z≈5, |dzDown|≈0.5, fully
        // revealed) to z≈9 (|dzDown|≈4.5). Under the much smaller
        // kEdgeZCostAsymDownCost it stays fully revealed across that reach —
        // contrast confirmed against a mirrored (zCostUp-equal) cost, which
        // pushes the far end past the disc radius.
        IREntity::createEntity(
            C_LocalTransform{vec3(kEdgeZCostAsymDownXOffset, 0.0f, 7.0f)},
            C_VoxelSetNew{IRMath::ivec3{4, 4, 4}, Color{130, 230, 150, 255}, true}
        );
        return;
    }

    // --edge-zcost-ceiling: the same disc + pillar geometry as
    // --edge-zcost, but with a penalty-free height BAND and a near-radius
    // zCostUp so the reveal collapses within ~1 unit past the band instead of
    // fading linearly — a hard ceiling. zCostDown is left at the mirror
    // sentinel (unused: the pillar/cube pair only exercises the up side).
    if (g_edgeZCostCeiling) {
        IRPrefab::Fog::setVisionCircle(
            0.0f,
            0.0f,
            kEdgeZCostRadius,
            kFogVisionEdgeDefault,
            kEdgeZCostObserverZ,
            kEdgeZCostCeilingUpCost,
            IRComponents::kFogVisionZCostMirrorUp,
            kEdgeZCostCeilingFreeBand
        );

        // Low floor slab — its z sits at observer height, well inside the
        // penalty-free band, so the whole disc-interior floor reveals.
        createEdgeGroundSlab();

        // Central TALL pillar at the observer's XY, same shape as --edge-zcost's:
        // the section within kEdgeZCostCeilingFreeBand units above the observer
        // (z from ~4.5 down to ~-3.5) reads fully revealed — the band — then cuts
        // off within ~1 unit past it as kEdgeZCostCeilingUpCost drives the
        // effective distance past the disc radius almost immediately.
        g_ceilingPillar = IREntity::createEntity(
            C_LocalTransform{vec3(0.0f, 0.0f, -10.0f)},
            C_VoxelSetNew{IRMath::ivec3{4, 4, 28}, Color{120, 200, 240, 255}, true}
        );

        // A LOW wide cube a few cells off-origin, well inside both the disc and
        // the free band, so it stays fully revealed as a baseline contrast
        // against the pillar's sharp cutoff.
        IREntity::createEntity(
            C_LocalTransform{vec3(6.0f, 0.0f, 2.0f)},
            C_VoxelSetNew{IRMath::ivec3{5, 5, 4}, Color{130, 230, 150, 255}, true}
        );
        if (g_perAxisOverflow) {
            IREntity::createEntity(
                C_LocalTransform{vec3(18.0f, 0.0f, 2.0f)},
                C_VoxelSetNew{IRMath::ivec3{4, 4, 4}, Color{220, 180, 70, 255}, true}
            );
        }
        return;
    }

    // --edge-zcost: the vision disc as an XY radius with a Z-cost height
    // penalty. Set the disc once at the origin with the observer at floor level
    // and a positive zCost — nothing re-clears it, so it persists across
    // warmup/settle/capture — and leave the grid all-unexplored so ONLY the disc
    // (with its height penalty) reveals.
    if (g_edgeZCost) {
        IRPrefab::Fog::setVisionCircle(
            0.0f,
            0.0f,
            kEdgeZCostRadius,
            kFogVisionEdgeDefault,
            kEdgeZCostObserverZ,
            kEdgeZCostFactor
        );

        // Low floor slab — its z sits at observer height (~kEdgeZCostObserverZ),
        // so |z - observerZ| is small and the whole disc-interior floor reveals.
        createEdgeGroundSlab();

        // Central TALL pillar at the observer's XY (distXY 0): iso +Z is downward,
        // so center it well ABOVE the floor to stand a tall column up. Its BASE
        // (z near the floor, ~4) reveals — |z - observerZ| small — while its TOP
        // (z ≈ -24, far above the observer) is penalized past the disc radius:
        // effective distance dist_xy + zCost*|z - observerZ| = 0 + 1*|−24−4.5| ≈
        // 28.5 ≫ radius 14, so it fades then drops (own-column clip), showing the
        // revealed floor behind. The height fade down its length is the headline.
        IREntity::createEntity(
            C_LocalTransform{vec3(0.0f, 0.0f, -10.0f)},
            C_VoxelSetNew{IRMath::ivec3{4, 4, 28}, Color{120, 200, 240, 255}, true}
        );

        // A LOW wide cube a few cells off-origin, still well inside the disc: its
        // z stays near the floor, so it reveals fully at the same XY band where the
        // tall pillar's top fades — the side-by-side "same XY, different height"
        // contrast that reads the penalty as a height effect, not an XY one.
        IREntity::createEntity(
            C_LocalTransform{vec3(6.0f, 0.0f, 2.0f)},
            C_VoxelSetNew{IRMath::ivec3{5, 5, 4}, Color{130, 230, 150, 255}, true}
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

#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_script.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/camera.hpp>

// Components
#include <irreden/common/components/component_auto_spin.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/render/components/component_camera.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/face_occupancy.hpp>

// Systems
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_auto_yaw_rotate.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>
#include <irreden/render/systems/system_build_light_occlusion_grid.hpp>
#include <irreden/render/systems/system_camera_scroll_zoom.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_compute_sun_shadow.hpp>
#include <irreden/render/systems/system_compute_voxel_ao.hpp>
#include <irreden/render/systems/system_entity_canvas_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_lighting_to_trixel.hpp>
#include <irreden/render/systems/system_lod_update.hpp>
#include <irreden/render/systems/system_propagate_canvas_rotation.hpp>
#include <irreden/render/systems/system_resolve_per_axis_screen_depth.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/update/systems/system_auto_spin_local_transform.hpp>
#include <irreden/update/systems/system_lifetime.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/systems/system_rebuild_detached_voxels.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>

// Prefab helpers
#include <irreden/render/camera.hpp>
#include <irreden/render/camera_controls.hpp>
#include <irreden/render/depth_probe.hpp>
#include <irreden/render/entity_canvas.hpp>
#include <irreden/render/trixel_text.hpp>

// Command suites
#include <irreden/common/command_suite_capture.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

// canvas_stress exercises the detached-canvas voxel path: many entities,
// each owning its own per-entity canvas + voxel pool, are composited over a
// main-canvas GRID grid by ENTITY_CANVAS_TO_FRAMEBUFFER. It is the permanent
// visual regression canary for detached canvases and inter-trixel rendering.
// The SO(3) canary cubes ride RotationMode::DETACHED_REVOXELIZE (the sole
// shipped detached SO(3) renderer, #1581/#1589); the orbit ring still spans
// all three rotation modes for a side-by-side of the techniques.
//
// Per #1259 the demo also serves as the rotation-visualization showcase:
// camera yaw auto-rotates by default, detached canvases spin continuously
// at per-entity rates around their assigned axes (full SO(3) bake), a
// small cluster of GRID-mode cubes re-rasterizes via REBUILD_GRID_VOXELS
// each frame, and the standard lighting pipeline (AO + sun + shadows)
// runs so face orientation is visually perceptible.
//
// Rotation-mode contract (#1624, supersedes #1582/#1620): every detached
// canvas — canary cubes, orbit ring, re-voxelize proof solids — is
// WORLD-PLACED by default (C_EntityCanvas::screenLocked_ = false): it
// depth-sorts against the SDF floor and world geometry, and the re-voxelize
// solids receive (P4b-2/#1617) + cast (P4b-3/#1596) world sun-shadow at their
// world pos. Plain-DETACHED orbiters depth-sort only (forward-scatter has no
// faithful world-pos recovery for lighting). Run with --screen-lock-detached
// to opt the whole demo back into fixed-depth overlays at the pre-#1624
// canary layout — byte-identical to the pre-flip default scene, the
// regression canary for the screenLocked_ opt-out path.
// See render/CLAUDE.md "Rotation modes".

using namespace IRComponents;
using namespace IREntity;
using namespace IRMath;

namespace {

struct CanvasStressSettings {
    int mainGridSize_ = 5;
    int detachedCount_ = 5;
    float initialZoom_ = 1.0f;
    float cameraYaw_ = 0.0f;
    bool autoRotate_ = true;
    bool fullRotate_ = false;
    bool noSpin_ = false;
    bool noLighting_ = false;
    // #1619 step-0 isolation harness (architect-mandated). Spawns exactly ONE
    // rotated DETACHED_REVOXELIZE solid — the multi-color L-prism at its
    // off-cardinal initialRotation — and nothing else (no floor, no main grid,
    // no GRID spin cubes, no canary cubes, no second proof solid, no orbit
    // ring). Discriminates cross-canvas clobber (renders alone, breaks with
    // neighbours) from a single-canvas mode-1 chain defect (breaks alone too).
    bool soloRevox_ = false;
    // OFF by default (#1624): every detached canvas in the demo rides the
    // world-placed engine default and depth-sorts against the #1587 SDF floor.
    // Pass --screen-lock-detached (legacy alias --screen-lock-revox) to opt
    // them ALL into C_EntityCanvas::screenLocked_ AND keep the pre-#1624
    // canary z — reproducing the pre-flip overlay scene byte-for-byte, the
    // regression canary for the screen-locked opt-out path.
    bool screenLockDetached_ = false;
    // #1721 diagnosis harness. `--only <group>[,<group>...]` spawns only the
    // named entity groups (maingrid, gridspin, canary, revox, orbit, floor);
    // 0 means no filter (default run, byte-identical). The unified-rotation
    // comparison region (#1374) is the one OPT-IN group: "compare" spawns it
    // (e.g. `--only compare` for the isolated labeled side-by-side) and it never
    // appears in the default run. The sweep flags replace
    // the base auto-screenshot suite with a focused capture run and force
    // --no-auto-rotate so the swept variable is the only one moving:
    // `--sweep-yaw <from> <to> <n>` interpolates camera yaw (radians) across n
    // shots (brackets a rebracket threshold, e.g. 0.6→0.95 across π/4);
    // `--sweep-frames <n> <settle>` captures n identical-camera shots `settle`
    // frames apart so entity self-spin phase is the swept variable (octahedral
    // corner-threshold capture).
    std::uint32_t onlyGroups_ = 0;
    float sweepYawFrom_ = 0.0f;
    float sweepYawTo_ = 0.0f;
    int sweepYawCount_ = 0;
    int sweepFramesCount_ = 0;
    int sweepFramesSettle_ = 0;
    // `--auto-profile` enables per-system frame timing so the World dtor
    // writes save_files/profile_report.txt on the auto-screenshot exit —
    // the per-system CPU sensor for the rotating-GRID / re-voxelize load
    // this demo uniquely exercises (perf_grid has no rotating sets).
    bool autoProfile_ = false;
    // `--debug-overlay <mode>` forces a render debug overlay for the whole run
    // (none|ao|light_level|shadow|peraxis_id|peraxis_origin|unlit). T-1 (#1767)
    // captures with `--debug-overlay shadow` so cast shadows read magenta and
    // render-shadow-metric.py (#1765) can classify the swiss-cheese / dithering
    // signature (epic #1717). The run-global form keeps the shared
    // AutoScreenshotShot table backend-agnostic — a per-shot overlay field is
    // deferred to the structural-gate work (T-2/T-3). NONE leaves the live
    // pipeline untouched, so a flagless run is byte-identical.
    IRRender::DebugOverlayMode debugOverlay_ = IRRender::DebugOverlayMode::NONE;
    // `--depth-probe X,Y` (#1910): each frame, read back and log the real
    // composite depth-test value at main-framebuffer texture pixel (X,Y) —
    // top-left origin, in framebuffer-texture space (the size logged at canvas
    // creation), not window/screenshot pixels. Off by default; when off no probe
    // system is registered, so a flagless run is byte-identical. Used to
    // root-cause the #1884 detached-vs-floor depth crossing in this demo (the
    // floating canary cube clips behind the SDF floor at high zoom).
    bool depthProbeSet_ = false;
    ivec2 depthProbePixel_{0, 0};
    // `--depth-probe-assert X,Y` (#1957): the depth-write regression guard. Like
    // --depth-probe but emits a `[depth-probe-assert] … result=PASS|FAIL` verdict
    // line — PASS iff the composite stored a non-background depth at (X,Y), i.e.
    // the detached-canvas composite WROTE the depth attachment there. Aim it at a
    // texel inside a world-placed detached solid (canonical:
    // `--only canary --no-spin --no-auto-rotate --depth-probe-assert 321,210`,
    // which the canary covers on both backends). FAILs if a future pass disables
    // the composite depth-write. Off by default; registers no system when unset,
    // so a flagless run is byte-identical.
    bool depthProbeAssertSet_ = false;
    ivec2 depthProbeAssertPixel_{0, 0};
    // readConfig() runs AFTER parseArgs() (it needs IREngine::init), so a
    // config `auto_rotate` would otherwise clobber an explicit
    // --auto-rotate / --no-auto-rotate flag. Latch CLI intent so config only
    // supplies the default when the flag is absent (CLI overrides config).
    bool autoRotateSetByCli_ = false;
};

// #1721 spawn groups for --only isolation. Group placement stays derived from
// the CONFIGURED scene (e.g. the GRID spin row's y comes from the configured
// main-grid size even when maingrid is filtered out) so isolated captures stay
// positionally comparable to the default run.
enum SpawnGroup : std::uint32_t {
    kGroupMainGrid = 1u << 0,
    kGroupGridSpin = 1u << 1,
    kGroupCanary = 1u << 2,
    kGroupReVox = 1u << 3,
    kGroupOrbit = 1u << 4,
    kGroupFloor = 1u << 5,
    kGroupCompare = 1u << 6,
};

// 0.5 degrees per frame → full revolution in ~720 frames (~12 s at 60 fps)
constexpr float kYawDeltaPerFrame = IRMath::kPi / 360.0f;

// SO(3) camera-rotate canary. Per-frame rates are chosen so the three axes
// never re-align at the same phase inside a normal capture window — GRID
// canvases stay axis-aligned (only Z reaches them via the yaw helpers)
// while DETACHED canvases tilt continuously, exercising the per-canvas
// SO(3) bake.
constexpr float kFullRotateYawPerFrame = IRMath::kPi / 540.0f; // ~3 rev / 1080 fr
constexpr float kFullRotatePitchPerFrame = IRMath::kPi / 720.0f;
constexpr float kFullRotatePitchYPerFrame =
    IRMath::kPi / 900.0f; // Y-axis; Z=yaw, X=pitch in ISO frame

// Per-entity continuous spin for DETACHED canvases (#1259). 0.4° / frame
// base rate scales by (i + 1) so adjacent canvases visibly de-sync within
// the auto-screenshot capture window. The slowest entity completes a full
// rotation in ~900 frames (~15 s at 60 fps); the fastest is ~5×.
constexpr float kDetachedSpinBaseRadPerFrame = IRMath::kPi / 450.0f;

// GRID-mode spin rate — slower than DETACHED so per-cell re-rasterization
// aliasing (cells aliased when adjacent voxels round to the same world
// cell after rotation) is visible as smooth swap-and-settle rather than
// a strobe. ~0.25° / frame → full revolution in ~1440 frames (~24 s).
constexpr float kGridSpinRadPerFrame = IRMath::kPi / 720.0f;

// Detached RE-VOXELIZE test solids (#1553 P1 / #1555). Distinct from the
// forward-scatter DETACHED cubes above: these rotate by re-filling their
// private pool at the full-rotation CELL positions (the rotation lives in the
// cells, not a 2D deform), so an asymmetric solid reads as a true 3D-rotated
// solid. Canvas + pool are larger than the forward-scatter cubes because a
// re-voxelized solid spans its FULL rotated AABB (≈ base extent × √3), not the
// static footprint.
// Canvas sized like the forward-scatter cubes (so the composite shows them at a
// comparable scale, not shrunk) but with headroom for the rotated AABB; the pool
// 3D bounds span that AABB (base extent × √3). A denser base box keeps the
// round-to-cell surface readable (P1 tolerates aliasing; P3 refines it).
constexpr ivec2 kReVoxCanvasSize{140, 140};
constexpr ivec3 kReVoxPoolSize{22, 22, 22};
constexpr ivec3 kReVoxSolidSize{12, 12, 12}; // base box; the L is carved from it
// Detached canvases rasterize their canvas-local pool against the MAIN camera's
// world cull viewport, so they only render while the camera sits near world
// origin (a pan moves the viewport off the canvas-local pool and culls every
// detached canvas). The framing shots therefore stay at camera (0,0); these
// entities are placed on the screen-center column (separated in Z so iso-Y
// stacks them vertically) where they read large and unobstructed at zoom ~1.
constexpr vec3 kReVoxAsymWorld{0.0f, 0.0f, 42.0f};
constexpr vec3 kReVoxCubeWorld{0.0f, 0.0f, -42.0f};
// Grounded cast-proof solid (#1576 P4b-3): in line with the GRID spin-cube row
// (y≈28, z=-6 → bottoms at z=0, floor 4 below) and offset +x past its end, so
// its world sun shadow falls onto the open #1587 floor beside the GRID cubes'.
constexpr vec3 kReVoxGroundedWorld{40.0f, 24.0f, -6.0f};
// ~0.5° / frame — full revolution in ~720 frames; slow enough to read as a
// smooth true-3D tumble, not a strobe.
constexpr float kReVoxSpinPerFrame = IRMath::kPi / 360.0f;

// ── Unified rotation-harness comparison region (#1374) ──────────────────────
// One labeled entity per rotation technique in a screen-horizontal row, all
// driven by the shared camera Z-yaw. OPT-IN (`--only compare`) — every manifest
// shot renders the whole world pool, so world content added here would break the
// byte-identical detached-canvas canary (full byte-neutrality rationale in PR
// #1943 / issue #1374). Geometry: world (D,D,0) projects straight down in iso
// (iso.y = -2D); stepping (-s,+s,0) per cell spreads them horizontally (iso.x =
// 2s). Camera stays at (0,0) — detached canvases cull on a pan (#1555).
constexpr float kCompareDepth = 24.0f;  // iso.y = -48; row sits just below center
constexpr float kCompareSpread = 30.0f; // iso.x step = +-60 between cell centers
constexpr vec3 kCompareCenterWorld{kCompareDepth, kCompareDepth, 0.0f};
constexpr vec3 kCompareRowStep{-kCompareSpread, kCompareSpread, 0.0f};
constexpr ivec3 kCompareCubeSize{12, 12, 12};
constexpr float kCompareZoom = 1.4f; // tight framing — isolated, no clutter
// Label offset below each cell along canvas-Y, scaled per-frame by the live
// subdivision factor so it tracks cell size across zoom. Tuned empirically.
constexpr int kCompareLabelBelowCells = 16;
// 0 = GRID-attached spin, 1 = world-Z-yaw GRID (static), 2 = DETACHED SO(3).
// Distinct hues so the three techniques read apart even before the labels.
constexpr Color kCompareColors[3]{
    {235, 130, 90, 255},
    {110, 200, 240, 255},
    {180, 235, 110, 255},
};
constexpr const char *kCompareLabels[3]{"GRID spin", "world Z-yaw", "detached SO(3)"};
// World position of comparison cell `i` (0..2), centered row at iso.y = -2D.
inline vec3 compareCellWorld(int index) {
    return kCompareCenterWorld + static_cast<float>(index - 1) * kCompareRowStep;
}

// Sun / lighting tuned for the canvas_stress layout — flat ground plane
// of GRID cubes plus floating DETACHED canvases. Slightly steeper than
// the standard lighting demo so detached cube tops and main-grid tops
// both get lambert above ambient at neutral camera yaw.
// `setSunDirection` points world→sun and asserts z <= 0 (+Z is down, sun is up).
// Points toward -x/-y so the GRID spin cubes (at +y, north of the main grid)
// throw their shadows toward +x/+y — onto the OPEN floor behind them, away from
// the cluttered main-grid lattice, where they read as clean distinct patches
// (mirroring shape_debug's shadows-onto-empty-floor framing). Grazing (moderate
// |z|) keeps the shadows long enough to be obvious. Lower ambient than the
// standard demo so the cast shadows read as dark patches, not washed out.
constexpr vec3 kSunDirection = vec3(-0.42f, -0.60f, -0.55f); // setSunDirection normalizes
constexpr float kSunIntensity = 1.0f;
constexpr float kSunAmbient = 0.30f;

// Shadow floor: a thin SDF box just below the center GRID spin-cube cluster so
// those casters drop visible sun shadows onto a surface. SDF (free at any size)
// rather than a voxel slab so the plate costs nothing in the world pool.
//
// GRID-only focused shadow stage (#1587, architect decision on the PR). The
// engine sun-shadow mechanism is sound — BAKE_SUN_SHADOW_MAP reads every
// rasterized trixel as a caster, so the GRID spin cubes (VOXEL_TO_TRIXEL) and
// this SDF floor (SHAPES_TO_TRIXEL) both participate, like shape_debug's proven
// floor. The earlier covers-the-orbit placement (kFloorZ=16, span=560) sat 3x
// shape_debug's caster->floor gap below the casters, so the shadows never read.
// This plate mirrors shape_debug instead: a focused floor a few u below the 12^3
// spin cubes (#1585), whose bottoms sit at z=0 (centered at z=-6), with the span
// sized to the cluster — NOT out to the orbit radius (200) — so it doesn't
// intersect the orbit shapes or the z=+-42 re-voxelize column.
//
// Screen-locked DETACHED casters (orbit ring, canary cubes) intentionally cast
// NO world shadow: they composite as overlays at fixed depth and never write
// world trixelDistances (#1582 resolved Option B). WORLD-PLACED re-voxelize
// solids DO cast (P4b-3 / #1596): the bake resolves their model-frame depth to
// the world frame, so the grounded proof solid drops a shadow here beside the
// GRID cubes'.
constexpr float kFloorZ = 4.0f;      // a few u below the spin-cube bottoms (z=0)
constexpr float kFloorSpan = 120.0f; // covers the cluster + grid, short of orbit
constexpr float kFloorThickness = 4.0f;
// Light neutral gray (like shape_debug's proven floor) so the cast shadows —
// which drop to ambient — read as distinct dark patches. A dark floor washes
// the shadows out (shadowed floor ≈ unlit floor → no contrast).
constexpr Color kFloorColor{150, 152, 160, 255};

CanvasStressSettings g_settings{};
int g_autoWarmupFrames = 0;

bool groupEnabled(std::uint32_t group) {
    return g_settings.onlyGroups_ == 0u || (g_settings.onlyGroups_ & group) != 0u;
}

// The comparison region (#1374) is OPT-IN, unlike the demo's other spawn groups
// (which are all-on by default). It must NOT appear in the default render-verify
// run — the so3_* / revoxelize_solids manifest shots render the whole world, so
// any added world content breaks their byte-identical canary. It spawns only
// when `--only` explicitly names "compare" (e.g. `--only compare` for the
// isolated labeled side-by-side, or `--only compare,maingrid` to mix).
bool compareGroupRequested() {
    return (g_settings.onlyGroups_ & kGroupCompare) != 0u;
}

std::uint32_t parseSpawnGroups(const char *arg) {
    struct GroupName {
        const char *name_;
        std::uint32_t bit_;
    };
    constexpr GroupName kGroupNames[]{
        {"maingrid", kGroupMainGrid},
        {"gridspin", kGroupGridSpin},
        {"canary", kGroupCanary},
        {"revox", kGroupReVox},
        {"orbit", kGroupOrbit},
        {"floor", kGroupFloor},
        {"compare", kGroupCompare},
    };
    std::uint32_t bits = 0u;
    const std::string list{arg};
    std::size_t start = 0;
    while (start <= list.size()) {
        std::size_t end = list.find(',', start);
        if (end == std::string::npos) {
            end = list.size();
        }
        const std::string token = list.substr(start, end - start);
        bool matched = false;
        for (const auto &group : kGroupNames) {
            if (token == group.name_) {
                bits |= group.bit_;
                matched = true;
                break;
            }
        }
        if (!matched && !token.empty()) {
            IR_LOG_WARN("canvas_stress: unknown --only group '{}'", token);
        }
        start = end + 1;
    }
    return bits;
}

// Combined shot table (base SO(3) suite + the re-voxelize framing shots),
// built at runtime in initSystems because the re-voxelize shots' camera-iso
// positions are derived from world positions. File-scope so it outlives the
// game loop, per the AutoScreenshotConfig lifetime contract.
std::vector<IRVideo::AutoScreenshotShot> g_allShots;

// Generated sweep-shot labels (#1721) need stable storage for the
// AutoScreenshotShot `const char *label_` lifetime contract. Reserved to its
// final size before any push so c_str() pointers never move.
std::vector<std::string> g_sweepShotLabels;

// SO(3) detached-canvas regression suite (#1444).
//
// Five shots at varying camera yaw and zoom cover the five regression checks:
// smooth-deformation at cardinal yaw, off-snap discriminating at 45°, wide
// cross-host parity, GRID cross-check (GRID path unaffected by DETACHED
// changes), and a second cross-host parity angle at 30°.
//
// Identity fast-path (zero-rotation) is verified by running the demo with
// --no-spin (entities stay at quaternion identity): any regression in the
// identity shader path causes that separate run to diverge from its baseline.
constexpr IRVideo::AutoScreenshotShot kShots[] = {
    // (1) Primary: standard camera at yaw=0. Fails if SO(3) deform breaks.
    {1.0f, vec2(0, 0), 0.0f, "so3_smooth_sweep"},
    // (2) Discriminating: 45° camera yaw exposes per-voxel depth ordering on
    //     non-octahedral-snap poses (R^-1*(1,1,1) depth axis regression).
    {1.0f, vec2(0, 0), IRMath::kQuarterPi, "so3_offsnap_disc"},
    // (3) Wide parity: full-scene zoom-out for cross-host GL/Metal comparison.
    {0.65f, vec2(0, 0), 0.0f, "so3_wide_parity"},
    // (4) GRID cross-check: wider zoom reveals GRID-mode cubes on the ground.
    //     Validates the (1,1,1) depth path is untouched by DETACHED changes.
    {0.45f, vec2(0, 0), 0.0f, "so3_grid_cross"},
    // (5) Off-snap wide: 30° camera yaw + wide zoom; second cross-host angle.
    {0.65f, vec2(0, 0), IRMath::kPi / 6.0f, "so3_offsnap_wide"},
};

// One detached SO(3) CANARY cube (#1259 / #1589): a per-entity canvas (textures
// + voxel pool), a voxel cube allocated into that pool, and a world entity
// carrying C_EntityCanvas + RotationMode::DETACHED_REVOXELIZE that
// ENTITY_CANVAS_TO_FRAMEBUFFER composites. Rides re-voxelize — the sole shipped
// detached SO(3) renderer (#1581), where SYSTEM_REBUILD_DETACHED_VOXELS re-fills
// the private pool at the full rotation's cell positions each frame — not plain
// DETACHED's single-canvas faceDeformationMatrixSO3 deform, whose off-snap
// degradation produced #1584's B/C artifacts. Reads cleanly at every pose.

void spawnDetachedVoxelObject(
    int index, vec3 worldPos, vec3 spinAxis, float spinRate, Color color
) {
    // The pool MUST span the cube's ROTATED AABB or the rotated cells clip at the
    // pool bound: a 10³ cube reaches 5√3 ≈ 8.66 cells from the centered pool
    // origin under rotation, so a 20³ pool (half-extent 10) clears it with margin
    // — the same base×√3 rule kReVoxPoolSize follows for the 12³ proof solids.
    constexpr ivec2 kCanvasSize{128, 128};
    constexpr ivec3 kPoolSize{20, 20, 20};
    constexpr ivec3 kCubeSize{10, 10, 10};

    C_EntityCanvas canvas = IRPrefab::EntityCanvas::createWithVoxelPool(
        "detached_canvas_" + std::to_string(index),
        kCanvasSize,
        kPoolSize
    );
    canvas.screenLocked_ = g_settings.screenLockDetached_;
    // #1958 Bug-A acceptance: the floating canary cubes carry FOREGROUND depth
    // priority so they render fully in front of the SDF floor at every zoom and
    // yaw (the `--only canary,floor` case) instead of clipping behind it as world
    // iso-depth grows. No-op under screenLocked_ (the composite gates priority on
    // !screenLocked_), so the screen-locked byte-identical regression canary is
    // unchanged. The canary still writes depth, so --depth-probe-assert 321,210
    // (now reading inside the foreground near band) still PASSes.
    canvas.depthPriority_ = 1;

    // The voxel cube lives centered in the detached canvas's pool so
    // SYSTEM_REBUILD_DETACHED_VOXELS rotates its cells about the pool origin
    // (translation-free) and keeps the cube centered on its canvas as it tumbles.
    IREntity::createEntity(
        C_LocalTransform{vec3(0.0f)},
        C_VoxelSetNew{kCubeSize, color, true, canvas.canvasEntity_}
    );

    // The world entity carries the canvas wrapper + re-voxelize rotation mode +
    // continuous spin. AUTO_SPIN_LOCAL_TRANSFORM advances C_LocalTransform's SO(3)
    // quaternion each UPDATE tick; PROPAGATE_CANVAS_ROTATION threads it onto the
    // canvas, and REBUILD_DETACHED_VOXELS re-rasterizes the cells at that rotation.
    IREntity::createEntity(
        C_LocalTransform{worldPos},
        C_RotationMode{RotationMode::DETACHED_REVOXELIZE},
        C_AutoSpin{spinAxis, spinRate},
        canvas
    );
}

// Color a re-voxelize verification voxel by its model position so a stale
// exposed-mask defect (#1557) reads as a color break (a wrong / buried face) or
// a black gap (a hole) instead of being hidden by a uniform fill. Each model
// axis drives one RGB channel, so every visible face shows a distinct gradient.
Color reVoxelizeVerifyColor(vec3 modelPos, ivec3 size) {
    const vec3 half = vec3(size) * 0.5f;
    auto chan = [](float v, float ext) -> std::uint8_t {
        const float t = IRMath::clamp((v + ext) / IRMath::max(2.0f * ext, 1.0f), 0.0f, 1.0f);
        return static_cast<std::uint8_t>(40.0f + 200.0f * t);
    };
    return Color{chan(modelPos.x, half.x), chan(modelPos.y, half.y), chan(modelPos.z, half.z), 255};
}

// One detached RE-VOXELIZE proof solid (#1555). Same canvas + private-pool shape
// as the canary cubes above (also RotationMode::DETACHED_REVOXELIZE since #1589),
// but carries the carve / multi-color verification machinery: RotationMode::DETACHED_REVOXELIZE
// routes it through the GPU scatter (cells re-filled at the full rotation) + the
// cardinal raster path. When `carveAsymmetric`, an +x/+y quadrant column is
// carved out of the centered box to make an L-prism — the asymmetric solid whose
// true-3D rotation a 2D forward-scatter skew cannot represent (the headline
// #1551 discriminator). When `multiColor`, each voxel is tinted by its model
// position — the P3 verification vehicle (#1557): a dense uniform solid hides the
// stale exposed-mask defect (the distance buffer fills gated holes, uniform color
// masks wrong faces), so the discriminating solid must be multi-color (and the
// carve makes it sparse/concave so rotation changes which faces are exposed).
// `initialRotation` seeds a clear off-cardinal pose so even shot 0 reads as true-3D.
void spawnDetachedReVoxelizeSolid(
    int index,
    vec3 worldPos,
    vec4 initialRotation,
    vec3 spinAxis,
    float spinRate,
    Color color,
    bool carveAsymmetric,
    bool multiColor = false,
    bool screenLocked = false
) {
    C_EntityCanvas canvas = IRPrefab::EntityCanvas::createWithVoxelPool(
        "revox_canvas_" + std::to_string(index),
        kReVoxCanvasSize,
        kReVoxPoolSize
    );
    // World-depth compositing is the engine default (#1624); `screenLocked`
    // (--screen-lock-detached / --solo-revox) reverts this solid to the
    // fixed-depth overlay regression path.
    canvas.screenLocked_ = screenLocked;

    // Light the re-voxelize solid (#1558): AO + directional sun + sky. Attached
    // HERE — on the re-voxelize canvas only — not on the shared kVoxelPoolCanvas
    // builder, which also builds the main canvas (double-allocating its AO and
    // lighting the forward-scatter cubes, breaking the byte-identical guarantee).
    // C_TrixelCanvasRenderBehavior's default useCameraPositionIso_ lets the AO /
    // lighting passes run over this canvas; COMPUTE_VOXEL_AO + LIGHTING_TO_TRIXEL
    // author its own (cardinal) voxel frame before each dispatch. No
    // C_CanvasSunShadow / C_CanvasLightVolume here even on the opt-in world path:
    // a world-placed solid RECEIVES the SHARED world sun-shadow map + 128³ light
    // volume at its recovered world pos directly in LIGHTING_TO_TRIXEL (#1576
    // P4b-2), not via a per-canvas texture.
    if (!g_settings.noLighting_) {
        IREntity::setComponent(canvas.canvasEntity_, C_TrixelCanvasRenderBehavior{});
        IREntity::setComponent(canvas.canvasEntity_, C_CanvasAOTexture{kReVoxCanvasSize});
    }

    // Centered around origin so SYSTEM_REBUILD_DETACHED_VOXELS can rotate the
    // cells about the pool origin (translation-free) and keep the solid centered
    // on its canvas as it tumbles.
    EntityId solid = IREntity::createEntity(
        C_LocalTransform{vec3(0.0f)},
        C_VoxelSetNew{kReVoxSolidSize, color, true, canvas.canvasEntity_}
    );

    if (carveAsymmetric) {
        C_VoxelSetNew &voxelSet = IREntity::getComponent<C_VoxelSetNew>(solid);
        for (int i = 0; i < voxelSet.numVoxels_; ++i) {
            const vec3 pos = voxelSet.positions_[i].pos_;
            if (pos.x > 0.0f && pos.y > 0.0f) {
                voxelSet.voxels_[i].deactivate();
            }
        }
        voxelSet.syncActiveMask();
        // Seed the model-space face-occlusion bits for the carved shape. The
        // re-voxelize raster no longer consults this mask: it is in the unrotated
        // authoring frame, so gating the rotated cells against it dropped /
        // mis-coloured faces, and `c_voxel_to_trixel_stage_{1,2}` now bypass it
        // for re-voxelize (#1570). The seed is vestigial today, kept as the
        // correct initial state for a future GPU rotated-cell mask recompute.
        IRPrefab::Voxel::recomputeFaceOccupancy(voxelSet.voxels_, voxelSet.size_);
    }

    if (multiColor) {
        C_VoxelSetNew &voxelSet = IREntity::getComponent<C_VoxelSetNew>(solid);
        for (int i = 0; i < voxelSet.numVoxels_; ++i) {
            if (voxelSet.voxels_[i].color_.alpha_ == 0) {
                continue; // skip carved-away voxels
            }
            voxelSet.voxels_[i].color_ =
                reVoxelizeVerifyColor(voxelSet.positions_[i].pos_, voxelSet.size_);
        }
        // RGBA written through the raw pool span (no alpha change) — the active
        // mask is unaffected, and STAGE_1 re-uploads the colors each frame.
    }

    IREntity::createEntity(
        C_LocalTransform{worldPos, initialRotation},
        C_RotationMode{RotationMode::DETACHED_REVOXELIZE},
        C_AutoSpin{spinAxis, spinRate},
        canvas
    );
}

// Orbit ring (#1259 follow-on): a wider second ring of MORE shape variety that
// tumbles around the existing center cluster. Each entry carves a distinct
// silhouette out of a centered voxel box and spins it, cycling the three
// rotation modes so one ring exercises the main world canvas (GRID) AND both
// detached-canvas paths (forward-scatter DETACHED + DETACHED_REVOXELIZE) side by
// side. Colors are uniform per shape so the silhouette reads cleanly; the
// multi-color re-voxelize L-prism above stays the dedicated exposed-mask canary.
enum class OrbitShape { CUBE, SPHERE, OCTAHEDRON, PYRAMID, CROSS, FRAME };

// Carve `shape` out of a centered-around-origin voxel set by deactivating the
// voxels outside the silhouette, then resync the active mask + face-occlusion.
void carveOrbitShape(C_VoxelSetNew &vs, OrbitShape shape) {
    const vec3 half = vec3(vs.size_) * 0.5f;
    const float r = IRMath::min(half.x, IRMath::min(half.y, half.z));
    for (int i = 0; i < vs.numVoxels_; ++i) {
        const vec3 p = vs.positions_[i].pos_;
        const float ax = IRMath::abs(p.x);
        const float ay = IRMath::abs(p.y);
        const float az = IRMath::abs(p.z);
        bool keep = true;
        switch (shape) {
        case OrbitShape::SPHERE:
            keep = IRMath::length(p) <= r;
            break;
        case OrbitShape::OCTAHEDRON:
            keep = (ax + ay + az) <= r * 1.35f;
            break;
        case OrbitShape::PYRAMID: {
            const float t = (p.z + half.z) / IRMath::max(2.0f * half.z, 1.0f); // 0 base, 1 apex
            const float span = r * (1.0f - IRMath::clamp(t, 0.0f, 1.0f));
            keep = ax <= span && ay <= span;
            break;
        }
        case OrbitShape::CROSS: {
            const float arm = r * 0.52f; // thicker bars read cleanly under spin

            const int onAxis = static_cast<int>(ax <= arm) + static_cast<int>(ay <= arm) +
                               static_cast<int>(az <= arm);
            keep = onAxis >= 2; // near 2+ axes -> the three bars of a 3D plus
            break;
        }
        case OrbitShape::FRAME: {
            constexpr float edge = 1.5f;
            const int onEdge = static_cast<int>(ax >= half.x - edge) +
                               static_cast<int>(ay >= half.y - edge) +
                               static_cast<int>(az >= half.z - edge);
            keep = onEdge >= 2; // near 2+ faces -> the 12 cube edges (wireframe)
            break;
        }
        case OrbitShape::CUBE:
        default:
            keep = true;
            break;
        }
        if (!keep) {
            vs.voxels_[i].deactivate();
        }
    }
    vs.syncActiveMask();
    IRPrefab::Voxel::recomputeFaceOccupancy(vs.voxels_, vs.size_);
}

// Spawn one orbit-ring shape at `worldPos`, carved to `shape`, spinning about
// `spinAxis` at `spinRate`. `mode` selects the render path: GRID rasterizes into
// the shared world pool; DETACHED / DETACHED_REVOXELIZE each get a private canvas
// + pool composited by ENTITY_CANVAS_TO_FRAMEBUFFER.
void spawnOrbitShape(
    int index,
    vec3 worldPos,
    OrbitShape shape,
    RotationMode mode,
    int extent,
    vec3 spinAxis,
    float spinRate,
    vec4 initialRotation,
    Color color
) {
    const ivec3 shapeSize{extent, extent, extent};
    if (mode == RotationMode::GRID) {
        const EntityId e = IREntity::createEntity(
            C_LocalTransform{worldPos, initialRotation},
            C_RotationMode{RotationMode::GRID},
            C_AutoSpin{spinAxis, spinRate},
            C_VoxelSetNew{shapeSize, color, true}
        );
        carveOrbitShape(IREntity::getComponent<C_VoxelSetNew>(e), shape);
        return;
    }
    // Detached: private canvas + pool scale with the shape so large and small
    // orbiters composite at proportional on-screen size. The pool spans the
    // rotated AABB (extent x sqrt(3)); the canvas footprint (≈ 12 px/voxel, like
    // the forward-scatter cubes) sets the composite scale.
    const int poolDim = static_cast<int>(IRMath::ceil(static_cast<float>(extent) * 1.85f));
    const ivec2 canvasSize{extent * 12, extent * 12};
    const ivec3 poolSize{poolDim, poolDim, poolDim};
    C_EntityCanvas canvas = IRPrefab::EntityCanvas::createWithVoxelPool(
        "orbit_canvas_" + std::to_string(index),
        canvasSize,
        poolSize
    );
    canvas.screenLocked_ = g_settings.screenLockDetached_;
    const EntityId solid = IREntity::createEntity(
        C_LocalTransform{vec3(0.0f)},
        C_VoxelSetNew{shapeSize, color, true, canvas.canvasEntity_}
    );
    carveOrbitShape(IREntity::getComponent<C_VoxelSetNew>(solid), shape);
    IREntity::createEntity(
        C_LocalTransform{worldPos, initialRotation},
        C_RotationMode{mode},
        C_AutoSpin{spinAxis, spinRate},
        canvas
    );
}

Color gridColor(int x, int y, int gridSize) {
    const float denom = static_cast<float>(IRMath::max(gridSize - 1, 1));
    return Color{
        static_cast<std::uint8_t>(70 + 150.0f * (static_cast<float>(x) / denom)),
        static_cast<std::uint8_t>(110 + 110.0f * (static_cast<float>(y) / denom)),
        static_cast<std::uint8_t>(170),
        255
    };
}

void readConfig() {
    IRScript::LuaScript configScript{IREngine::resolveScriptPath("config.lua").c_str()};
    sol::table table = configScript.getTable("canvas_stress");
    if (!table.valid()) {
        return;
    }
    sol::object gridSize = table["main_grid_size"];
    if (gridSize.is<int>())
        g_settings.mainGridSize_ = gridSize.as<int>();
    sol::object detachedCount = table["detached_count"];
    if (detachedCount.is<int>())
        g_settings.detachedCount_ = detachedCount.as<int>();
    sol::object zoom = table["initial_zoom"];
    if (zoom.is<float>())
        g_settings.initialZoom_ = zoom.as<float>();
    sol::object autoRotate = table["auto_rotate"];
    if (autoRotate.is<bool>() && !g_settings.autoRotateSetByCli_)
        g_settings.autoRotate_ = autoRotate.as<bool>();
}

void parseArgs(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &g_autoWarmupFrames);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--yaw") == 0 && i + 1 < argc) {
            g_settings.cameraYaw_ = static_cast<float>(std::atof(argv[i + 1]));
            ++i;
        } else if (std::strcmp(argv[i], "--auto-rotate") == 0) {
            g_settings.autoRotate_ = true;
            g_settings.autoRotateSetByCli_ = true;
        } else if (std::strcmp(argv[i], "--no-auto-rotate") == 0) {
            g_settings.autoRotate_ = false;
            g_settings.autoRotateSetByCli_ = true;
        } else if (std::strcmp(argv[i], "--full-rotate") == 0) {
            g_settings.fullRotate_ = true;
        } else if (std::strcmp(argv[i], "--no-spin") == 0) {
            g_settings.noSpin_ = true;
        } else if (std::strcmp(argv[i], "--no-lighting") == 0) {
            g_settings.noLighting_ = true;
        } else if (
            std::strcmp(argv[i], "--screen-lock-detached") == 0 ||
            std::strcmp(argv[i], "--screen-lock-revox") == 0
        ) {
            g_settings.screenLockDetached_ = true;
        } else if (std::strcmp(argv[i], "--solo-revox") == 0) {
            g_settings.soloRevox_ = true;
            // The solo harness spawns no floor to depth-sort against, and the
            // #1619 step-0 GL evidence (committed crops) was captured on the
            // screen-locked overlay path — keep the lone L-prism screen-locked
            // rather than inheriting the world-place default.
            g_settings.screenLockDetached_ = true;
        } else if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            g_settings.onlyGroups_ |= parseSpawnGroups(argv[i + 1]);
            ++i;
        } else if (std::strcmp(argv[i], "--sweep-yaw") == 0 && i + 3 < argc) {
            g_settings.sweepYawFrom_ = static_cast<float>(std::atof(argv[i + 1]));
            g_settings.sweepYawTo_ = static_cast<float>(std::atof(argv[i + 2]));
            g_settings.sweepYawCount_ = std::atoi(argv[i + 3]);
            i += 3;
        } else if (std::strcmp(argv[i], "--sweep-frames") == 0 && i + 2 < argc) {
            g_settings.sweepFramesCount_ = std::atoi(argv[i + 1]);
            g_settings.sweepFramesSettle_ = std::atoi(argv[i + 2]);
            i += 2;
        } else if (std::strcmp(argv[i], "--auto-profile") == 0) {
            g_settings.autoProfile_ = true;
        } else if (std::strcmp(argv[i], "--debug-overlay") == 0 && i + 1 < argc) {
            g_settings.debugOverlay_ = IRRender::debugOverlayModeFromString(argv[i + 1]);
            ++i;
        } else if (std::strcmp(argv[i], "--depth-probe") == 0 && i + 1 < argc) {
            int px = 0;
            int py = 0;
            if (std::sscanf(argv[i + 1], "%d,%d", &px, &py) == 2) {
                g_settings.depthProbeSet_ = true;
                g_settings.depthProbePixel_ = ivec2(px, py);
            } else {
                IR_LOG_WARN(
                    "--depth-probe: expected X,Y (e.g. 960,540); ignoring '{}'",
                    argv[i + 1]
                );
            }
            ++i;
        } else if (std::strcmp(argv[i], "--depth-probe-assert") == 0 && i + 1 < argc) {
            int px = 0;
            int py = 0;
            if (std::sscanf(argv[i + 1], "%d,%d", &px, &py) == 2) {
                g_settings.depthProbeAssertSet_ = true;
                g_settings.depthProbeAssertPixel_ = ivec2(px, py);
            } else {
                IR_LOG_WARN(
                    "--depth-probe-assert: expected X,Y (e.g. 321,210); ignoring '{}'",
                    argv[i + 1]
                );
            }
            ++i;
        }
    }
    if (g_settings.sweepYawCount_ > 0 || g_settings.sweepFramesCount_ > 0) {
        // Sweep captures isolate one moving variable; continuous camera
        // auto-yaw would otherwise drift the pose during the settle frames and
        // confound both sweep kinds.
        g_settings.autoRotate_ = false;
        g_settings.autoRotateSetByCli_ = true;
    }
}

} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    parseArgs(argc, argv);
    IR_LOG_INFO("Starting creation: canvas_stress");
    IREngine::init(argv[0]);
    readConfig();
    if (g_settings.autoProfile_) {
        IREngine::enableFrameTiming(true);
    }

    initSystems();
    initCommands();
    initEntities();

    IRRender::setCameraPosition2DIso(vec2(0.0f, 0.0f));
    IRRender::setCameraZoom(g_settings.initialZoom_);
    IRPrefab::Camera::setYaw(g_settings.cameraYaw_);

    // T-1 (#1767): force the requested debug overlay for the whole run.
    // NONE (the default) is a no-op so a flagless run stays byte-identical.
    if (g_settings.debugOverlay_ != IRRender::DebugOverlayMode::NONE) {
        IRRender::setDebugOverlay(g_settings.debugOverlay_);
    }

    IREngine::gameLoop();
    return 0;
}

void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {// LOD_UPDATE writes the C_ActiveLodLevel singleton that SHAPES_TO_TRIXEL
         // reads at beginTick (the floor SDF box renders through that pass).
         IRSystem::createSystem<IRSystem::LOD_UPDATE>(),
         IRSystem::createSystem<IRSystem::AUTO_SPIN_LOCAL_TRANSFORM>(),
         IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
         IRSystem::createSystem<IRSystem::REBUILD_GRID_VOXELS>(),
         IRSystem::createSystem<IRSystem::PROPAGATE_CANVAS_ROTATION>(),
         // Detached re-voxelize (#1555): fills each DETACHED_REVOXELIZE canvas's
         // private pool at the full-rotation cell positions. Must run AFTER
         // PROPAGATE_CANVAS_ROTATION (needs the camera-composed rotation) and
         // UPDATE_VOXEL_SET_CHILDREN (overwrites its translate-only baseline).
         IRSystem::createSystem<IRSystem::REBUILD_DETACHED_VOXELS>(),
         IRSystem::createSystem<IRSystem::LIFETIME>()
        }
    );
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>(),
         IRSystem::System<IRSystem::CAMERA_SCROLL_ZOOM>::create()}
    );

    std::list<IRSystem::SystemId> renderPipeline = IRPrefab::Camera::standardControlSystems();

    if (g_settings.autoRotate_) {
        renderPipeline.push_back(
            IRSystem::createSystem<IRSystem::AUTO_YAW_ROTATE>(kYawDeltaPerFrame)
        );
    }

    if (g_settings.fullRotate_) {
        // SO(3) camera driver — composes per-frame X/Y/Z spins into the
        // camera's C_LocalTransform.rotation_ via setRotationQuat. C_Camera
        // anchors the singleton tick; GRID canvases see only the Z-component
        // (via IRPrefab::Camera::getYaw); DETACHED canvases pick up the full
        // quat through PROPAGATE_CANVAS_ROTATION.
        renderPipeline.push_back(
            IRSystem::createSystem<C_Camera>(
                "AutoFullRotate",
                [](C_Camera &) {},
                []() {
                    const vec4 delta = IRMath::quatMul(
                        IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), kFullRotateYawPerFrame),
                        IRMath::quatMul(
                            IRMath::quatAxisAngle(
                                vec3(0.0f, 1.0f, 0.0f),
                                kFullRotatePitchYPerFrame
                            ),
                            IRMath::quatAxisAngle(vec3(1.0f, 0.0f, 0.0f), kFullRotatePitchPerFrame)
                        )
                    );
                    IRPrefab::Camera::setRotationQuat(
                        IRMath::quatMul(delta, IRPrefab::Camera::getRotationQuat())
                    );
                }
            )
        );
    }

    if (!g_settings.noLighting_) {
        renderPipeline.push_back(IRSystem::createSystem<IRSystem::BUILD_LIGHT_OCCLUSION_GRID>());
    }
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>());
    if (!g_settings.noLighting_) {
        // SDF floor pass — runs after the voxel raster and before the lighting/
        // shadow passes so the floor is in `trixelDistances` when BAKE_SUN_SHADOW_MAP
        // projects the GRID casters' shadows onto it.
        renderPipeline.push_back(IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>());
        renderPipeline.push_back(IRSystem::createSystem<IRSystem::COMPUTE_VOXEL_AO>());
        // Collapses the per-axis voxel canvases into the cardinal-layout
        // resolve texture BAKE_SUN_SHADOW_MAP casts (#1435). Without it the
        // voxel cubes cast NO shadow at any non-cardinal camera yaw — the
        // per-axis canvases gracefully degrade to an empty resolve — which is
        // most of this demo's runtime under the default --auto-rotate (#1719).
        renderPipeline.push_back(IRSystem::createSystem<IRSystem::RESOLVE_PER_AXIS_SCREEN_DEPTH>());
        renderPipeline.push_back(IRSystem::createSystem<IRSystem::BAKE_SUN_SHADOW_MAP>());
        renderPipeline.push_back(IRSystem::createSystem<IRSystem::COMPUTE_SUN_SHADOW>());
        renderPipeline.push_back(IRSystem::createSystem<IRSystem::COMPUTE_LIGHT_VOLUME>());
        renderPipeline.push_back(IRSystem::createSystem<IRSystem::LIGHTING_TO_TRIXEL>());
    }
    // Per-mode in-scene labels for the comparison region (#1374). The main canvas
    // is cleared + rebuilt every frame, so labels must be redrawn here each frame
    // — AFTER lighting (to keep their authored color) and BEFORE
    // TRIXEL_TO_FRAMEBUFFER. WORLD-ANCHORED (projected through the live camera
    // yaw, mirroring the voxel raster's `trixelFrameOffset` mapping) so only the
    // compare_* shots capture them; a screen-locked overlay would paint every
    // shot and break the so3_* canary. C_Camera anchors the singleton tick; the
    // draw runs in beginTick (once per frame).
    if (!g_settings.soloRevox_ && compareGroupRequested()) {
        renderPipeline.push_back(
            IRSystem::createSystem<C_Camera>(
                "CompareRegionLabels",
                [](C_Camera &) {},
                []() {
                    C_TriangleCanvasTextures &canvas =
                        IREntity::getComponent<C_TriangleCanvasTextures>(
                            IRRender::getActiveCanvasEntity()
                        );
                    const float yaw = IRPrefab::Camera::getYaw();
                    const int sub =
                        IRRender::getSubdivisionMode() != IRRender::SubdivisionMode::NONE
                            ? IRMath::max(IRRender::getVoxelRenderEffectiveSubdivisions(), 1)
                            : 1;
                    const float subF = static_cast<float>(sub);
                    const ivec2 originOffset = IRMath::trixelOriginOffsetZ1(canvas.size_);
                    const ivec2 cameraTerm =
                        ivec2(IRMath::floor(IRRender::getEffectiveCameraIso() * subF));
                    for (int i = 0; i < 3; ++i) {
                        const vec2 iso = IRMath::pos3DtoPos2DIsoYawed(compareCellWorld(i), yaw);
                        const ivec2 cellTrixel =
                            originOffset + cameraTerm + IRMath::roundVec(iso * subF);
                        const ivec2 labelSize = IRRender::measureText(kCompareLabels[i]);
                        const ivec2 labelPos =
                            cellTrixel + ivec2(-labelSize.x / 2, kCompareLabelBelowCells * sub);
                        IRRender::renderText(
                            canvas,
                            kCompareLabels[i],
                            labelPos,
                            kCompareColors[i]
                        );
                    }
                }
            )
        );
    }

    renderPipeline.push_back(IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>());
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::ENTITY_CANVAS_TO_FRAMEBUFFER>());
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>());

    // #1910 composite-depth probe — registered only with --depth-probe so a
    // flagless run adds no system. Runs last (after the framebuffer composite is
    // complete) and logs the depth-test winner at the requested pixel each frame.
    if (g_settings.depthProbeSet_) {
        const ivec2 probePixel = g_settings.depthProbePixel_;
        renderPipeline.push_back(
            IRSystem::createSystem<C_Camera>(
                "DepthProbe",
                [](C_Camera &) {},
                [probePixel]() { IRPrefab::DepthProbe::logCompositeDepth(probePixel); }
            )
        );
    }

    // #1957 depth-write regression guard — registered only with
    // --depth-probe-assert. Runs after the framebuffer composite and emits a
    // PASS/FAIL verdict line so a headless run catches a future pass that
    // disables the detached-canvas composite depth-write.
    if (g_settings.depthProbeAssertSet_) {
        const ivec2 assertPixel = g_settings.depthProbeAssertPixel_;
        renderPipeline.push_back(
            IRSystem::createSystem<C_Camera>(
                "DepthProbeAssert",
                [](C_Camera &) {},
                [assertPixel]() { IRPrefab::DepthProbe::assertCompositeWritesDepth(assertPixel); }
            )
        );
    }

    if (g_autoWarmupFrames > 0) {
        int settleFrames = 60;
        if (g_settings.sweepYawCount_ > 0 || g_settings.sweepFramesCount_ > 0) {
            // #1721 sweep mode: a focused diagnostic capture REPLACES the base
            // suite — the swept variable (camera yaw, or entity spin phase via
            // inter-shot settle frames) should be the only thing changing.
            const int totalSweepShots = IRMath::max(g_settings.sweepYawCount_, 0) +
                                        IRMath::max(g_settings.sweepFramesCount_, 0);
            g_sweepShotLabels.reserve(totalSweepShots);
            for (int i = 0; i < g_settings.sweepYawCount_; ++i) {
                const float t =
                    g_settings.sweepYawCount_ > 1
                        ? static_cast<float>(i) / static_cast<float>(g_settings.sweepYawCount_ - 1)
                        : 0.0f;
                const float yaw = g_settings.sweepYawFrom_ +
                                  t * (g_settings.sweepYawTo_ - g_settings.sweepYawFrom_);
                g_sweepShotLabels.push_back("sweep_yaw_" + std::to_string(i));
                g_allShots.push_back({1.0f, vec2(0.0f), yaw, g_sweepShotLabels.back().c_str()});
            }
            for (int i = 0; i < g_settings.sweepFramesCount_; ++i) {
                g_sweepShotLabels.push_back("sweep_frame_" + std::to_string(i));
                g_allShots.push_back(
                    {1.0f, vec2(0.0f), g_settings.cameraYaw_, g_sweepShotLabels.back().c_str()}
                );
            }
            if (g_settings.sweepFramesSettle_ > 0) {
                settleFrames = g_settings.sweepFramesSettle_;
            }
        } else {
            // Base SO(3) suite + dedicated re-voxelize framing shots. Detached
            // canvases rasterize their canvas-local pool against the MAIN camera's
            // world cull viewport, so a camera PAN moves the viewport off the pool
            // and culls every detached canvas — the framing shots must stay at
            // camera (0,0). The re-voxelize solids sit on the screen-center column
            // (z-separated) and read large there; the solids' own auto-spin supplies
            // the rotation. Zoom 1 frames both; the wide shot gives full context.
            g_allShots.assign(kShots, kShots + sizeof(kShots) / sizeof(kShots[0]));
            g_allShots.push_back({1.0f, vec2(0.0f), 0.0f, "revoxelize_solids"});
            // Zoomed in on the screen-center column so the re-voxelized cube + L read
            // clearly as true-3D solids (voxel centers reorganized), the headline
            // #1555 criterion. Round-to-cell speckle is the expected P1 aliasing.
            g_allShots.push_back({2.2f, vec2(0.0f), 0.0f, "revoxelize_solids_zoom"});
            // Wide pull-back framing the full orbit ring (radius 200) around the
            // center cluster, so the added shape variety reads as one tumbling orbit.
            g_allShots.push_back({0.32f, vec2(0.0f), 0.0f, "orbit_overview"});
            // T-1 (#1767) structural-validation framing shots for epic #1766.
            // Appended AFTER the manifest suite so render-verify (which reads the
            // first N=manifest shots) ignores them — they feed the structural
            // metrics, not the pixel-diff gate (T-2 #1768 wires the gate). Both
            // stay at camera (0,0) for the detached-canvas cull contract above.
            //
            // (a) Cast-shadow occupancy: a wide framing of the #1587 SDF floor,
            //     the GRID spin row, and the grounded re-voxelize cast-proof cube
            //     (#1596). Capture with `--debug-overlay shadow` so cast shadows
            //     read magenta and render-shadow-metric.py (#1765) measures
            //     hole_ratio / components (the swiss-cheese signature, #1717).
            g_allShots.push_back({0.55f, vec2(0.0f), 0.0f, "shadow_overlay_floor"});
            // (b) Re-voxelize surface coverage: the screen-center solid column
            //     zoomed for per-voxel surface readout, captured in normal render
            //     so a coverage metric can count interior background holes inside
            //     the solid silhouette (the #1619-class missing-face defect).
            g_allShots.push_back({2.4f, vec2(0.0f), 0.0f, "revox_coverage"});
            // Unified-rotation comparison region (#1374), OPT-IN via
            // `--only compare`. A tight zoom frames the labeled row (GRID-attached
            // spin / world-Z-yaw GRID / DETACHED SO(3)) at camera (0,0). Two
            // camera yaws exercise the shared-yaw comparison: yaw 0 (cardinal) and
            // an off-cardinal quarter-pi that drives the world-Z-yaw cell's
            // per-axis deform; compare_detached re-frames at a half-pi yaw so the
            // detached cell's SO(3) bake reads at another pose. Appended after the
            // manifest suite (render-verify's first-N pixel-diff ignores them) AND
            // emitted only in the opt-in run, so the default capture is unchanged.
            if (compareGroupRequested()) {
                g_allShots.push_back({kCompareZoom, vec2(0.0f), 0.0f, "compare_yaw0"});
                g_allShots.push_back(
                    {kCompareZoom, vec2(0.0f), IRMath::kQuarterPi, "compare_yaw_q"}
                );
                g_allShots.push_back(
                    {kCompareZoom, vec2(0.0f), IRMath::kHalfPi, "compare_detached"}
                );
            }
        }

        IRVideo::AutoScreenshotConfig cfg{};
        cfg.warmupFrames_ = g_autoWarmupFrames;
        // Base suite: 60 settle frames between shots — entities advance ~24° at
        // base spin rate, giving clearly distinct rotation states per shot.
        // Sweep mode: --sweep-frames' settle overrides so spin phase advances
        // by a controlled amount between captures.
        cfg.settleFrames_ = settleFrames;
        cfg.shots_ = g_allShots.data();
        cfg.numShots_ = static_cast<int>(g_allShots.size());
        renderPipeline.push_back(IRVideo::createAutoScreenshotSystem(cfg));
    }

    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}

void initCommands() {
    IRPrefab::Camera::registerStandardKeyboardCommands();
    IRCommand::registerCaptureCommands();
}

void initEntities() {
    EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    IREntity::setComponent(mainCanvas, C_TrixelCanvasRenderBehavior{});

    // Lighting wiring (#1259). The lighting pipeline writes per-canvas
    // shadow / AO / light-volume textures sized to the main canvas;
    // they're allocated once at startup. Sun direction / intensity /
    // ambient are global render state set below.
    if (!g_settings.noLighting_) {
        const ivec2 mainCanvasSize =
            IREntity::getComponent<C_TriangleCanvasTextures>(mainCanvas).size_;
        IREntity::setComponent(mainCanvas, C_CanvasAOTexture{mainCanvasSize});
        IREntity::setComponent(mainCanvas, C_CanvasSunShadow{mainCanvasSize});
        IREntity::setComponent(mainCanvas, C_CanvasLightVolume{});
        IRRender::setSunDirection(kSunDirection);
        IRRender::setSunIntensity(kSunIntensity);
        IRRender::setSunAmbient(kSunAmbient);
        IRRender::setSunShadowsEnabled(true);
        IRRender::setAOEnabled(true);

        // Shadow floor (SDF box) just below the center GRID spin cluster.
        // Receives sun shadow + AO. The GRID-mode spin cubes and the grounded
        // WORLD-PLACED re-voxelize solid cast onto it (P4b-3 / #1596);
        // screen-locked DETACHED entities (orbit ring, canary cubes) do NOT —
        // they composite as overlays after the shadow bake and never write
        // world depth (#1582 Option B).
        if (!g_settings.soloRevox_ && groupEnabled(kGroupFloor)) {
            const EntityId floor = IREntity::createEntity(
                C_LocalTransform{vec3(0.0f, 0.0f, kFloorZ)},
                C_ShapeDescriptor{
                    IRRender::ShapeType::BOX,
                    vec4(kFloorSpan, kFloorSpan, kFloorThickness, 0.0f),
                    kFloorColor
                }
            );
            IREntity::setComponent(floor, C_LightBlocker{false, false, 0.0f});
        }
    }

    // Main-canvas GRID grid: a flat lattice of small voxel cubes. Exercises
    // T-293 inter-trixel deformation on the world canvas under camera yaw.
    // nConfigured keeps dependent placement (the GRID spin row's y) stable
    // when the group is filtered out by --only (#1721).
    const int nConfigured = IRMath::max(0, g_settings.mainGridSize_);
    const int n = (g_settings.soloRevox_ || !groupEnabled(kGroupMainGrid)) ? 0 : nConfigured;
    constexpr float kGridSpacing = 7.0f;
    const float gridCenter = (static_cast<float>(IRMath::max(n, 1)) - 1.0f) * 0.5f;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const vec3 pos{
                (static_cast<float>(x) - gridCenter) * kGridSpacing,
                (static_cast<float>(y) - gridCenter) * kGridSpacing,
                0.0f
            };
            IREntity::createEntity(
                C_LocalTransform{pos},
                C_VoxelSetNew{ivec3(3, 3, 3), gridColor(x, y, n), true}
            );
        }
    }

    // GRID-mode rotating cluster: a row of mid-sized cubes that spin in
    // place around staggered axes. Each tick AUTO_SPIN_LOCAL_TRANSFORM
    // advances C_LocalTransform.rotation_, PROPAGATE_TRANSFORM composes
    // C_WorldTransform, and REBUILD_GRID_VOXELS re-rasterizes the voxel
    // cells into the world pool (#1259 §C6 — face-swapping / cell-aliasing
    // path). Sits above the main grid so the re-rasterized cells are
    // clearly visible against the flat ground.
    constexpr int kGridSpinCount = 4;
    constexpr ivec3 kGridSpinCubeSize{12, 12, 12};
    constexpr float kGridSpinSpacing = 16.0f;
    constexpr vec3 kGridSpinAxes[kGridSpinCount]{
        {0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {1.0f, 1.0f, 1.0f},
    };
    constexpr Color kGridSpinColors[kGridSpinCount]{
        {255, 120, 120, 255},
        {120, 255, 120, 255},
        {120, 160, 255, 255},
        {255, 220, 120, 255},
    };
    const float gridSpinCenter = (static_cast<float>(kGridSpinCount) - 1.0f) * 0.5f;
    const float gridSpinY = (static_cast<float>(nConfigured) * 0.5f + 1.5f) * kGridSpacing;
    const int gridSpinCount =
        (g_settings.soloRevox_ || !groupEnabled(kGroupGridSpin)) ? 0 : kGridSpinCount;
    for (int i = 0; i < gridSpinCount; ++i) {
        const vec3 pos{
            (static_cast<float>(i) - gridSpinCenter) * kGridSpinSpacing,
            gridSpinY,
            -static_cast<float>(kGridSpinCubeSize.z) * 0.5f
        };
        IREntity::createEntity(
            C_LocalTransform{pos},
            C_RotationMode{RotationMode::GRID},
            C_AutoSpin{kGridSpinAxes[i], g_settings.noSpin_ ? 0.0f : kGridSpinRadPerFrame},
            C_VoxelSetNew{kGridSpinCubeSize, kGridSpinColors[i], true}
        );
    }

    // Detached re-voxelize CANARY cubes: a grid of per-entity canvases, each at a
    // distinct SO(3) rotation. The world spacing must exceed the detached canvas
    // footprint (composited at canvasSize / mainCanvasSize of the framebuffer)
    // or the canvases overlap — kDetachedSpacing is sized for the 128-px canvas.
    const int detached = (g_settings.soloRevox_ || !groupEnabled(kGroupCanary))
                             ? 0
                             : IRMath::max(0, g_settings.detachedCount_);
    constexpr float kDetachedSpacing = 160.0f;
    const int cols = IRMath::max(
        1,
        static_cast<int>(IRMath::ceil(IRMath::sqrt(static_cast<float>(IRMath::max(detached, 1)))))
    );
    const int rows = (detached + cols - 1) / IRMath::max(cols, 1);
    const float colCenter = (static_cast<float>(cols) - 1.0f) * 0.5f;
    const float rowCenter = (static_cast<float>(IRMath::max(rows, 1)) - 1.0f) * 0.5f;
    // Rotation axes cycled per entity so the grid shows yaw, pitch, roll, and
    // a mixed diagonal — full SO(3) re-voxelized into each canvas's private pool.
    constexpr vec3 kAxes[]{
        {0.0f, 0.0f, 1.0f}, // yaw
        {1.0f, 0.0f, 0.0f}, // pitch
        {0.0f, 1.0f, 0.0f}, // roll
        {1.0f, 1.0f, 1.0f}, // mixed diagonal
    };
    constexpr Color kDetachedColors[]{
        {230, 70, 70, 255},
        {70, 210, 90, 255},
        {80, 110, 230, 255},
        {230, 200, 60, 255},
        {210, 90, 220, 255},
        {70, 210, 210, 255},
    };
    // World-placed canaries (#1624) must clear the #1587 floor slab
    // (z ∈ [kFloorZ ± kFloorThickness/2] = [2, 6]): at the legacy z = 0 a
    // spinning 10³ cube (rotated half-diagonal 5√3 ≈ 8.66) sweeps into the
    // band and reads as sunk through the floor. z = -8 keeps the full SO(3)
    // sweep above the floor top (max extent z ≈ 0.66 < 2). The legacy z is
    // kept under --screen-lock-detached so the opt-out scene stays
    // byte-identical to the pre-#1624 default.
    const float canaryZ = g_settings.screenLockDetached_ ? 0.0f : -8.0f;
    for (int i = 0; i < detached; ++i) {
        const int col = i % cols;
        const int row = i / cols;
        const vec3 worldPos{
            (static_cast<float>(col) - colCenter) * kDetachedSpacing,
            (static_cast<float>(row) - rowCenter) * kDetachedSpacing,
            canaryZ
        };
        // Per-entity spin rate: base * (i + 1) so adjacent canvases visibly
        // de-sync within the auto-screenshot capture window (#1259). Spin
        // axis cycles independently of rate so axis/rate pairs cover the
        // full SO(3) bake matrix.
        const float spinRate =
            g_settings.noSpin_ ? 0.0f : kDetachedSpinBaseRadPerFrame * static_cast<float>(i + 1);
        spawnDetachedVoxelObject(i, worldPos, kAxes[i % 4], spinRate, kDetachedColors[i % 6]);
    }

    // Detached RE-VOXELIZE proof solids (#1555): a MULTI-COLOR asymmetric L-prism
    // (the headline true-3D discriminator, doubling as the #1557 P3 stale-mask
    // verification vehicle — multi-color + carved so a mis-gated face reads as a
    // color break / hole) and a dense uniform cube (validates the cube case the
    // epic uses to supersede #1551, and the dense-uniform control the defect
    // hides on). Seeded at a clear off-cardinal tilt so the first shot already
    // reads as a true 3D-rotated solid; a slow auto-spin then sweeps the full
    // SO(3) range to prove smoothness (no pop) and parade every residual pose.
    const float reVoxSpin = g_settings.noSpin_ ? 0.0f : kReVoxSpinPerFrame;
    if (g_settings.soloRevox_ || groupEnabled(kGroupReVox)) {
        spawnDetachedReVoxelizeSolid(
            0,
            kReVoxAsymWorld,
            IRMath::quatAxisAngle(IRMath::normalize(vec3(1.0f, 0.6f, 0.3f)), IRMath::kPi / 4.5f),
            vec3(1.0f, 1.0f, 0.4f),
            reVoxSpin,
            Color{255, 150, 60, 255},
            /*carveAsymmetric=*/true,
            /*multiColor=*/true,
            /*screenLocked=*/g_settings.screenLockDetached_
        );
    }
    if (!g_settings.soloRevox_ && groupEnabled(kGroupReVox)) {
        spawnDetachedReVoxelizeSolid(
            1,
            kReVoxCubeWorld,
            IRMath::quatAxisAngle(IRMath::normalize(vec3(0.3f, 1.0f, 0.5f)), IRMath::kPi / 5.0f),
            vec3(0.4f, 1.0f, 0.6f),
            reVoxSpin,
            Color{70, 210, 210, 255},
            /*carveAsymmetric=*/false,
            /*multiColor=*/false,
            /*screenLocked=*/g_settings.screenLockDetached_
        );
    }

    // #1619 step-0 isolation: the L-prism above is the entire scene.
    if (g_settings.soloRevox_) {
        IR_LOG_INFO("canvas_stress: --solo-revox — single rotated DETACHED_REVOXELIZE L-prism");
        return;
    }

    // Grounded world-placed CAST proof (#1576 P4b-3): a third re-voxelize cube
    // in line with the GRID spin-cube row — same 12³ size, same z (bottoms at
    // z=0, floor 4 below) — so its sun shadow lands on the #1587 floor right
    // beside the GRID cubes' shadows, a direct GRID-vs-re-voxelize cast
    // comparison. The z=±42 column solids above sit ~84 sun-Z voxels from the
    // floor, past the kMaxShadowDepthRange=24 receive cutoff, so they can never
    // demonstrate cast (the #1591 lesson); this one mirrors the proven GRID
    // caster→floor geometry instead. Follows --screen-lock-revox like the rest.
    if (groupEnabled(kGroupReVox)) {
        spawnDetachedReVoxelizeSolid(
            2,
            kReVoxGroundedWorld,
            IRMath::quatAxisAngle(IRMath::normalize(vec3(0.5f, 1.0f, 0.2f)), IRMath::kPi / 4.2f),
            vec3(0.7f, 0.3f, 1.0f),
            reVoxSpin,
            Color{210, 120, 255, 255},
            /*carveAsymmetric=*/false,
            /*multiColor=*/false,
            /*screenLocked=*/g_settings.screenLockDetached_
        );
    }

    // Orbit ring: a wider second ring of varied silhouettes that tumbles around
    // the center cluster. Cycles shape kind, rotation mode (GRID / DETACHED /
    // DETACHED_REVOXELIZE), and spin axis so the same shape variety appears on
    // both the world canvas and the detached canvases. Radius sits well outside
    // the detached grid so it reads as a distinct orbit at the wide framing shots.
    constexpr int kOrbitCount = 12;
    constexpr float kOrbitRadius = 200.0f;
    // Shape↔mode pairing is by robustness. Both CELL-REBAKING paths — GRID (main
    // world canvas) and DETACHED_REVOXELIZE (detached) — re-derive the exposed
    // mask against the rotated cells (#1570) and inverse-resample the dest
    // lattice (#1619 detached, #1720 GRID), so coverage is hole-free for solids
    // AND for thin carved shapes (the full-box span has slot slack ≫ covered
    // cells). The CROSS rides the GRID path deliberately as the standing thin-
    // geometry regression guard (#1720 DoD); the FRAME stays on forward-scatter
    // DETACHED, which deforms face SHAPES instead of moving cells (the
    // wireframe also showcases that technique's crisp thin detail). Spheres
    // appear on all three paths for a side-by-side of the techniques.
    constexpr OrbitShape kOrbitShapes[kOrbitCount]{
        OrbitShape::SPHERE,
        OrbitShape::FRAME,
        OrbitShape::SPHERE,
        OrbitShape::OCTAHEDRON,
        OrbitShape::CROSS,
        OrbitShape::OCTAHEDRON,
        OrbitShape::CUBE,
        OrbitShape::FRAME,
        OrbitShape::PYRAMID,
        OrbitShape::PYRAMID,
        OrbitShape::SPHERE,
        OrbitShape::CUBE,
    };
    constexpr RotationMode kOrbitModes[kOrbitCount]{
        RotationMode::GRID,
        RotationMode::DETACHED,
        RotationMode::DETACHED_REVOXELIZE,
        RotationMode::DETACHED, // octahedron
        RotationMode::GRID,     // cross — the standing GRID thin-geometry guard (#1720)
        RotationMode::DETACHED_REVOXELIZE,
        RotationMode::GRID,
        RotationMode::DETACHED,
        RotationMode::DETACHED_REVOXELIZE,
        RotationMode::GRID,
        RotationMode::DETACHED,
        RotationMode::DETACHED_REVOXELIZE,
    };
    constexpr vec3 kOrbitAxes[]{
        {0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {1.0f, 1.0f, 1.0f},
    };
    constexpr Color kOrbitColors[kOrbitCount]{
        {240, 80, 80, 255},
        {245, 150, 60, 255},
        {240, 215, 70, 255},
        {130, 220, 70, 255},
        {70, 210, 130, 255},
        {70, 200, 220, 255},
        {80, 130, 235, 255},
        {150, 90, 235, 255},
        {220, 80, 220, 255},
        {235, 90, 160, 255},
        {120, 235, 200, 255},
        {200, 200, 210, 255},
    };
    // Per-shape base extent in voxels — a mix of large and small orbiters, with
    // a large + a small in every rotation mode so the size range shows on the
    // world canvas and both detached paths.
    constexpr int kOrbitExtents[kOrbitCount]{
        16,
        14,
        13,
        10,
        16,
        15,
        12,
        14,
        16,
        11,
        13,
        12,
    };
    const int orbitCount = groupEnabled(kGroupOrbit) ? kOrbitCount : 0;
    for (int i = 0; i < orbitCount; ++i) {
        const float angle =
            IRMath::kTwoPi * static_cast<float>(i) / static_cast<float>(kOrbitCount);
        const vec3 worldPos{
            kOrbitRadius * IRMath::cos(angle),
            kOrbitRadius * IRMath::sin(angle),
            0.0f
        };
        const vec3 axis = kOrbitAxes[i % 4];
        // Off-cardinal seed pose so even shot 0 reads as a true-3D solid; the
        // per-entity rate de-syncs neighbours across the capture window.
        const vec4 initialRotation =
            IRMath::quatAxisAngle(IRMath::normalize(axis), IRMath::kQuarterPi);
        const float spinRate = g_settings.noSpin_ ? 0.0f
                                                  : kDetachedSpinBaseRadPerFrame *
                                                        (1.0f + 0.25f * static_cast<float>(i % 4));
        spawnOrbitShape(
            i,
            worldPos,
            kOrbitShapes[i],
            kOrbitModes[i],
            kOrbitExtents[i],
            axis,
            spinRate,
            initialRotation,
            kOrbitColors[i]
        );
    }

    // Comparison region (#1374): one entity per rotation technique in a labeled
    // screen-horizontal row, all driven by the shared camera Z-yaw. No
    // pixel-parity is asserted between them — GRID re-voxelize aliasing and the
    // detached SO(3) bake are intentionally different visuals. OPT-IN via
    // `--only compare`; the per-frame label tick (initSystems) draws each name.
    if (!g_settings.soloRevox_ && compareGroupRequested()) {
        // Cell 0 — GRID-attached spin: self-spins about Z; REBUILD_GRID_VOXELS
        // re-rasterizes its rotated cells into the shared world pool each frame.
        IREntity::createEntity(
            C_LocalTransform{compareCellWorld(0)},
            C_RotationMode{RotationMode::GRID},
            C_AutoSpin{vec3(0.0f, 0.0f, 1.0f), g_settings.noSpin_ ? 0.0f : kGridSpinRadPerFrame},
            C_VoxelSetNew{kCompareCubeSize, kCompareColors[0], true}
        );
        // Cell 1 — world-Z-yaw GRID: static voxels (no C_AutoSpin); its only
        // motion is the camera Z-yaw via the main canvas per-axis world deform.
        IREntity::createEntity(
            C_LocalTransform{compareCellWorld(1)},
            C_RotationMode{RotationMode::GRID},
            C_VoxelSetNew{kCompareCubeSize, kCompareColors[1], true}
        );
        // Cell 2 — DETACHED SO(3): private-canvas re-voxelize bake, self-spinning
        // about an off-cardinal axis (the sole shipped detached SO(3) renderer).
        spawnDetachedVoxelObject(
            100,
            compareCellWorld(2),
            IRMath::normalize(vec3(1.0f, 0.6f, 0.4f)),
            g_settings.noSpin_ ? 0.0f : kReVoxSpinPerFrame,
            kCompareColors[2]
        );
    }

    IR_LOG_INFO(
        "canvas_stress: main grid {}x{} ({} cubes), {} GRID-spin cubes, {} detached "
        "canvases, 2 detached re-voxelize solids (L + cube), {} orbit-ring shapes",
        n,
        n,
        n * n,
        gridSpinCount,
        detached,
        orbitCount
    );
}

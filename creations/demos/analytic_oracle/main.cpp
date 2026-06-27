// analytic_oracle — a deterministic render-validation oracle scene (epic #1766
// T-4, issue #1770).
//
// The render-regression net (render-verify) pixel-diffs each shot against a
// committed per-backend reference PNG. That breaks down exactly where the live
// render bugs live: at zoom, CPU<->GPU float divergence drifts the match% run
// to run, so the magnified shots are *excluded* from the gate (canvas_stress's
// revoxelize_solids_zoom is the canonical example). This demo closes that hole
// with an *analytic oracle*: a scene whose expected shadow occupancy is
// computable from first principles, gated by a structural metric
// (render-shadow-metric.py) against that computed expectation rather than a
// jittery captured reference. The structural gate is backend-agnostic and
// resolution-independent, so one threshold is a shared oracle across the
// macos-debug (Metal) and linux-debug (OpenGL) backends — no per-backend
// reference set, no pixel-diff, deterministic at any zoom.
//
// Scene (entity positions -> expected shadow, the documented oracle):
//   * A single axis-aligned BOX caster of edge kBoxEdge, centred at the world
//     origin, resting on a flat floor (its bottom face flush with the floor's
//     top surface). One convex caster on a flat ground under a single
//     directional sun casts EXACTLY ONE connected, hole-free shadow: the box's
//     own self-shadowed faces and its cast shadow on the floor are adjacent in
//     the iso projection and merge into one magenta blob.
//   * A fixed sun (kSunDirection) high enough that the shadow lands beside the
//     box and stays in frame at zoom.
//
// Rendered in the SHADOW debug overlay (black = lit, magenta = shadowed) so
// occupancy is encoded directly as colour and classifies exactly. The
// analytic expectation the manifest gates against:
//   * components == 1   (one caster -> one connected shadow; a fragmentation /
//                        swiss-cheese regression shatters it into many)
//   * largest_frac ~= 1 (that one component dominates; a vanished shadow reads
//                        0, a fragmented one reads low)
// See creations/demos/analytic_oracle/test/references/manifest.json for the
// gated thresholds and the full derivation.
//
// Deterministic by construction: no spin, no randomness, fixed camera. The
// scene is intentionally minimal — one box, one floor, one sun — so the
// expected metric is trivially analytic, unlike the busy multi-shape scenes
// (shape_debug, canvas_stress) whose shadows fragment into dozens of blobs.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/render/camera.hpp>

// COMPONENTS
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>

// SYSTEMS — the standard render pipeline (mirrors shape_debug's), including the
// sun-shadow bake/compute stages the oracle depends on.
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/render/systems/system_lod_update.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_build_light_occlusion_grid.hpp>
#include <irreden/render/systems/system_compute_voxel_ao.hpp>
#include <irreden/render/systems/system_resolve_per_axis_screen_depth.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>
#include <irreden/render/systems/system_compute_sun_shadow.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_lighting_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/camera_controls.hpp>

namespace {

// +Z is the downward height axis in this iso convention. The floor's top
// surface sits at kFloorZ - kFloorHalfThickness; the box (edge kBoxEdge,
// centred at the origin) reaches down to +kBoxEdge/2, so picking
// kFloorZ = kBoxEdge/2 + kFloorHalfThickness rests the box flush on the floor.
constexpr float kBoxEdge = 18.0f;
constexpr float kFloorHalfThickness = 1.0f;
constexpr float kFloorZ = kBoxEdge * 0.5f + kFloorHalfThickness;
constexpr float kFloorSpan = 96.0f;

// Sun above the ground (dir.z < 0 per the engine convention), tilted in +x/+y
// so the box throws one compact shadow that stays in frame at zoom.
constexpr vec3 kSunDirection = vec3(0.30f, 0.52f, -0.74f);

// One zoomed shot of the box + its cast shadow. zoom 2.5 puts it well into the
// magnified regime where pixel-diff is excluded; the structural gate holds
// regardless. Camera centred, no yaw — fully deterministic.
constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {2.5f, vec2(0, 0), 0.0f, "oracle_box_zoom"},
};

int g_autoWarmupFrames = 0; // 0 = --auto-screenshot not requested

} // namespace

void initSystems();
void initEntities();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: analytic_oracle");
    IREngine::init(argc, argv);
    g_autoWarmupFrames = IREngine::args().autoScreenshotWarmupFrames();
    initSystems();
    initEntities();
    // SHADOW debug overlay (black = lit, magenta = shadowed). This demo exists
    // solely to gate shadow occupancy against an analytic expectation, so it
    // renders in SHADOW mode unconditionally — not a per-run flag.
    IRRender::setDebugOverlay(IRRender::DebugOverlayMode::SHADOW);
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

    std::list<IRSystem::SystemId> renderPipeline = IRPrefab::Camera::standardControlSystems();
    renderPipeline.insert(
        renderPipeline.end(),
        {
            IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
            IRSystem::createSystem<IRSystem::BUILD_LIGHT_OCCLUSION_GRID>(),
            // Owns the shared SingleVoxelFrameData UBO (binding 7) that the AO /
            // shadow / lighting stages below read; a no-op for the geometry
            // (no voxel sets in this scene) but its frame-data buffer is not.
            IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
            IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::COMPUTE_VOXEL_AO>(),
            IRSystem::createSystem<IRSystem::RESOLVE_PER_AXIS_SCREEN_DEPTH>(),
            IRSystem::createSystem<IRSystem::BAKE_SUN_SHADOW_MAP>(),
            IRSystem::createSystem<IRSystem::COMPUTE_SUN_SHADOW>(),
            IRSystem::createSystem<IRSystem::COMPUTE_LIGHT_VOLUME>(),
            IRSystem::createSystem<IRSystem::LIGHTING_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
        }
    );

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

void initEntities() {
    // Flat floor (shadow receiver). C_LightBlocker{false, false, 0} lets light
    // pass through it (it doesn't cast) but it still receives cast shadows.
    IR_LOG_INFO("--- Floor ---");
    EntityId floorEntity = IREntity::createEntity(
        C_LocalTransform{vec3(0.0f, 0.0f, kFloorZ)},
        C_ShapeDescriptor{
            IRRender::ShapeType::BOX,
            vec4(kFloorSpan, kFloorSpan, kFloorHalfThickness * 2.0f, 0.0f),
            Color{150, 150, 160, 255}
        }
    );
    IREntity::setComponent(floorEntity, C_LightBlocker{false, false, 0.0f});

    // The single axis-aligned box caster, centred at the origin and resting on
    // the floor. A default shape (no C_LightBlocker override) casts shadows.
    IR_LOG_INFO("--- Oracle box ---");
    IREntity::createEntity(
        C_LocalTransform{vec3(0.0f, 0.0f, 0.0f)},
        C_ShapeDescriptor{
            IRRender::ShapeType::BOX,
            vec4(kBoxEdge, kBoxEdge, kBoxEdge, 0.0f),
            Color{120, 200, 220, 255}
        }
    );

    // The shape canvas prefab doesn't carry the AO / sun-shadow / light-volume
    // components, so the lighting systems' archetype filter wouldn't otherwise
    // match the main canvas and they'd silently skip it.
    EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    const ivec2 canvasSize = IREntity::getComponent<C_TriangleCanvasTextures>(mainCanvas).size_;
    IREntity::setComponent(mainCanvas, C_CanvasAOTexture{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasSunShadow{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasLightVolume{});
    IREntity::setComponent(mainCanvas, C_TrixelCanvasRenderBehavior{});

    IRRender::setSunDirection(kSunDirection);
    IRRender::setSunShadowsEnabled(true);
}

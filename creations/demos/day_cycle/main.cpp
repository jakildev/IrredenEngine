// day_cycle — visual proof-out of the sim-clock substrate (engine #200).
//
// This is the runnable counterpart to the 23 gtests in `test/time/`: it wires
// the three sim-clock systems into a real UPDATE pipeline alongside the render
// pipeline, creates a generic `C_Cycle("day")`, and drives the global sun
// direction / intensity / sky tint each frame off `IRSim::cycleFraction("day")`
// — the "load-bearing continuous primitive for time-driven values" the service
// header advertises. Watch the shadows sweep and the sky warm at dawn/dusk.
//
// What it demonstrates that the unit tests cannot:
//   1. Real pipeline registration + ordering (SIM_CLOCK_ADVANCE first, then
//      CYCLE_BOUNDARY_DETECT / TIMER_FIRE) inside a live UPDATE+RENDER loop —
//      the one piece the tests stub by hand-registering a bare SystemManager.
//   2. cycleFraction → sun angle, end to end through the lighting passes.
//   3. The discrete boundary + timer events firing alongside the continuous
//      query (logged on the tick they cross).
//
// Follow-ups (tracked on the issue, intentionally out of scope here):
//   - Key-scrubbing pause / timeScale (needs enum-typed IRCommand entries).
//   - Deterministic sim-seek so render-verify can pin per-phase references; a
//      wall-clock fixed-step loop makes a time-varying scene non-deterministic
//      by frame count, so no reference images are committed (cf. the
//      wall-clock lighting_sun_orbit demo, which likewise commits none).

#include <irreden/ir_engine.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>

#include <irreden/render/camera.hpp>
#include <irreden/render/camera_controls.hpp>

// Sim-clock substrate (#200).
#include <irreden/common/sim_clock.hpp>
#include <irreden/update/systems/system_cycle_boundary_detect.hpp>
#include <irreden/update/systems/system_sim_clock_advance.hpp>
#include <irreden/update/systems/system_timer_fire.hpp>

// Scene components.
#include <irreden/common/command_suite_capture.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

// Scene systems.
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>
#include <irreden/render/systems/system_build_light_occlusion_grid.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_compute_sun_shadow.hpp>
#include <irreden/render/systems/system_compute_voxel_ao.hpp>
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
using IRMath::vec2;
using IRMath::vec3;

namespace {

// One in-game day spans 1200 sim ticks; the cycle is phase-offset by half a
// period so the sim starts at local noon (a bright frame for the warmup
// screenshot) and animates toward dusk from there.
constexpr std::uint64_t kDayPeriodTicks = 1200;
constexpr std::uint64_t kNoonPhaseOffset = kDayPeriodTicks / 2;
// A recurring marker every quarter-day, just to exercise TIMER_FIRE in a real
// pipeline next to the cycle.
constexpr std::uint64_t kQuarterDayTicks = kDayPeriodTicks / 4;

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {2.0f, vec2(0, 0), 0.0f, "day_noon_zoom2"},
    {4.0f, vec2(0, 0), 0.0f, "day_noon_zoom4"},
};

int g_autoWarmupFrames = 0;

// Reads the continuous day fraction and steers the global sun + sky. This is
// the demo's whole point: every visible lighting cue below is a pure function
// of IRSim::cycleFraction("day"), which freezes on pause and scales with
// timeScale because it rides the sim clock rather than wall-clock time.
void driveSunFromDayCycle() {
    const float t = IRSim::cycleFraction("day"); // [0, 1)
    const float dayAngle = t * IRMath::kTwoPi;

    // sunHeight: +1 at noon (t=0.5), -1 at midnight (t=0/1). Azimuth sweeps a
    // full turn over the day so shadows rotate as well as lengthen.
    const float sunHeight = -IRMath::cos(dayAngle);
    const float horizontal = IRMath::sqrt(IRMath::max(0.0f, 1.0f - sunHeight * sunHeight));
    const vec3 sunDir = IRMath::normalize(vec3(
        horizontal * IRMath::cos(dayAngle),
        horizontal * IRMath::sin(dayAngle),
        -IRMath::max(sunHeight, 0.05f) // keep a slight downward bias at night
    ));
    IRRender::setSunDirection(sunDir);

    const float daylight = IRMath::clamp(sunHeight, 0.0f, 1.0f);
    IRRender::setSunIntensity(0.15f + 1.1f * daylight);
    IRRender::setSunAmbient(0.12f + 0.33f * daylight);

    // Warm the sky toward orange when the sun grazes the horizon (dawn/dusk),
    // cool blue at noon, and dim it toward night.
    const float warmth = IRMath::clamp(1.0f - IRMath::abs(sunHeight), 0.0f, 1.0f);
    const vec3 daySky(0.45f, 0.62f, 0.95f);
    const vec3 duskSky(0.95f, 0.55f, 0.30f);
    const vec3 sky = daySky * (1.0f - warmth) + duskSky * warmth;
    IRRender::setSkyColor(sky * (0.2f + 0.8f * daylight));

    // Discrete events ride alongside the continuous query, polled the tick the
    // detector raised them (the events-as-components model).
    if (IRSim::cycleBoundaryCrossed("day")) {
        IR_LOG_INFO("[day_cycle] dawn of day {}", IRSim::cycleNumber("day"));
    }
    if (IRSim::timerFired("quarter")) {
        IR_LOG_INFO("[day_cycle] quarter-day marker (sim tick {})", IRSim::tick());
    }
}

} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: day_cycle");
    IREngine::init(argc, argv);
    g_autoWarmupFrames = IREngine::args().autoScreenshotWarmupFrames();
    initSystems();
    initCommands();
    initEntities();
    IREngine::gameLoop();
    return 0;
}

void initSystems() {
    // Sim-clock systems lead the UPDATE pipeline so CYCLE_BOUNDARY_DETECT /
    // TIMER_FIRE observe the freshly advanced tick the same frame; the voxel /
    // transform systems follow.
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::SIM_CLOCK_ADVANCE>(),
         IRSystem::createSystem<IRSystem::CYCLE_BOUNDARY_DETECT>(),
         IRSystem::createSystem<IRSystem::TIMER_FIRE>(),
         IRSystem::createSystem<IRSystem::LOD_UPDATE>(),
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
            IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
            IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::COMPUTE_VOXEL_AO>(),
            IRSystem::createSystem<IRSystem::BAKE_SUN_SHADOW_MAP>(),
            IRSystem::createSystem<IRSystem::COMPUTE_SUN_SHADOW>(),
            IRSystem::createSystem<IRSystem::COMPUTE_LIGHT_VOLUME>(),
            IRSystem::createSystem<IRSystem::LIGHTING_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
        }
    );

    // Per-frame hook (once-per-frame beginTick form, cf. the lighting demos'
    // animated sun): runs first so the lighting passes pick up the new sun.
    IRSystem::SystemId sunTickId = IRSystem::createSystem<C_Name>(
        "DayCycleSunTick",
        [](C_Name &) {},
        []() { driveSunFromDayCycle(); }
    );
    renderPipeline.push_front(sunTickId);

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

void initEntities() {
    // Canvas lighting attachments (AO + sun shadow + light volume) so the
    // sweeping sun actually casts and shades.
    IREntity::EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    const IRMath::ivec2 canvasSize =
        IREntity::getComponent<C_TriangleCanvasTextures>(mainCanvas).size_;
    IREntity::setComponent(mainCanvas, C_CanvasAOTexture{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasSunShadow{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasLightVolume{});
    IREntity::setComponent(mainCanvas, C_TrixelCanvasRenderBehavior{});

    IRRender::setSunShadowsEnabled(true);
    IRRender::setAOEnabled(true);

    // A flat floor for shadows to fall on, plus a few solid cubes to cast them.
    IREntity::createEntity(
        C_LocalTransform{vec3{0.0f, 0.0f, 6.0f}},
        C_VoxelSetNew{IRMath::ivec3{48, 48, 2}, IRMath::Color{140, 140, 150, 255}, true}
    );
    const IRMath::Color cubeColors[] = {
        IRMath::Color{200, 120, 90, 255},
        IRMath::Color{110, 190, 210, 255},
        IRMath::Color{210, 200, 110, 255},
    };
    for (int i = 0; i < 3; ++i) {
        IREntity::createEntity(
            C_LocalTransform{
                vec3{static_cast<float>(i * 12 - 12), static_cast<float>(i * 8 - 8), -4.0f}
            },
            C_VoxelSetNew{IRMath::ivec3{6, 6, 12}, cubeColors[i], true}
        );
    }

    // Materialize the sim clock singleton, then the generic day cycle + a
    // quarter-day recurring timer. cycleFraction("day") starts at 0.5 (noon)
    // thanks to the half-period phase offset.
    IRSim::clock();
    IRSim::createCycle("day", kDayPeriodTicks, kNoonPhaseOffset);
    IRSim::createTimer("quarter", IRSim::tick() + kQuarterDayTicks, kQuarterDayTicks);
}

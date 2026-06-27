// scene_reset — render-stack idempotency proof for IREntity::resetGameplay (#1857).
//
// The render-stack half of #1814's acceptance that the headless unit test
// (no RenderManager / no C_VoxelPool) cannot exercise. It drives the
// scene-transition primitive over a LIVE RenderManager + real voxel pool:
// build scene -> register UPDATE pipeline -> reset, looped N>=10 cycles, and
// asserts the world returns to its preserve-set baseline each cycle with no
// entity or voxel-pool leak. A final scene then renders under --auto-screenshot
// to prove the render context survived the resets.
//
// Mirrors chunk_streaming_smoke: the cycles run BEFORE IREngine::gameLoop()
// against the real pool the unit test could not construct, then the loop
// renders the post-reset scene (auto-screenshot closes the window).
//
// VoxelPool count is a HIGH-WATER MARK, not live occupancy: deallocateVoxels
// pushes freed spans to an exact-size free-list and never lowers the waterline,
// and allocateVoxels reuses an exact-size span before extending it. So after a
// warm-up cycle seeds the free-list, identical-footprint scenes leave the
// waterline exactly constant — the honest signal is "no growth across cycles,"
// NOT "==0 after reset." A real leak (missing onDestroy deallocate) would grow
// it by one scene's voxel count every cycle.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_profile.hpp>

// COMPONENTS
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

// SYSTEMS
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/render/systems/system_lod_update.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/voxel/systems/system_voxel_squash_stretch.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_build_light_occlusion_grid.hpp>
#include <irreden/render/systems/system_compute_voxel_ao.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>
#include <irreden/render/systems/system_compute_sun_shadow.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_lighting_to_trixel.hpp>
#include <irreden/render/systems/system_fog_to_trixel.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_sprites_to_screen.hpp>
#include <irreden/render/camera_controls.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>

// COMMAND SUITES
#include <irreden/common/command_suite_capture.hpp>

#include <list>

namespace {

constexpr int kCycles = 12;                // N >= 10 reset/rebuild cycles
constexpr int kSetsPerScene = 3;           // gameplay entities per scene
constexpr IRMath::ivec3 kSetDims{5, 5, 5}; // identical A/B footprint -> flat waterline
constexpr int kSetSpacing = 6;             // world-unit gap between sets (1 unit clear at 5^3)

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {2.0f, vec2(0, 0), 0.0f, "zoom2_scene_reset"},
    {4.0f, vec2(0, 0), 0.0f, "zoom4_scene_reset"},
};

enum class SceneId { SCENE_A, SCENE_B };

int g_autoWarmupFrames = 0;
int g_failCount = 0;

// Cached once; the canvas + camera are C_Persistent so they survive every reset.
IREntity::EntityId g_mainCanvas = IREntity::kNullEntity;
// Preserve-set baselines, captured after a warm-up cycle (see captureBaselines).
IREntity::EntityId g_baselineEntities = 0;
int g_baselineWaterline = 0;

// Systems are created ONCE and their SystemIds reused across pipeline swaps.
// Systems live in the SystemManager's own arrays (not the EntityManager), so
// they persist across resetGameplay and never count toward getLiveEntityCount;
// re-calling createSystem each cycle would slowly grow those arrays for nothing.
struct SceneSystems {
    IRSystem::SystemId lodUpdate_ = 0;
    IRSystem::SystemId propagateTransform_ = 0;
    IRSystem::SystemId updateVoxelSetChildren_ = 0;
    IRSystem::SystemId rebuildGridVoxels_ = 0;
    IRSystem::SystemId squashStretch_ = 0;
};
SceneSystems g_systems;

} // namespace

void setupSystemsOnce();
void initCommands();
int buildScene(SceneId scene);
void registerSceneUpdatePipeline(SceneId scene);
int poolLiveVoxelCount();
void captureBaselines();
void runResetCycles();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: scene_reset");
    IREngine::init(argc, argv);
    g_autoWarmupFrames = IREngine::args().autoScreenshotWarmupFrames();

    setupSystemsOnce();
    initCommands();

    // The reset/rebuild idempotency cycles run BEFORE gameLoop() against the
    // live RenderManager + real voxel pool (the chunk_streaming_smoke
    // precedent) — exactly the real-pool allocate/free bookkeeping the #1814
    // headless unit test could not construct.
    captureBaselines();
    runResetCycles();

    // Final scene for the render-survival proof: build it and register its
    // UPDATE pipeline (so UPDATE_VOXEL_SET_CHILDREN uploads voxel positions),
    // then run the loop — auto-screenshot renders the post-reset scene
    // (non-blank) and closes the window.
    buildScene(SceneId::SCENE_A);
    registerSceneUpdatePipeline(SceneId::SCENE_A);

    if (g_failCount == 0) {
        IR_LOG_INFO("[scene_reset] All {} reset/rebuild cycles PASSED", kCycles);
    } else {
        IR_LOG_ERROR("[scene_reset] {} assertion(s) FAILED across {} cycles", g_failCount, kCycles);
    }

    IREngine::gameLoop();
    return g_failCount > 0 ? 1 : 0;
}

// Create every system both scenes use exactly once, and register the INPUT +
// RENDER pipelines. The RENDER pipeline is registered ONCE and never cleared —
// clearing it mid-scene would stop rendering. Only UPDATE is swapped per scene.
void setupSystemsOnce() {
    g_systems.lodUpdate_ = IRSystem::createSystem<IRSystem::LOD_UPDATE>();
    g_systems.propagateTransform_ = IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>();
    g_systems.updateVoxelSetChildren_ =
        IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>();
    g_systems.rebuildGridVoxels_ = IRSystem::createSystem<IRSystem::REBUILD_GRID_VOXELS>();
    g_systems.squashStretch_ = IRSystem::createSystem<IRSystem::VOXEL_SQUASH_STRETCH>();

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
            IRSystem::createSystem<IRSystem::FOG_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
            IRSystem::createSystem<IRSystem::SPRITE_TO_SCREEN>(),
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

void initCommands() {
    IRPrefab::Camera::registerStandardKeyboardCommands();
    IRCommand::registerCaptureCommands();
}

// Build one scene's gameplay entities: kSetsPerScene voxel sets of the SAME
// dimensions (so the pool waterline stays flat once warmed) but varied color +
// position per scene. Returns the number of gameplay entities created.
int buildScene(SceneId scene) {
    const IRMath::Color color = (scene == SceneId::SCENE_A)
                                    ? IRMath::Color{220, 90, 70, 255}   // scene A: warm red
                                    : IRMath::Color{70, 130, 220, 255}; // scene B: cool blue
    const float sceneNudge = (scene == SceneId::SCENE_A) ? 0.0f : 1.0f;
    for (int i = 0; i < kSetsPerScene; ++i) {
        const IRMath::vec3 pos{static_cast<float>((i - 1) * kSetSpacing) + sceneNudge, 0.0f, 0.0f};
        IREntity::createEntity(C_LocalTransform{pos}, C_VoxelSetNew{kSetDims, color, true});
    }
    return kSetsPerScene;
}

// Scene A and B register DIFFERENT UPDATE pipelines so the
// clearPipeline + registerPipeline swap is genuinely exercised: A animates with
// VOXEL_SQUASH_STRETCH, B omits it. REBUILD_GRID_VOXELS runs after
// UPDATE_VOXEL_SET_CHILDREN per the voxel-pipeline ordering contract.
void registerSceneUpdatePipeline(SceneId scene) {
    std::list<IRSystem::SystemId> update = {
        g_systems.lodUpdate_,
        g_systems.propagateTransform_,
    };
    if (scene == SceneId::SCENE_A) {
        update.push_back(g_systems.squashStretch_);
    }
    update.push_back(g_systems.updateVoxelSetChildren_);
    update.push_back(g_systems.rebuildGridVoxels_);
    IRSystem::registerPipeline(IRTime::Events::UPDATE, update);
}

// Live voxel-pool waterline of the persistent main canvas, or -1 if the pool is
// gone (which the per-cycle survival assertion catches).
int poolLiveVoxelCount() {
    auto pool = IREntity::getComponentOptional<C_VoxelPool>(g_mainCanvas);
    return pool.has_value() ? (*pool)->getLiveVoxelCount() : -1;
}

void captureBaselines() {
    g_mainCanvas = IRRender::getCanvas("main");

    // Clear any startup gameplay entities so the baseline reflects only the
    // preserve set (singletons + the C_Persistent render context + the
    // component-type backing entities).
    IREntity::resetGameplay();

    // Warm-up cycle: building a scene lazily registers any not-yet-seen
    // component types (each backed by a preserved entity) and seeds the pool
    // free-list with exact-size spans. Capture the waterline while the scene is
    // live, then reset so the entity baseline counts only the preserve set.
    // After this point no new component types register, so the per-cycle counts
    // are exact.
    buildScene(SceneId::SCENE_A);
    g_baselineWaterline = poolLiveVoxelCount();
    IREntity::resetGameplay();
    g_baselineEntities = IREntity::getLiveEntityCount();

    IR_LOG_INFO(
        "[scene_reset] baselines: preserveEntities={} voxelWaterline={}",
        g_baselineEntities,
        g_baselineWaterline
    );
}

void runResetCycles() {
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        const SceneId scene = (cycle % 2 == 0) ? SceneId::SCENE_A : SceneId::SCENE_B;

        const int created = buildScene(scene);
        registerSceneUpdatePipeline(scene);
        // Sanity-check the freshly swapped pipeline for cross-system conflicts —
        // the scene-machine sanctioned sequence (clearPipeline + registerPipeline
        // + validate).
        IRSystem::validateAllPipelineGroups();

        const int liveVox = poolLiveVoxelCount();
        const IREntity::EntityId liveEnt = IREntity::getLiveEntityCount();

        // (a) build happened: gameplay entities added on top of the preserve baseline.
        if (liveEnt != g_baselineEntities + static_cast<IREntity::EntityId>(created)) {
            IR_LOG_ERROR(
                "[scene_reset] FAIL cycle {}: live entities {} != baseline {} + scene {}",
                cycle,
                liveEnt,
                g_baselineEntities,
                created
            );
            ++g_failCount;
        }

        // (b) no pool growth: freed spans recycled, waterline flat across cycles.
        if (liveVox != g_baselineWaterline) {
            IR_LOG_ERROR(
                "[scene_reset] FAIL cycle {}: voxel waterline {} != baseline {} (pool grew / "
                "leaked)",
                cycle,
                liveVox,
                g_baselineWaterline
            );
            ++g_failCount;
        }

        // (c) render context survived the prior reset: canvas + its pool persist.
        if (!IREntity::entityExists(g_mainCanvas) ||
            !IREntity::getComponentOptional<C_VoxelPool>(g_mainCanvas).has_value()) {
            IR_LOG_ERROR(
                "[scene_reset] FAIL cycle {}: main canvas / pool did not survive the reset",
                cycle
            );
            ++g_failCount;
        }

        // Teardown — the scene-machine sequence at a frame boundary:
        // clear UPDATE, then tear down gameplay entities.
        IRSystem::clearPipeline(IRTime::Events::UPDATE);
        IREntity::resetGameplay();

        // (d) gameplay torn down to the preserve baseline (no entity leak).
        const IREntity::EntityId postReset = IREntity::getLiveEntityCount();
        if (postReset != g_baselineEntities) {
            IR_LOG_ERROR(
                "[scene_reset] FAIL cycle {}: post-reset entities {} != baseline {} (gameplay "
                "leak)",
                cycle,
                postReset,
                g_baselineEntities
            );
            ++g_failCount;
        }

        IR_LOG_INFO(
            "[scene_reset] cycle {} ({}): liveEnt={} liveVox={} postReset={}",
            cycle,
            scene == SceneId::SCENE_A ? "A" : "B",
            liveEnt,
            liveVox,
            postReset
        );
    }
}

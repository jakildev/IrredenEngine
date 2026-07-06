// persist_roundtrip — GPU-resident state regeneration on load (persist P6 /
// W-10, #2217, epic #667).
//
// The render-stack half of W-10's acceptance that the headless serializer unit
// test (`test/world/voxel_set_serialize_test.cpp`, no RenderManager / no pool)
// cannot exercise. It drives the full save -> reset -> load -> seed round-trip
// over a LIVE RenderManager + real voxel pool:
//
//   1. build a lit voxel scene (multiple C_VoxelSetNew, lighting + AO +
//      sun-shadow active in the RENDER pipeline),
//   2. IRWorld::saveWorld — the sets serialize their canonical voxel data,
//   3. IREntity::resetGameplay — gameplay entities destroyed, each set's
//      onDestroy returns its pool span (pool back to baseline); the
//      C_Persistent canvas / camera / C_VoxelPool survive,
//   4. IRWorld::loadWorld — sets come back in STAGED mode (numVoxels_ == 0,
//      pendingVoxels_ populated), invisible to the pool pipeline,
//   5. gameLoop — the SEED_STAGED_VOXELS UPDATE system runs
//      C_VoxelSetNew::attachToCanvas over every staged set, moving it into a
//      live pool span; the scene renders lit and --auto-screenshot captures it.
//
// The round-trip runs BEFORE gameLoop (the scene_reset / chunk_streaming_smoke
// precedent) so the machine-checkable contract is asserted around it:
//   - post-reset the pool waterline returns to its pre-scene baseline,
//   - post-load every set is STAGED with recordCount == its saved voxel count,
//   - post-gameLoop (after the seed system ran) every set is pool-resident
//     (numVoxels_ > 0, pendingVoxels_ empty) and the waterline is idempotent
//     versus the original live scene — no pool growth across the round-trip.
// The --auto-screenshot frame is the visual "renders lit / non-blank" proof;
// committed-reference pixel-equivalence via render-verify is a follow-up.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_constants.hpp>

// COMPONENTS
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

// PERSIST SURFACE (no ir_world umbrella yet; the P7 Lua/convenience surface
// layers on later — for now a snapshot creation registers components directly,
// mirroring test/world). save_component_inventory.hpp supplies the engine
// components' SaveTrait decisions; voxel_set_serialize.hpp the C_VoxelSetNew
// SaveSerialize specialization.
#include <irreden/world/world_snapshot.hpp>
#include <irreden/world/save_registry.hpp>
#include <irreden/world/save_component_inventory.hpp>
#include <irreden/voxel/voxel_set_serialize.hpp>

// SYSTEMS
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/render/systems/system_lod_update.hpp>
#include <irreden/voxel/systems/system_seed_staged_voxels.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
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
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_sprites_to_screen.hpp>
#include <irreden/render/camera_controls.hpp>

// COMMAND SUITES
#include <irreden/common/command_suite_capture.hpp>

#include <list>
#include <string>
#include <vector>

using namespace IRComponents;

namespace {

constexpr int kSetsPerScene = 3;
constexpr IRMath::ivec3 kSetDims{6, 6, 6}; // integer-origin sets -> exact boundsMin round-trip
constexpr int kSetSpacing = 8;
const std::string kSnapshotPath = "persist_roundtrip_snapshot.irws";

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {2.0f, vec2(0, 0), 0.0f, "zoom2_persist_roundtrip"},
    {4.0f, vec2(0, 0), 0.0f, "zoom4_persist_roundtrip"},
};

int g_autoWarmupFrames = 0;
int g_failCount = 0;

IREntity::EntityId g_mainCanvas = IREntity::kNullEntity;
int g_liveSceneWaterline = 0; // pool waterline with the original scene live
int g_baselineWaterline = 0;  // pool waterline after resetGameplay (no scene)
IREntity::EntityId g_baselineEntities = 0;

// Every set is the same footprint, so recordCount is a shared constant.
constexpr std::size_t kSetVoxelCount =
    static_cast<std::size_t>(kSetDims.x) * kSetDims.y * kSetDims.z;

std::vector<IREntity::EntityId> g_setEntities;

void checkTrue(bool cond, const std::string &label) {
    if (!cond) {
        IR_LOG_ERROR("[persist_roundtrip] FAIL: {}", label);
        ++g_failCount;
    } else {
        IR_LOG_INFO("[persist_roundtrip] ok: {}", label);
    }
}

} // namespace

void setupSystemsOnce();
IRWorld::SaveRegistry buildRegistry();
void buildScene();
int poolLiveVoxelCount();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: persist_roundtrip");
    IREngine::init(argc, argv);
    g_autoWarmupFrames = IREngine::args().autoScreenshotWarmupFrames();

    setupSystemsOnce();
    IRPrefab::Camera::registerStandardKeyboardCommands();
    IRCommand::registerCaptureCommands();

    g_mainCanvas = IRRender::getCanvas("main");

    // Baseline: pool waterline with no gameplay scene (post a warm-up build so
    // the free-list is seeded — waterline is a high-water mark, per scene_reset).
    IREntity::resetGameplay();
    buildScene();
    g_liveSceneWaterline = poolLiveVoxelCount();
    IREntity::resetGameplay();
    g_baselineWaterline = poolLiveVoxelCount();
    g_baselineEntities = IREntity::getLiveEntityCount();

    const IRWorld::SaveRegistry registry = buildRegistry();

    // (1) Build the scene to save.
    buildScene();
    const IREntity::EntityId liveWithScene = IREntity::getLiveEntityCount();
    checkTrue(
        liveWithScene == g_baselineEntities + static_cast<IREntity::EntityId>(kSetsPerScene),
        "scene built on top of preserve baseline"
    );

    // (2) Save.
    const IRAsset::BinaryStatus saveStatus = IRWorld::saveWorld(registry, kSnapshotPath);
    checkTrue(saveStatus.ok(), "saveWorld succeeded");

    // (3) Reset — gameplay torn down, render context preserved.
    IREntity::resetGameplay();
    checkTrue(
        IREntity::getLiveEntityCount() == g_baselineEntities,
        "post-reset entities back to preserve baseline"
    );
    checkTrue(
        poolLiveVoxelCount() == g_baselineWaterline,
        "post-reset pool waterline back to baseline"
    );
    checkTrue(
        IREntity::entityExists(g_mainCanvas) &&
            IREntity::getComponentOptional<C_VoxelPool>(g_mainCanvas).has_value(),
        "C_Persistent canvas + pool survived resetGameplay"
    );

    // (4) Load — sets return in STAGED mode.
    const IRWorld::LoadResult load = IRWorld::loadWorld(registry, kSnapshotPath);
    checkTrue(load.ok(), "loadWorld succeeded");
    checkTrue(
        load.entitiesRestored_ == static_cast<std::uint64_t>(kSetsPerScene),
        "loadWorld restored every gameplay entity"
    );

    for (const IREntity::EntityId id : g_setEntities) {
        const bool exists = IREntity::entityExists(id);
        checkTrue(exists, "restored set entity exists (id-stable)");
        if (!exists) {
            continue;
        }
        const C_VoxelSetNew &set = IREntity::getComponent<C_VoxelSetNew>(id);
        checkTrue(set.numVoxels_ == 0, "restored set is STAGED (numVoxels_ == 0)");
        checkTrue(set.recordCount() == kSetVoxelCount, "restored set recordCount == saved count");
    }

    // (5) Seed: drive one UPDATE tick so the registered SEED_STAGED_VOXELS
    // system runs C_VoxelSetNew::attachToCanvas over every staged set, moving it
    // into a live pool span. Running it here (not only inside gameLoop) lets the
    // post-seed contract be asserted before the render loop — gameLoop's
    // --auto-screenshot exits the process before it returns (the scene_reset
    // precedent), so every assertion and the PASS/FAIL summary must be emitted
    // first; a verifier greps the log for FAIL rather than trusting the exit code.
    IRSystem::executePipeline(IRTime::Events::UPDATE);

    for (const IREntity::EntityId id : g_setEntities) {
        if (!IREntity::entityExists(id)) {
            continue;
        }
        const C_VoxelSetNew &set = IREntity::getComponent<C_VoxelSetNew>(id);
        checkTrue(set.numVoxels_ > 0, "seeded set exited staged mode (pool-resident)");
        checkTrue(set.pendingVoxels_.empty(), "seeded set cleared its staging buffer");
        checkTrue(set.recordCount() == kSetVoxelCount, "seeded set recordCount == saved count");
    }
    checkTrue(
        poolLiveVoxelCount() == g_liveSceneWaterline,
        "post-seed pool waterline idempotent vs original live scene (no leak)"
    );

    if (g_failCount == 0) {
        IR_LOG_INFO("[persist_roundtrip] ALL round-trip assertions PASSED");
    } else {
        IR_LOG_ERROR("[persist_roundtrip] {} assertion(s) FAILED", g_failCount);
    }

    // (6) Render the seeded post-load scene lit; --auto-screenshot captures it.
    // gameLoop's UPDATE ticks re-run SEED_STAGED_VOXELS as a no-op (every set is
    // already pool-resident). It exits the process under --auto-screenshot, so
    // the return below is reached only in an interactive run.
    IREngine::gameLoop();
    return g_failCount > 0 ? 1 : 0;
}

// Create every system once; register INPUT + RENDER (once, never cleared) and
// the UPDATE pipeline. SEED_STAGED_VOXELS leads UPDATE so a freshly-loaded
// staged set attaches to the pool before UPDATE_VOXEL_SET_CHILDREN uploads it.
void setupSystemsOnce() {
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );

    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {
            IRSystem::createSystem<IRSystem::SEED_STAGED_VOXELS>(),
            IRSystem::createSystem<IRSystem::LOD_UPDATE>(),
            IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
            IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
        }
    );

    std::list<IRSystem::SystemId> renderPipeline = IRPrefab::Camera::standardControlSystems();
    renderPipeline.insert(
        renderPipeline.end(),
        {
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

// The snapshot registry: exactly the opted-in components a gameplay set entity
// carries — C_VoxelSetNew (its custom staged serializer) plus the transform
// pair createEntity attaches. Registering C_WorldTransform keeps the restored
// archetype whole so PROPAGATE_TRANSFORM re-derives each set's world position.
IRWorld::SaveRegistry buildRegistry() {
    IRWorld::SaveRegistry registry;
    registry.registerComponent<C_VoxelSetNew>();
    registry.registerComponent<C_LocalTransform>();
    registry.registerComponent<C_WorldTransform>();
    return registry;
}

// Build the gameplay scene: kSetsPerScene solid voxel sets in a row, distinct
// colors, integer local origins (non-centered) so boundsMin round-trips exactly.
void buildScene() {
    g_setEntities.clear();
    const IRMath::Color colors[kSetsPerScene] = {
        IRMath::Color{220, 90, 70, 255},  // warm red
        IRMath::Color{90, 200, 110, 255}, // green
        IRMath::Color{70, 130, 220, 255}, // cool blue
    };
    for (int i = 0; i < kSetsPerScene; ++i) {
        const IRMath::vec3 pos{static_cast<float>((i - 1) * kSetSpacing), 0.0f, 0.0f};
        const IREntity::EntityId id =
            IREntity::createEntity(C_LocalTransform{pos}, C_VoxelSetNew{kSetDims, colors[i]});
        g_setEntities.push_back(id);
    }
}

int poolLiveVoxelCount() {
    auto pool = IREntity::getComponentOptional<C_VoxelPool>(g_mainCanvas);
    return pool.has_value() ? (*pool)->getLiveVoxelCount() : -1;
}

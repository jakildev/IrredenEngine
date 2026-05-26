// Chunk streaming smoke test — exercises the ChunkDiskPersistence +
// ChunkResidencyManager round-trip against a real voxel pool allocation.
// Run with --auto-screenshot <N> for fleet-standard screenshot capture.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/render/camera.hpp>

#include <irreden/world/chunk_persistence.hpp>
#include <irreden/world/chunk_residency.hpp>
#include <irreden/world/chunk_coord.hpp>

// COMPONENTS
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>

// SYSTEMS
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/render/systems/system_lod_update.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>
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

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {2.0f, vec2(0, 0), "zoom2_chunk_cube"},
    {4.0f, vec2(0, 0), "zoom4_chunk_cube"},
};

int g_autoWarmupFrames = 0;
int g_smokeFailCount = 0;

} // namespace

void initSystems();
void initCommands();
void initEntities();
void runChunkSmokeTest();

int main(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &g_autoWarmupFrames);

    IR_LOG_INFO("Starting creation: chunk_streaming_smoke");
    IREngine::init(argv[0]);
    initSystems();
    initCommands();
    // Run smoke test before initEntities so the 32768-voxel allocation is
    // returned to the pool before the visual cube claims its own slice.
    runChunkSmokeTest();
    initEntities();
    IREngine::gameLoop();
    return g_smokeFailCount > 0 ? 1 : 0;
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

// Exercises ChunkDiskPersistence + ChunkResidencyManager against the real
// voxel pool. Two test cases:
//   1. Color round-trip: write → evict (saves) → reload → verify colors.
//   2. Clean (never-dirtied) chunk must not produce a file on evict.
void runChunkSmokeTest() {
    constexpr unsigned int kVolumeSize =
        IRConstants::kChunkSize.x * IRConstants::kChunkSize.y * IRConstants::kChunkSize.z;
    const std::string saveRoot = "save_files/chunk_smoke";

    IRWorld::ChunkDiskPersistence persistence(saveRoot);

    IRWorld::ChunkResidencyManager::Config cfg{};
    cfg.voxelsPerChunk_ = kVolumeSize;
    cfg.poolAllocator_ = [](unsigned int size) -> IRRender::VoxelPoolAllocation {
        return IRRender::allocateVoxels(size, "main");
    };
    cfg.poolDeallocator_ = [](const IRRender::VoxelPoolAllocation &alloc) {
        IRRender::deallocateVoxels(alloc.startIndex_, alloc.voxels_.size(), "main");
    };
    cfg.persistence_ = &persistence;

    IRWorld::ChunkResidencyManager mgr(std::move(cfg));

    // --- Test 1: color round-trip ---
    auto originKey = IRPrefab::Chunk::pack(IRMath::ivec3{0, 0, 0});
    mgr.requestResident(originKey, IRWorld::RequestPriority::FORCED);

    auto *slot = mgr.slot(originKey);
    if (slot == nullptr) {
        IR_LOG_ERROR("[chunk_smoke] FAIL: slot null after requestResident");
        ++g_smokeFailCount;
    } else {
        // Write a distinctive color to every voxel in the allocated slice.
        constexpr IRMath::Color kTestColor{200, 150, 80, 255};
        for (std::size_t i = 0; i < kVolumeSize; ++i) {
            slot->poolAllocation_.voxels_[i].color_ = kTestColor;
        }
        mgr.markChunkDirty(originKey);
        mgr.requestEvict(originKey);

        if (!persistence.chunkExists(originKey)) {
            IR_LOG_ERROR("[chunk_smoke] FAIL: chunk file missing after dirty evict");
            ++g_smokeFailCount;
        } else {
            IR_LOG_INFO("[chunk_smoke] PASS: dirty chunk written to disk");
        }

        // Re-request and verify the colors survived the serialization round-trip.
        mgr.requestResident(originKey, IRWorld::RequestPriority::FORCED);
        slot = mgr.slot(originKey);
        if (slot == nullptr) {
            IR_LOG_ERROR("[chunk_smoke] FAIL: slot null after reload");
            ++g_smokeFailCount;
        } else {
            bool ok = true;
            for (std::size_t i = 0; i < kVolumeSize && ok; ++i) {
                const auto &c = slot->poolAllocation_.voxels_[i].color_;
                if (c.red_ != kTestColor.red_ || c.green_ != kTestColor.green_ ||
                    c.blue_ != kTestColor.blue_ || c.alpha_ != kTestColor.alpha_) {
                    IR_LOG_ERROR(
                        "[chunk_smoke] FAIL: color mismatch at voxel {}: "
                        "expected ({},{},{},{}) got ({},{},{},{})",
                        i,
                        kTestColor.red_,
                        kTestColor.green_,
                        kTestColor.blue_,
                        kTestColor.alpha_,
                        c.red_,
                        c.green_,
                        c.blue_,
                        c.alpha_
                    );
                    ++g_smokeFailCount;
                    ok = false;
                }
            }
            if (ok) {
                IR_LOG_INFO(
                    "[chunk_smoke] PASS: color round-trip verified for {} voxels", kVolumeSize
                );
            }
        }
        mgr.requestEvict(originKey);
    }

    // --- Test 2: clean (non-dirty) chunk must not produce a file ---
    auto cleanKey = IRPrefab::Chunk::pack(IRMath::ivec3{99, 99, 99});
    IR_ASSERT(!persistence.chunkExists(cleanKey), "Pre-condition: clean chunk coord should have no file");
    mgr.requestResident(cleanKey, IRWorld::RequestPriority::FORCED);
    // Intentionally omit markChunkDirty — eviction must skip the save.
    mgr.requestEvict(cleanKey);
    if (persistence.chunkExists(cleanKey)) {
        IR_LOG_ERROR("[chunk_smoke] FAIL: clean (non-dirty) chunk was saved to disk");
        ++g_smokeFailCount;
    } else {
        IR_LOG_INFO("[chunk_smoke] PASS: clean chunk not written to disk");
    }

    if (g_smokeFailCount == 0) {
        IR_LOG_INFO("[chunk_smoke] All smoke tests PASSED");
    } else {
        IR_LOG_ERROR("[chunk_smoke] {} smoke test(s) FAILED", g_smokeFailCount);
    }
}

void initEntities() {
    // Simple visual cube — confirms the render pipeline is healthy after
    // the smoke test completes.
    IREntity::createEntity(
        C_LocalTransform{IRMath::vec3{0.0f, 0.0f, 0.0f}},
        C_VoxelSetNew{IRMath::ivec3{5, 5, 5}, IRMath::Color{100, 200, 220, 255}, true}
    );
}

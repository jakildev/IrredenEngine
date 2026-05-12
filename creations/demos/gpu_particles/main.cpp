#include <irreden/ir_engine.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>

#include <irreden/render/components/component_gpu_particle_pool.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/gpu_particles.hpp>

#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_camera_mouse_pan.hpp>
#include <irreden/render/systems/system_render_gpu_particles_to_trixel.hpp>
#include <irreden/render/systems/system_screen_residual_rotate.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_update_gpu_particles.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>

#include <irreden/common/command_suite_camera.hpp>
#include <irreden/common/command_suite_capture.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <list>

using namespace IRComponents;
using namespace IRMath;

namespace {

constexpr int kInitialParticleCount = 1024;
constexpr float kSpawnExtent = 24.0f;    // half-extent of the spawn cube (voxels)
constexpr float kVelocityRange = 6.0f;   // ± voxels/second
constexpr float kLifetimeSeconds = 8.0f; // long enough that all 6 shots see live particles

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {0.5f, vec2(0, 0), "fit_field"},
    {1.0f, vec2(0, 0), "zoom1_origin"},
    {2.0f, vec2(0, 0), "zoom2_origin"},
};

int g_autoWarmupFrames = 0;
int g_spawnsPerFrame = 0; // T-159 continuous-emitter verification knob

void parseArgs(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &g_autoWarmupFrames);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--spawns-per-frame") == 0 && i + 1 < argc) {
            const int n = std::atoi(argv[i + 1]);
            g_spawnsPerFrame = n < 0 ? 0 : n;
            ++i;
        }
    }
}

void seedParticles() {
    for (int i = 0; i < kInitialParticleCount; ++i) {
        const vec3 pos = IRMath::randomVec(vec3(-kSpawnExtent), vec3(kSpawnExtent));
        const vec3 vel = IRMath::randomVec(vec3(-kVelocityRange), vec3(kVelocityRange));
        const Color color = IRMath::randomColor();
        IRPrefab::GpuParticles::spawn(pos, vel, kLifetimeSeconds, color);
    }
}

void configureCanvas() {
    const IREntity::EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    IREntity::setComponent(mainCanvas, C_TrixelCanvasRenderBehavior{});
    IREntity::setComponent(mainCanvas, C_GPUParticlePool{});
}

// Continuous-emitter spawner registered ahead of UPDATE_GPU_PARTICLES.
// One pool-iterating system that calls writeSlot N times per frame —
// every call is appended to the pool's pending list and flushed by
// UPDATE_GPU_PARTICLES as a single contiguous-run subData (the common
// case for sequential indices from `reserveSlot()`).
IRSystem::SystemId createContinuousSpawnerSystem(int spawnsPerFrame) {
    return IRSystem::createSystem<C_GPUParticlePool>(
        "GpuParticleContinuousSpawn",
        [spawnsPerFrame](C_GPUParticlePool &pool) {
            for (int i = 0; i < spawnsPerFrame; ++i) {
                const std::uint32_t slot = pool.reserveSlot();
                if (slot == C_GPUParticlePool::kInvalidSlot) {
                    return;
                }
                const vec3 pos = IRMath::randomVec(vec3(-kSpawnExtent), vec3(kSpawnExtent));
                const vec3 vel = IRMath::randomVec(vec3(-kVelocityRange), vec3(kVelocityRange));
                const Color color = IRMath::randomColor();
                pool.writeSlot(
                    slot,
                    IRRender::GpuParticle{pos, kLifetimeSeconds, vel, color.toPackedRGBA()}
                );
            }
        }
    );
}

} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    parseArgs(argc, argv);

    IR_LOG_INFO("Starting creation: gpu_particles");
    IREngine::init(argv[0]);

    initSystems();
    initCommands();
    initEntities();

    IRRender::setCameraPosition2DIso(vec2(0.0f, 0.0f));
    IRRender::setCameraZoom(1.0f);

    IREngine::gameLoop();
    return 0;
}

void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );

    std::list<IRSystem::SystemId> renderPipeline = {
        IRSystem::createSystem<IRSystem::CAMERA_MOUSE_PAN>(),
        IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
        IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_2>(),
        IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
    };
    if (g_spawnsPerFrame > 0) {
        // Insert spawner ahead of UPDATE_GPU_PARTICLES so its writes land
        // in the same-frame pending-list flush.
        renderPipeline.push_back(createContinuousSpawnerSystem(g_spawnsPerFrame));
    }
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::UPDATE_GPU_PARTICLES>());
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::RENDER_GPU_PARTICLES_TO_TRIXEL>());
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>());
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::SCREEN_SPACE_RESIDUAL_ROTATE>());

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
    IRCommand::registerCameraCommands();
    IRCommand::registerCaptureCommands();
}

void initEntities() {
    configureCanvas();
    seedParticles();
}

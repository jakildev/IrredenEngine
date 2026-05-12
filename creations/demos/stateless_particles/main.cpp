#include <irreden/ir_engine.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>

#include <irreden/render/components/component_stateless_particle_emitters.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>

#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_camera_mouse_pan.hpp>
#include <irreden/render/systems/system_render_stateless_particles_to_trixel.hpp>
#include <irreden/render/systems/system_screen_residual_rotate.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>

#include <irreden/common/command_suite_camera.hpp>
#include <irreden/common/command_suite_capture.hpp>

#include <cstdint>
#include <list>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace {

constexpr std::uint32_t kEmitterCount = 64u;
constexpr std::uint32_t kParticlesPerEmitter = 64u;
constexpr float kFieldHalfExtent = 48.0f;  // emitters spread across a ±48 voxel band
constexpr float kBaseLifetimeSeconds = 4.0f;
constexpr float kBaseUpwardVelocity = 6.0f;  // voxels/sec along -Z (visually up)
constexpr float kGravityZ = 4.0f;            // pulls particles back down (+Z is down)
constexpr float kVelocityJitter = 3.0f;
constexpr float kPositionJitter = 2.0f;

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {0.5f, vec2(0, 0), "fit_field"},
    {1.0f, vec2(0, 0), "zoom1_origin"},
    {2.0f, vec2(0, 0), "zoom2_origin"},
};

int g_autoWarmupFrames = 0;

void parseArgs(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &g_autoWarmupFrames);
}

void configureCanvas() {
    const IREntity::EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    IREntity::setComponent(mainCanvas, C_TrixelCanvasRenderBehavior{});
    IREntity::setComponent(mainCanvas, C_StatelessParticleEmitters{});
}

void seedEmitters() {
    const IREntity::EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    auto &emitters = IREntity::getComponent<C_StatelessParticleEmitters>(mainCanvas);

    for (std::uint32_t i = 0; i < kEmitterCount; ++i) {
        GpuParticleEmitter e{};
        e.origin_ = IRMath::randomVec(
            vec3(-kFieldHalfExtent, -kFieldHalfExtent, 0.0f),
            vec3(kFieldHalfExtent, kFieldHalfExtent, 0.0f)
        );
        // baseVelocity points "up" in iso (= -Z in world). Each emitter gets
        // an independent jittered velocity so the spray fans visibly.
        e.baseVelocity_ = vec3(0.0f, 0.0f, -kBaseUpwardVelocity);
        e.gravity_ = vec3(0.0f, 0.0f, kGravityZ);
        e.positionJitter_ = vec3(kPositionJitter, kPositionJitter, kPositionJitter);
        e.velocityJitter_ = vec3(kVelocityJitter, kVelocityJitter, kVelocityJitter);
        e.baseLifetimeSeconds_ = kBaseLifetimeSeconds;
        // spawnRate = particlesPerEmitter / baseLifetime so the staggered
        // phase offsets fully cover the lifetime: subIndex (0..N-1) maps to
        // spawnOffset (0..lifetime), keeping every emitter steady-state.
        e.spawnRate_ =
            static_cast<float>(kParticlesPerEmitter) / kBaseLifetimeSeconds;
        e.baseColorPacked_ = IRMath::randomColor().toPackedRGBA();
        e.emitterFlags_ = 0u;
        e.particlesPerEmitter_ = kParticlesPerEmitter;
        emitters.addEmitter(e);
    }
}

} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    parseArgs(argc, argv);

    IR_LOG_INFO("Starting creation: stateless_particles");
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
        IRSystem::createSystem<IRSystem::RENDER_STATELESS_PARTICLES_TO_TRIXEL>(),
        IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
        IRSystem::createSystem<IRSystem::SCREEN_SPACE_RESIDUAL_ROTATE>(),
    };

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
    seedEmitters();
}

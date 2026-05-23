#include <irreden/ir_engine.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>

#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_stateless_particle_emitters.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>

#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>
#include <irreden/render/systems/system_build_light_occlusion_grid.hpp>
#include <irreden/render/camera_controls.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_compute_sun_shadow.hpp>
#include <irreden/render/systems/system_compute_voxel_ao.hpp>
#include <irreden/render/systems/system_lighting_to_trixel.hpp>
#include <irreden/render/systems/system_render_stateless_particles_to_trixel.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>

#include <irreden/common/command_suite_capture.hpp>

#include <cstdint>
#include <list>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace {

constexpr std::uint32_t kEmitterCount = 64u;
constexpr std::uint32_t kParticlesPerEmitter = 64u;
// Field sized so the iso footprint (±2·extent in iso x, ±4·extent in iso y at
// peak particle altitude) fills most of the canvas at zoom=1. Engine clamps
// camera zoom to [1, 64], so there is no true zoom-out — the lowest valid
// zoom needs a field big enough to look populated on its own.
constexpr float kFieldHalfExtent = 160.0f;
constexpr float kBaseLifetimeSeconds = 4.0f;
constexpr float kBaseUpwardVelocity = 6.0f; // voxels/sec along -Z (visually up)
constexpr float kGravityZ = 4.0f;           // pulls particles back down (+Z is down)
constexpr float kVelocityJitter = 3.0f;
constexpr float kPositionJitter = 2.0f;

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, vec2(0, 0), "zoom1_origin"},
    {2.0f, vec2(0, 0), "zoom2_origin"},
    {4.0f, vec2(0, 0), "zoom4_origin"},
    {8.0f, vec2(0, 0), "zoom8_origin"},
};

int g_autoWarmupFrames = 0;

void parseArgs(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &g_autoWarmupFrames);
}

void configureCanvas() {
    const IREntity::EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    const ivec2 canvasSize = IREntity::getComponent<C_TriangleCanvasTextures>(mainCanvas).size_;
    // Particles render as 2x3 voxel diamonds with face-priority depth
    // encoding; the lighting pass uses the per-pixel face normal to apply
    // 3-tone shading. Without these canvas-side textures bound the
    // particles read as flat-color diamonds.
    IREntity::setComponent(mainCanvas, C_CanvasAOTexture{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasSunShadow{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasLightVolume{});
    IREntity::setComponent(mainCanvas, C_TrixelCanvasRenderBehavior{});
    IREntity::setComponent(mainCanvas, C_StatelessParticleEmitters{});
}

void configureLighting() {
    // Match lighting_demo_scene.hpp's slight off-axis sun pose so face
    // ordering reads Z > X > Y on visible voxel diamonds. The dot-product
    // lambert is positive on all three visible iso faces (-X, -Y, -Z
    // outward normals), so each face takes a distinct shade rather than
    // collapsing to flat top-light.
    IRRender::setSunDirection(vec3(-0.3f, -0.2f, -0.93f));
    IRRender::setSunIntensity(1.0f);
    IRRender::setSunAmbient(0.4f);
    IRRender::setSunShadowsEnabled(true);
    IRRender::setAOEnabled(true);
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
        e.spawnRate_ = static_cast<float>(kParticlesPerEmitter) / kBaseLifetimeSeconds;
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

    std::list<IRSystem::SystemId> renderPipeline = IRPrefab::Camera::standardControlSystems();
    renderPipeline.insert(
        renderPipeline.end(),
        {
            IRSystem::createSystem<IRSystem::BUILD_LIGHT_OCCLUSION_GRID>(),
            IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
            IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::RENDER_STATELESS_PARTICLES_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::COMPUTE_VOXEL_AO>(),
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

void initCommands() {
    IRPrefab::Camera::registerStandardKeyboardCommands();
    IRCommand::registerCaptureCommands();
}

void initEntities() {
    configureCanvas();
    configureLighting();
    seedEmitters();
}

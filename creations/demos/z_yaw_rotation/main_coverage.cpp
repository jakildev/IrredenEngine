#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/camera.hpp>

// COMPONENTS
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
// SYSTEMS
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>

// COMMAND SUITES
#include <irreden/common/command_suite_capture.hpp>
#include <irreden/render/camera_controls.hpp>

#include <cstdio>

using namespace IRComponents;

// Rotated-view coverage fixture: a voxel floor wider than any view of it, far
// from the world origin, looked at from every yaw quadrant under each pivot
// (explicit at the floor, explicit at the world origin, screen-center default).
// The floor fills the frame at every shot, so any background pixel is content
// the renderer dropped. Run it under configs/coverage_landscape.lua and
// configs/coverage_portrait.lua; the floor is sized for those two canvases at
// zoom 1. scripts/render-view-coverage-metric.py grades the captures.

namespace {

constexpr vec3 kFloorCenter{1500.0f, -900.0f, 0.0f};
constexpr int kFloorEdge = 400;
constexpr int kPillarEdge = 12;
constexpr int kPillarHeight = 40;
constexpr float kPillarOffset = 60.0f;

enum class Pivot { FLOOR, ORIGIN, DEFAULT };

constexpr float kYaws[] = {0.35f, 1.36f, 2.36f, 2.97f, 3.93f, 5.2f};
constexpr int kYawCount = static_cast<int>(sizeof(kYaws) / sizeof(kYaws[0]));
// One settled cardinal shot leads the default-pivot block: the first rotated
// one acquires its pivot from that frame's surface under the crosshair.
constexpr int kShotCount = 3 * kYawCount + 1;

// The half-diagonal of the largest supported view (in iso units, at zoom 1)
// must fit inside the floor's iso footprint, the diamond `|x| + |y| <= edge`.
constexpr float kLargestViewHalfDiagonalIso = 252.0f;
static_assert(
    kLargestViewHalfDiagonalIso * IRMath::kSqrt2 < static_cast<float>(kFloorEdge),
    "the floor must cover the frame at every yaw, or background stops meaning dropped content"
);

IRVideo::IndexedSweepShots<> g_shots;
int g_autoWarmupFrames = 0;

IRVideo::AutoScreenshotShot coverageShot(std::size_t index) {
    IRVideo::AutoScreenshotShot shot{};
    shot.zoom_ = 1.0f;
    const int block = static_cast<int>(index) / kYawCount;
    if (block >= 2) {
        // Default pivot: the raw camera centers the floor at yaw 0 and the
        // pivot keeps what is under the crosshair there.
        const int step = static_cast<int>(index) - 2 * kYawCount;
        shot.yawRadians_ = step == 0 ? 0.0f : kYaws[step - 1];
        shot.cameraIso_ = -IRMath::pos3DtoPos2DIso(kFloorCenter);
        return shot;
    }
    shot.yawRadians_ = kYaws[static_cast<int>(index) % kYawCount];
    shot.hasPivotFocus_ = true;
    if (block == 0) {
        shot.pivotFocusWorld_ = kFloorCenter;
        shot.cameraIso_ = -IRMath::pos3DtoPos2DIso(kFloorCenter);
    } else {
        // Pivot at the world origin, view panned out to the floor.
        shot.pivotFocusWorld_ = vec3(0.0f);
        shot.cameraIso_ = -IRMath::pos3DtoPos2DIsoYawed(kFloorCenter, shot.yawRadians_);
    }
    return shot;
}

void coverageLabel(std::size_t index, char *out, std::size_t size) {
    static constexpr const char *kPivotNames[] = {"floor", "origin", "default"};
    const int block = IRMath::min(static_cast<int>(index) / kYawCount, 2);
    const float yaw = coverageShot(index).yawRadians_;
    std::snprintf(out, size, "%s_yaw%03d", kPivotNames[block], static_cast<int>(yaw * 100.0f));
}

} // namespace

void initSystems();
void initEntities();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: z_yaw_rotation/coverage");
    IREngine::init(argc, argv);
    g_autoWarmupFrames = IREngine::args().autoScreenshotWarmupFrames();
    initSystems();
    IRPrefab::Camera::registerStandardKeyboardCommands();
    IRCommand::registerCaptureCommands();
    initEntities();
    IREngine::gameLoop();
    return 0;
}

void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>()}
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
            IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
        }
    );

    if (g_autoWarmupFrames > 0) {
        g_shots.build(kShotCount, coverageShot, coverageLabel);
        IRVideo::AutoScreenshotConfig config{};
        config.warmupFrames_ = g_autoWarmupFrames;
        config.shots_ = g_shots.shots_.data();
        config.numShots_ = static_cast<int>(g_shots.shots_.size());
        renderPipeline.push_back(IRVideo::createAutoScreenshotSystem(config));
    }

    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}

void initEntities() {
    IREntity::createEntity(
        C_LocalTransform{kFloorCenter},
        C_VoxelSetNew{ivec3(kFloorEdge, kFloorEdge, 1), Color{96, 104, 128, 255}, true}
    );

    // Height above the reference plane: pillars displace along the view axis
    // under yaw, which is what a store window with no headroom drops first.
    const float pillarZ = -0.5f * static_cast<float>(kPillarHeight);
    for (const vec2 corner :
         {vec2(1.0f, 1.0f), vec2(1.0f, -1.0f), vec2(-1.0f, 1.0f), vec2(-1.0f)}) {
        IREntity::createEntity(
            C_LocalTransform{kFloorCenter + vec3(corner * kPillarOffset, pillarZ)},
            C_VoxelSetNew{
                ivec3(kPillarEdge, kPillarEdge, kPillarHeight),
                Color{220, 140, 80, 255},
                true
            }
        );
    }

    IREntity::setComponent(IRRender::getActiveCanvasEntity(), C_TrixelCanvasRenderBehavior{});
    IRRender::setCameraPosition2DIso(-IRMath::pos3DtoPos2DIso(kFloorCenter));
}

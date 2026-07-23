// skeletal_demo — procedural skeletal voxel entities exercising the
// rig + skinning path (Phase 2.10 / #1611):
//   1. Snake (30-joint linear chain, bent at midpoint)
//   2. Desk   (static, no skeleton)
//   3. Lamp   (4-joint linear chain, bent at top)
//   4. Cross  (9-joint branching tree, tips bent upward)
//
// All skeletal entities are authored programmatically so the demo
// builds and runs headlessly. Round-trip .rig save/load is verified
// for each skeletal entity at startup.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/render/camera.hpp>

#include <irreden/asset/rig_format.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// COMPONENTS
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_joint.hpp>
#include <irreden/voxel/components/component_skeleton.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
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
#include <irreden/render/systems/system_update_voxel_positions_gpu.hpp>
#include <irreden/render/systems/system_update_joint_matrices.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_build_light_occlusion_grid.hpp>
#include <irreden/render/systems/system_compute_voxel_ao.hpp>
#include <irreden/render/systems/system_resolve_per_axis_screen_depth.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>
#include <irreden/render/systems/system_compute_sun_shadow.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_lighting_to_trixel.hpp>
#include <irreden/render/systems/system_fog_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_sprites_to_screen.hpp>
#include <irreden/render/camera_controls.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/fog_of_war.hpp>

// COMMAND SUITES
#include <irreden/common/command_suite_capture.hpp>

namespace {
int g_autoWarmupFrames = 0;
} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: skeletal_demo");
    IREngine::init(argc, argv);
    g_autoWarmupFrames = IREngine::args().autoScreenshotWarmupFrames();
    initSystems();
    initCommands();
    initEntities();
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
    const IRSystem::SystemId updateVoxelPositionsId =
        IRSystem::createSystem<IRSystem::UPDATE_VOXEL_POSITIONS_GPU>();
    const IRSystem::SystemId updateJointMatricesId =
        IRSystem::createSystem<IRSystem::UPDATE_JOINT_MATRICES>();
    renderPipeline.insert(
        renderPipeline.end(),
        {
            IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
            IRSystem::createSystem<IRSystem::BUILD_LIGHT_OCCLUSION_GRID>(),
            updateJointMatricesId,
            updateVoxelPositionsId,
            IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
            IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::COMPUTE_VOXEL_AO>(),
            IRSystem::createSystem<IRSystem::RESOLVE_PER_AXIS_SCREEN_DEPTH>(),
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
        // `static`: createAutoScreenshotSystem copies this config into a
        // process-lifetime CyclingState that keeps the `shots_` POINTER, so the
        // table must outlive the game loop (auto_screenshot.hpp contract). A
        // plain function-local `constexpr` array dies at scope exit, leaving
        // `shots_` dangling → garbage `shot.label_` (null) → fmt crash on the
        // first capture frame. Static storage duration matches the other demos.
        static constexpr IRVideo::AutoScreenshotShot kShots[] = {
            {1.0f, vec2(0, 0), 0.0f, "zoom1"},
            {2.0f, vec2(0, 0), 0.0f, "zoom2"},
            {4.0f, vec2(0, 0), 0.0f, "zoom4"},
            {4.0f, vec2(0, 0), IRMath::kHalfPi, "zoom4_yaw90"},
        };
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

// ---------------------------------------------------------------------------
// Rig round-trip verification. Builds an IRAsset::Rig from the C_Skeleton,
// writes it to memory, reads it back, and checks joint count.
// ---------------------------------------------------------------------------
static void
verifyRigRoundTrip(const C_Skeleton &skeleton, const std::string &label, int expectedJoints) {
    IRAsset::Rig rig;
    rig.joints_.reserve(skeleton.joints_.size());
    for (std::size_t i = 0; i < skeleton.joints_.size(); ++i) {
        IRAsset::RigJoint j;
        j.translation_ = vec4(skeleton.bindPose_[i].translation_, 0.0f);
        j.rotation_ = skeleton.bindPose_[i].rotation_;
        j.parentIndex_ =
            (i == 0)
                ? 0u
                : static_cast<std::uint32_t>(i - 1); // linear chain; hierarchy not round-tripped
        rig.joints_.push_back(j);
    }
    IRAsset::MemoryBinaryWriter w;
    auto status = IRAsset::writeRig(w, rig);
    if (!status.ok()) {
        IR_LOG_ERROR("{}: rig write failed", label);
        return;
    }
    const auto &buf = w.buffer();
    IRAsset::MemoryBinaryReader r{buf.data(), buf.size()};
    auto loaded = IRAsset::readRig(r);
    if (!loaded.ok()) {
        IR_LOG_ERROR("{}: rig read-back failed", label);
        return;
    }
    const int got = static_cast<int>(loaded.value_.joints_.size());
    if (got != expectedJoints) {
        IR_LOG_ERROR(
            "{}: round-trip joint count mismatch: expected={} got={}",
            label,
            expectedJoints,
            got
        );
    } else {
        IR_LOG_INFO("{}: rig round-trip OK ({} joints)", label, got);
    }
}

// ---------------------------------------------------------------------------
// Snake — 30-joint linear chain along the Z axis, joint 15 posed 25 degrees
// around the X axis so the rear half of the body deforms visibly.
// ---------------------------------------------------------------------------
static void createSnake(vec3 worldPos) {
    constexpr int kNumJoints = 30;
    constexpr int kSegLen = 3;                    // voxels per segment along Z
    const ivec3 size{3, 3, kNumJoints * kSegLen}; // 3x3x90
    const Color baseColor{60, 200, 90, 255};

    IREntity::EntityId rigRoot =
        IREntity::createEntity(C_LocalTransform{worldPos}, C_VoxelSetNew{size, baseColor, false});
    auto &vs = IREntity::getComponent<C_VoxelSetNew>(rigRoot);

    vs.editVoxels([&](int, C_Voxel &voxel, vec3 localPos) {
        int seg = static_cast<int>(localPos.z) / kSegLen;
        voxel.bone_id_ = static_cast<std::uint8_t>(seg);
        // Alternate shading per segment so joint boundaries are visible.
        if (seg & 1) {
            voxel.color_ = Color{40, 160, 70, 255};
        }
    });

    constexpr vec4 kIdentity{0.0f, 0.0f, 0.0f, 1.0f};
    const vec4 kBendPose = IRMath::quatAxisAngle(vec3(1.0f, 0.0f, 0.0f), IRMath::kPi / 7.2f);

    C_Skeleton skeleton;
    skeleton.joints_.reserve(kNumJoints);
    skeleton.bindPose_.reserve(kNumJoints);

    IREntity::EntityId prevJoint = IREntity::kNullEntity;
    for (int i = 0; i < kNumJoints; ++i) {
        const vec3 localPos =
            (i == 0) ? vec3(1.0f, 1.0f, 0.0f) : vec3(0.0f, 0.0f, static_cast<float>(kSegLen));
        const vec4 rot = (i == kNumJoints / 2) ? kBendPose : kIdentity;

        IREntity::EntityId joint =
            IREntity::createEntity(C_Joint{}, C_LocalTransform{localPos, rot});
        if (i == 0) {
            IREntity::setParent(joint, rigRoot);
        } else {
            IREntity::setParent(joint, prevJoint);
        }
        skeleton.joints_.push_back(joint);
        skeleton.bindPose_.push_back(
            IRMath::SQT{vec3(1.0f), kIdentity, vec3(1.0f, 1.0f, static_cast<float>(i * kSegLen))}
        );
        prevJoint = joint;
    }
    const int snakeVoxels = vs.numVoxels_;
    IREntity::setComponent(rigRoot, skeleton);
    IR_LOG_INFO("Snake: entity={} joints={} voxels={}", rigRoot, kNumJoints, snakeVoxels);
    verifyRigRoundTrip(skeleton, "snake", kNumJoints);
}

// ---------------------------------------------------------------------------
// Desk — static showcase entity (no skeleton). Tabletop + four legs via
// C_ShapeDescriptor so the GPU evaluates the SDF directly (no voxel alloc).
// ---------------------------------------------------------------------------
static void createDesk(vec3 worldPos) {
    const Color tableColor{160, 105, 60, 255};
    const Color legColor{130, 80, 45, 255};

    // Tabletop: wide flat BOX
    IREntity::createEntity(
        C_LocalTransform{worldPos + vec3(0.0f, 0.0f, 0.0f)},
        C_ShapeDescriptor{IRMath::SDF::ShapeType::BOX, vec4(14.0f, 1.0f, 8.0f, 0.0f), tableColor}
    );
    // Four legs: cylinders at each corner
    constexpr float kR = 0.8f;
    constexpr float kH = 6.0f;
    const vec4 legParams{kR, kR, kH, 0.0f};
    const vec3 legOffsets[4] = {
        {-6.0f, kH * 0.5f + 0.5f, -3.0f},
        {6.0f, kH * 0.5f + 0.5f, -3.0f},
        {-6.0f, kH * 0.5f + 0.5f, 3.0f},
        {6.0f, kH * 0.5f + 0.5f, 3.0f},
    };
    for (const auto &off : legOffsets) {
        IREntity::createEntity(
            C_LocalTransform{worldPos + off},
            C_ShapeDescriptor{IRMath::SDF::ShapeType::CYLINDER, legParams, legColor}
        );
    }
    IR_LOG_INFO("Desk: static entity at ({},{},{})", worldPos.x, worldPos.y, worldPos.z);
}

// ---------------------------------------------------------------------------
// Lamp — 4-joint linear chain along the Y axis, top joint posed 60 degrees
// around the Z axis so the lamp head angles outward.
// ---------------------------------------------------------------------------
static void createLamp(vec3 worldPos) {
    constexpr int kNumJoints = 4;
    constexpr int kSegLen = 5;                    // voxels per segment along Y
    const ivec3 size{2, kNumJoints * kSegLen, 2}; // 2x20x2
    const Color baseColor{220, 185, 50, 255};

    IREntity::EntityId rigRoot =
        IREntity::createEntity(C_LocalTransform{worldPos}, C_VoxelSetNew{size, baseColor, false});
    auto &vs = IREntity::getComponent<C_VoxelSetNew>(rigRoot);

    vs.editVoxels([&](int, C_Voxel &voxel, vec3 localPos) {
        int seg = static_cast<int>(localPos.y) / kSegLen;
        voxel.bone_id_ = static_cast<std::uint8_t>(seg);
        // Lighten toward the top to simulate a warm glow gradient.
        const float t = static_cast<float>(seg) / static_cast<float>(kNumJoints - 1);
        const auto r = static_cast<std::uint8_t>(220u + static_cast<std::uint8_t>(35u * t));
        voxel.color_ = Color{
            r,
            static_cast<std::uint8_t>(185u - static_cast<std::uint8_t>(40u * t)),
            50u,
            255u
        };
    });

    constexpr vec4 kIdentity{0.0f, 0.0f, 0.0f, 1.0f};
    const vec4 kHeadPose = IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), IRMath::kPi / 3.0f);

    C_Skeleton skeleton;
    skeleton.joints_.reserve(kNumJoints);
    skeleton.bindPose_.reserve(kNumJoints);

    IREntity::EntityId prevJoint = IREntity::kNullEntity;
    for (int i = 0; i < kNumJoints; ++i) {
        const vec3 localPos =
            (i == 0) ? vec3(0.5f, 0.0f, 0.5f) : vec3(0.0f, static_cast<float>(kSegLen), 0.0f);
        const vec4 rot = (i == kNumJoints - 1) ? kHeadPose : kIdentity;

        IREntity::EntityId joint =
            IREntity::createEntity(C_Joint{}, C_LocalTransform{localPos, rot});
        if (i == 0) {
            IREntity::setParent(joint, rigRoot);
        } else {
            IREntity::setParent(joint, prevJoint);
        }
        skeleton.joints_.push_back(joint);
        skeleton.bindPose_.push_back(
            IRMath::SQT{vec3(1.0f), kIdentity, vec3(0.5f, static_cast<float>(i * kSegLen), 0.5f)}
        );
        prevJoint = joint;
    }
    const int lampVoxels = vs.numVoxels_;
    IREntity::setComponent(rigRoot, skeleton);
    IR_LOG_INFO("Lamp: entity={} joints={} voxels={}", rigRoot, kNumJoints, lampVoxels);
    verifyRigRoundTrip(skeleton, "lamp", kNumJoints);
}

// ---------------------------------------------------------------------------
// Cross — 9-joint branching tree. Central hub with four arms, each arm has
// a shoulder joint and a wrist joint; wrists are posed 40 degrees upward.
// Demonstrates non-linear (tree) topology vs the linear snake/lamp chains.
//
// Layout (top view, XZ plane):
//   . . F F . .
//   L L C C R R
//   . . B B . .
// where C=center, L=left arm, R=right arm, F=front arm, B=back arm.
// Arm voxels outside the cross shape are deactivated.
// ---------------------------------------------------------------------------
static void createCross(vec3 worldPos) {
    // One 10x3x10 box; arms occupy a cross footprint, interior deactivated.
    const ivec3 size{10, 3, 10};
    const Color armColor{80, 140, 220, 255};

    IREntity::EntityId rigRoot =
        IREntity::createEntity(C_LocalTransform{worldPos}, C_VoxelSetNew{size, armColor, false});
    auto &vs = IREntity::getComponent<C_VoxelSetNew>(rigRoot);

    // Classify each voxel into a body region and assign bone_id.
    // Voxels outside the cross footprint are deactivated. editVoxels resyncs
    // the active mask AND face occupancy — the deactivated interior would
    // otherwise render black under the lit path (the recompute a bare
    // syncActiveMask() misses).
    vs.editVoxels([&](int, C_Voxel &voxel, vec3 localPos) {
        const int x = static_cast<int>(localPos.x);
        const int z = static_cast<int>(localPos.z);
        const bool inCenter = (x >= 3 && x <= 6 && z >= 3 && z <= 6);
        const bool inLeft = (x >= 0 && x <= 2 && z >= 3 && z <= 6);
        const bool inRight = (x >= 7 && x <= 9 && z >= 3 && z <= 6);
        const bool inFront = (x >= 3 && x <= 6 && z >= 0 && z <= 2);
        const bool inBack = (x >= 3 && x <= 6 && z >= 7 && z <= 9);

        if (inCenter) {
            voxel.bone_id_ = 0;
            voxel.color_ = Color{100, 160, 240, 255};
        } else if (inLeft) {
            // x=2 → shoulder (bone 1), x=0..1 → wrist (bone 2)
            voxel.bone_id_ = (x == 2) ? 1u : 2u;
            voxel.color_ = (x == 2) ? Color{80, 140, 220, 255} : Color{60, 110, 190, 255};
        } else if (inRight) {
            voxel.bone_id_ = (x == 7) ? 3u : 4u;
            voxel.color_ = (x == 7) ? Color{80, 140, 220, 255} : Color{60, 110, 190, 255};
        } else if (inFront) {
            voxel.bone_id_ = (z == 2) ? 5u : 6u;
            voxel.color_ = (z == 2) ? Color{80, 140, 220, 255} : Color{60, 110, 190, 255};
        } else if (inBack) {
            voxel.bone_id_ = (z == 7) ? 7u : 8u;
            voxel.color_ = (z == 7) ? Color{80, 140, 220, 255} : Color{60, 110, 190, 255};
        } else {
            voxel.deactivate();
        }
    });

    constexpr vec4 kIdentity{0.0f, 0.0f, 0.0f, 1.0f};
    // Wrist-bend poses: left/right bend around Z; front/back bend around X.
    const vec4 kWristBendLR = IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), IRMath::kPi / 4.5f);
    const vec4 kWristBendFB = IRMath::quatAxisAngle(vec3(1.0f, 0.0f, 0.0f), IRMath::kPi / 4.5f);

    // joint[0]  center hub      — (4.5, 1, 4.5), CHILD_OF rigRoot
    // joint[1]  left shoulder   — (-2.5, 0, 0) from hub, CHILD_OF hub
    // joint[2]  left wrist      — (-2, 0, 0) from shoulder, CHILD_OF shoulder
    // joint[3]  right shoulder  — (+2.5, 0, 0), CHILD_OF hub
    // joint[4]  right wrist     — (+2, 0, 0), CHILD_OF right shoulder
    // joint[5]  front shoulder  — (0, 0, -2.5), CHILD_OF hub
    // joint[6]  front wrist     — (0, 0, -2), CHILD_OF front shoulder
    // joint[7]  back shoulder   — (0, 0, +2.5), CHILD_OF hub
    // joint[8]  back wrist      — (0, 0, +2), CHILD_OF back shoulder

    struct JointDef {
        vec3 localPos_;
        vec4 rot_;
        int parentIdx_; // -1 = rigRoot, else index in joints_
        vec3 bindPos_;  // rig-root-local rest position
    };
    const JointDef defs[9] = {
        {vec3(4.5f, 1.0f, 4.5f), kIdentity, -1, vec3(4.5f, 1.0f, 4.5f)},
        {vec3(-2.5f, 0.0f, 0.0f), kIdentity, 0, vec3(2.0f, 1.0f, 4.5f)},
        {vec3(-2.0f, 0.0f, 0.0f), kWristBendLR, 1, vec3(0.0f, 1.0f, 4.5f)},
        {vec3(2.5f, 0.0f, 0.0f), kIdentity, 0, vec3(7.0f, 1.0f, 4.5f)},
        {vec3(2.0f, 0.0f, 0.0f), kWristBendLR, 3, vec3(9.0f, 1.0f, 4.5f)},
        {vec3(0.0f, 0.0f, -2.5f), kIdentity, 0, vec3(4.5f, 1.0f, 2.0f)},
        {vec3(0.0f, 0.0f, -2.0f), kWristBendFB, 5, vec3(4.5f, 1.0f, 0.0f)},
        {vec3(0.0f, 0.0f, 2.5f), kIdentity, 0, vec3(4.5f, 1.0f, 7.0f)},
        {vec3(0.0f, 0.0f, 2.0f), kWristBendFB, 7, vec3(4.5f, 1.0f, 9.0f)},
    };

    C_Skeleton skeleton;
    skeleton.joints_.reserve(9);
    skeleton.bindPose_.reserve(9);
    IREntity::EntityId jointIds[9] = {};

    for (int i = 0; i < 9; ++i) {
        jointIds[i] =
            IREntity::createEntity(C_Joint{}, C_LocalTransform{defs[i].localPos_, defs[i].rot_});
        if (defs[i].parentIdx_ < 0) {
            IREntity::setParent(jointIds[i], rigRoot);
        } else {
            IREntity::setParent(jointIds[i], jointIds[defs[i].parentIdx_]);
        }
        skeleton.joints_.push_back(jointIds[i]);
        skeleton.bindPose_.push_back(IRMath::SQT{vec3(1.0f), kIdentity, defs[i].bindPos_});
    }
    const int crossVoxels = vs.numVoxels_;
    IREntity::setComponent(rigRoot, skeleton);
    IR_LOG_INFO("Cross: entity={} joints=9 voxels={}", rigRoot, crossVoxels);
    verifyRigRoundTrip(skeleton, "cross", 9);
}

void initEntities() {
    // Scene: four entities spread along the X axis.
    // Snake at x=-40, desk at x=-10, lamp at x=10, cross at x=35.
    createSnake(vec3(-40.0f, 0.0f, -5.0f));
    createDesk(vec3(-10.0f, 0.0f, 0.0f));
    createLamp(vec3(10.0f, 0.0f, -5.0f));
    createCross(vec3(35.0f, 0.0f, -5.0f));

    // Floor so AO / sun-shadow lighting has a surface.
    constexpr float kFloorZ = 5.0f;
    IREntity::EntityId floorEntity = IREntity::createEntity(
        C_LocalTransform{vec3(0.0f, 0.0f, kFloorZ)},
        C_ShapeDescriptor{
            IRMath::SDF::ShapeType::BOX,
            vec4(120.0f, 40.0f, 2.0f, 0.0f),
            Color{140, 140, 150, 255}
        }
    );
    IREntity::setComponent(floorEntity, C_LightBlocker{false, false, 0.0f});

    // Canvas setup: AO, sun shadow, light volume, fog of war.
    IREntity::EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    const ivec2 canvasSize = IREntity::getComponent<C_TriangleCanvasTextures>(mainCanvas).size_;
    IREntity::setComponent(mainCanvas, C_CanvasAOTexture{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasSunShadow{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasLightVolume{});
    IRPrefab::Fog::attachToCanvas(mainCanvas, 80);

    IRRender::setSunDirection(vec3(0.35f, 0.85f, -0.4f));
}

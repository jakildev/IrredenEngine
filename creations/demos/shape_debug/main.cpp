#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/render/camera.hpp>

#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/voxel/dense_bridge.hpp>

#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <numbers>
#include <string>
#include <vector>
// COMPONENTS
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/camera.hpp>

// SYSTEMS
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/render/systems/system_lod_update.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_update_voxel_positions_gpu.hpp>
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
#include <irreden/render/systems/system_auto_yaw_rotate.hpp>

// COMMAND SUITES
#include <irreden/common/command_suite_capture.hpp>

namespace {

// ROI crop tables are framebuffer-pixel coords; values below assume the
// 1280x720 default game resolution (`kGameResolution`). On HiDPI hosts the
// framebuffer is a power-of-two larger and the crops land in the upper-left
// quadrant of the captured image — still useful for edge-fidelity inspection
// at a given fixed offset. Pixel-precise crop placement is intentionally a
// per-host iteration point; refine the coords as the demo's content evolves.
constexpr IRVideo::RoiCrop kCropsZoom4Origin[] = {
    {520, 280, 128, 128, "center_cube_top"},
    {220, 280, 128, 128, "left_cube_silhouette"},
    {820, 280, 128, 128, "right_cube_silhouette"},
};

constexpr IRVideo::RoiCrop kCropsZoom8Origin[] = {
    {520, 280, 128, 128, "center_cube_top"},
    {300, 400, 128, 128, "lower_left_face"},
};

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, vec2(0, 0), 0.0f, "zoom1_origin"},
    {2.0f, vec2(0, 0), 0.0f, "zoom2_origin"},
    {4.0f,
     vec2(0, 0),
     0.0f,
     "zoom4_origin",
     kCropsZoom4Origin,
     sizeof(kCropsZoom4Origin) / sizeof(kCropsZoom4Origin[0])},
    {1.0f, vec2(1, 0), 0.0f, "zoom1_odd_offset"},
    {8.0f,
     vec2(0, 0),
     0.0f,
     "zoom8_origin",
     kCropsZoom8Origin,
     sizeof(kCropsZoom8Origin) / sizeof(kCropsZoom8Origin[0])},
    {4.0f, vec2(3, 5), 0.0f, "zoom4_offset_3_5"},
    // zoom16_lod_all_visible: active tier LOD_0, LOD_0 (red) tops the co-located stack.
    {16.0f, vec2(0, 0), 0.0f, "zoom16_lod_all_visible"},

    // Rotation coverage (#1261): four cardinals + one inter-cardinal expose
    // rotation-only regressions (#1256 checkerboard, #1257 inter-cardinal
    // deformation, future face-normal / shadow-AABB / chunk-mask bugs) that
    // a yaw=0 shot list cannot catch. zoom8 close-ups make sub-pixel
    // parity artifacts visible at full pixel scale.
    {4.0f, vec2(0, 0), IRMath::kHalfPi, "zoom4_yaw90"},
    {4.0f, vec2(0, 0), IRMath::kPi, "zoom4_yaw180"},
    {4.0f, vec2(0, 0), 3.0f * IRMath::kHalfPi, "zoom4_yaw270"},
    {4.0f, vec2(0, 0), IRMath::kQuarterPi, "zoom4_yaw45_inter_cardinal"},
    {8.0f,
     vec2(0, 0),
     IRMath::kPi,
     "zoom8_yaw180",
     kCropsZoom8Origin,
     sizeof(kCropsZoom8Origin) / sizeof(kCropsZoom8Origin[0])},

    // Camera-focus pivot coverage (#1352): the camera is panned off-origin and
    // then yawed. With RotationPivotMode::CAMERA_CENTER (the new engine default)
    // the world point under screen center stays pinned across the whole sweep —
    // the same focused content sits at the same screen pixel in all four shots.
    // The pre-#1352 ORIGIN pivot (reproducible via --pivot-origin) instead swings
    // that content in an arc. A shot list with only unpanned-or-yaw0 entries
    // cannot catch a pivot regression: the CAMERA_CENTER correction is the
    // identity unless the camera is BOTH panned and rotated, so every existing
    // shot is byte-identical between the two modes. yaw45 also exercises the
    // per-axis smooth-yaw scatter base (perAxisBase_) under the pivot.
    {4.0f, vec2(16, 16), 0.0f, "zoom4_pan16_yaw0_pivot"},
    {4.0f, vec2(16, 16), IRMath::kHalfPi, "zoom4_pan16_yaw90_pivot"},
    {4.0f, vec2(16, 16), IRMath::kPi, "zoom4_pan16_yaw180_pivot"},
    {4.0f, vec2(16, 16), IRMath::kQuarterPi, "zoom4_pan16_yaw45_pivot"},
};

int g_autoWarmupFrames = 0; // 0 = --auto-screenshot not requested
bool g_depthColor = false;
bool g_checkerboard = false; // opt-in via --checkerboard; flickered, off by default
// --gpu-voxel-smoke (#1396): spawn one voxel cube routed through the GPU
// voxel-position prepass with a fixed 45° rotation. Off by default so the
// standard scene stays byte-identical; the rotated cube is direct proof the
// prepass applied modelToWorld (the CPU world-position path is translation-only).
bool g_gpuVoxelSmoke = false;
int g_autoProfileFrames = 0; // 0 = disabled
int g_autoProfileCount = 0;
float g_initialZoom = 0.0f; // 0 = use engine default
float g_initialYawRadians = 0.0f;
float g_initialYaw = 0.0f;
bool g_initialYawSet = false;
// --pivot-origin (#1352): force RotationPivotMode::ORIGIN (the pre-#1352
// world-origin pivot) instead of the CAMERA_CENTER engine default. Lets the
// same panned+rotated shot list be captured in both modes for an A/B compare —
// CAMERA_CENTER pins the focused content at screen center, ORIGIN swings it in
// an arc. Off by default so the demo exercises the shipped default.
bool g_pivotOrigin = false;
IRRender::DebugOverlayMode g_debugOverlay = IRRender::DebugOverlayMode::NONE;
// --load-vxs <path>: load a DENSE-mode .vxs and render frame 0 alongside the
// built-in shape fixtures. Empty = not requested.
std::string g_loadVxsPath;

// --spin-yaw [deg/sec] (#1271): drive the camera's Z-yaw at a constant
// rate so the cardinal/residual rebracket can be eyeballed (live) or sampled
// at N evenly-spaced angles (auto-screenshot). 0 = flag not requested.
float g_spinYawDegPerSec = 0.0f;
// In auto-screenshot + spin-yaw mode, --auto-screenshot's value is
// reinterpreted as "shot count across one rotation"; default 24 → every 15°
// which hits every cardinal (0/90/180/270°) and every rebracket (45/135/...).
int g_spinYawShotCount = 24;

// Dynamic shot table populated at startup when --spin-yaw + --auto-screenshot
// are both set. Lives at namespace scope so the pointer handed to
// IRVideo::AutoScreenshotConfig outlives the game loop. The label strings
// are backed by a parallel buffer so each AutoScreenshotShot::label_ pointer
// remains stable.
std::vector<IRVideo::AutoScreenshotShot> g_spinYawShots;
std::vector<std::array<char, 32>> g_spinYawShotLabels;

} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &g_autoWarmupFrames);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--auto-profile") == 0) {
            g_autoProfileFrames = 300; // default
            if (i + 1 < argc) {
                int frames = std::atoi(argv[i + 1]);
                if (frames > 0) {
                    g_autoProfileFrames = frames;
                    ++i;
                }
            }
        } else if (std::strcmp(argv[i], "--depth-color") == 0) {
            g_depthColor = true;
        } else if (std::strcmp(argv[i], "--checkerboard") == 0) {
            g_checkerboard = true;
        } else if (std::strcmp(argv[i], "--gpu-voxel-smoke") == 0) {
            g_gpuVoxelSmoke = true;
        } else if (std::strcmp(argv[i], "--zoom") == 0) {
            if (i + 1 < argc) {
                float z = static_cast<float>(std::atof(argv[i + 1]));
                if (z > 0.0f) {
                    g_initialZoom = z;
                    ++i;
                }
            }
        } else if (std::strcmp(argv[i], "--debug-overlay") == 0) {
            if (i + 1 < argc) {
                g_debugOverlay = IRRender::debugOverlayModeFromString(argv[i + 1]);
                ++i;
            }
        } else if (std::strcmp(argv[i], "--yaw") == 0) {
            if (i + 1 < argc) {
                g_initialYawRadians = static_cast<float>(std::atof(argv[i + 1]));
                g_initialYaw = static_cast<float>(std::atof(argv[i + 1]));
                g_initialYawSet = true;
                ++i;
            }
        } else if (std::strcmp(argv[i], "--pivot-origin") == 0) {
            g_pivotOrigin = true;
        } else if (std::strcmp(argv[i], "--load-vxs") == 0) {
            if (i + 1 < argc) {
                g_loadVxsPath = argv[i + 1];
                ++i;
            }
        } else if (std::strcmp(argv[i], "--spin-yaw") == 0) {
            g_spinYawDegPerSec = 30.0f; // default rotation rate
            if (i + 1 < argc) {
                float v = static_cast<float>(std::atof(argv[i + 1]));
                if (v > 0.0f) {
                    g_spinYawDegPerSec = v;
                    ++i;
                }
            }
        }
    }

    // --spin-yaw + --auto-screenshot: reinterpret the screenshot value as
    // "shots across one rotation", and use a small internal warmup. This is
    // the regression-detection mode — N static frames at evenly-spaced yaws
    // so a render-verify diff can pinpoint the angle a glitch appears at.
    if (g_spinYawDegPerSec > 0.0f && g_autoWarmupFrames > 0) {
        g_spinYawShotCount = g_autoWarmupFrames;
        g_autoWarmupFrames = 10;
        IR_LOG_INFO(
            "Spin-yaw: warmup reset to 10 frames (--auto-screenshot value {} reinterpreted as shot "
            "count)",
            g_spinYawShotCount
        );
    }

    IR_LOG_INFO("Starting creation: shape_debug");
    IREngine::init(argv[0]);
    if (g_autoProfileFrames > 0) {
        IREngine::enableFrameTiming(true);
    }
    initSystems();
    initCommands();
    initEntities();
    if (g_initialZoom > 0.0f) {
        IRRender::setCameraZoom(g_initialZoom);
        vec2 actualZoom = IRRender::getCameraZoom();
        IR_LOG_INFO(
            "Initial zoom: requested={}, actual={} (snapped to nearest power of two)",
            g_initialZoom,
            actualZoom.x
        );
    }
    if (g_debugOverlay != IRRender::DebugOverlayMode::NONE) {
        IRRender::setDebugOverlay(g_debugOverlay);
    }
    if (g_initialYawRadians != 0.0f) {
        IRPrefab::Camera::setYaw(g_initialYawRadians);
        IR_LOG_INFO(
            "Initial camera Z-yaw: {} rad ({} deg)",
            g_initialYawRadians,
            g_initialYawRadians * (180.0f / std::numbers::pi_v<float>)
        );
    }
    if (g_initialYawSet) {
        IRPrefab::Camera::setYaw(g_initialYaw);
        IR_LOG_INFO("Initial yaw: {} rad", g_initialYaw);
    }
    if (g_pivotOrigin) {
        IRRender::setRotationPivotMode(IRRender::RotationPivotMode::ORIGIN);
        IR_LOG_INFO(
            "RotationPivotMode: ORIGIN (--pivot-origin) — Z-yaw pivots about the world origin"
        );
    }
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
    // --spin-yaw live mode: drive the camera each frame. In auto-screenshot
    // mode the per-shot setYaw() supplies the rotation instead — running both
    // would double-rotate between shots and break the "evenly-spaced" contract.
    if (g_spinYawDegPerSec > 0.0f &&
        g_autoWarmupFrames == 0) { // == 0: no auto-screenshot requested
        const float radPerFrame =
            g_spinYawDegPerSec * IRMath::kPi / 180.0f / static_cast<float>(IRConstants::kFPS);
        renderPipeline.push_front(IRSystem::createSystem<IRSystem::AUTO_YAW_ROTATE>(radPerFrame));
        IR_LOG_INFO(
            "Spin-yaw live: {} deg/sec ({} rad/frame at {} fps)",
            g_spinYawDegPerSec,
            radPerFrame,
            IRConstants::kFPS
        );
    }
    renderPipeline.insert(
        renderPipeline.end(),
        {
            IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
            IRSystem::createSystem<IRSystem::BUILD_LIGHT_OCCLUSION_GRID>(),
            // GPU voxel-position prepass (#1396) — writes binding 5 for
            // GPU-transform-indirected voxel sets before STAGE_1 reads it.
            // A no-op (no dispatch) unless a voxel set opts in via
            // gpuTransformSlot_, so the default scene stays byte-identical.
            IRSystem::createSystem<IRSystem::UPDATE_VOXEL_POSITIONS_GPU>(),
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

    if (g_autoProfileFrames > 0) {
        IRSystem::SystemId autoProfileId = IRSystem::createSystem<C_VoxelSetNew>(
            "AutoProfile",
            [](C_VoxelSetNew &) {},
            []() {
                ++g_autoProfileCount;
                if (g_autoProfileCount >= g_autoProfileFrames) {
                    IR_LOG_INFO("Auto-profile: {} frames collected, exiting", g_autoProfileFrames);
                    IRWindow::closeWindow();
                }
            }
        );
        renderPipeline.push_back(autoProfileId);
    }

    if (g_autoWarmupFrames > 0) {
        IRVideo::AutoScreenshotConfig cfg{};
        cfg.warmupFrames_ = g_autoWarmupFrames;
        cfg.settleFrames_ = 3;
        if (g_spinYawDegPerSec > 0.0f) {
            // Sweep one full rotation at camera=(0,0). Default zoom=4 matches
            // the rotation-coverage shots (#1261) for scene-scale smoothness;
            // pass --zoom to sweep at high zoom (e.g. 16), where rotation-only
            // parity glitches (#1218 black faces, #1256 checkerboard) are
            // visible at full pixel scale. The regression set baselines both.
            const float sweepZoom = g_initialZoom > 0.0f ? g_initialZoom : 4.0f;
            const int n = IRMath::max(2, g_spinYawShotCount);
            // Reserve up front so push_back never reallocates — moving the
            // label buffer would invalidate the pointers already in
            // g_spinYawShots.
            g_spinYawShotLabels.reserve(n);
            g_spinYawShots.reserve(n);
            for (int i = 0; i < n; ++i) {
                const float yaw = (static_cast<float>(i) / static_cast<float>(n)) * IRMath::kTwoPi;
                auto &label = g_spinYawShotLabels.emplace_back();
                std::snprintf(label.data(), label.size(), "spin_yaw_%03d_of_%03d", i, n);
                g_spinYawShots.push_back({sweepZoom, vec2(0, 0), yaw, label.data()});
            }
            cfg.shots_ = g_spinYawShots.data();
            cfg.numShots_ = static_cast<int>(g_spinYawShots.size());
            IR_LOG_INFO(
                "Spin-yaw sweep: {} shots across one rotation at zoom={}",
                cfg.numShots_,
                sweepZoom
            );
        } else {
            cfg.shots_ = kShots;
            cfg.numShots_ = sizeof(kShots) / sizeof(kShots[0]);
        }
        renderPipeline.push_back(IRVideo::createAutoScreenshotSystem(cfg));
    }

    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}

void initCommands() {
    IRPrefab::Camera::registerStandardKeyboardCommands();
    IRCommand::registerCaptureCommands();
}

void applyCheckerboard(C_VoxelSetNew &voxelSet, Color baseColor) {
    for (int i = 0; i < voxelSet.numVoxels_; ++i) {
        if (voxelSet.voxels_[i].color_.alpha_ == 0)
            continue;
        ivec3 cellPos = ivec3(glm::round(voxelSet.positions_[i].pos_));
        Color c = baseColor;
        if (((cellPos.x + cellPos.y + cellPos.z) & 1) != 0) {
            c.red_ = static_cast<std::uint8_t>(c.red_ * 0.55f);
            c.green_ = static_cast<std::uint8_t>(c.green_ * 0.55f);
            c.blue_ = static_cast<std::uint8_t>(c.blue_ * 0.55f);
        }
        voxelSet.voxels_[i].color_ = c;
    }
}

// Bounding half-extent mirroring the GPU formula in
// system_shapes_to_trixel.hpp / c_shapes_to_trixel.glsl, so that CPU depth
// normalization uses the exact same range as the GPU shader.
// Classic HSV->RGB (h,s,v in [0,1]) matching the shader's hsvToRgb helper.
vec3 hsvToRgbCpu(vec3 c) {
    const vec4 K(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
    vec3 p = glm::abs(glm::fract(vec3(c.x) + vec3(K)) * 6.0f - vec3(K.w));
    return c.z * glm::mix(vec3(K.x), glm::clamp(p - vec3(K.x), 0.0f, 1.0f), c.y);
}

// Color each active voxel by its LOCAL iso-depth (x+y+z), normalized to
// [0,1] across the shape's bounding dExtent.  Matches the GPU depth-color
// path in c_shapes_to_trixel.glsl exactly, so the voxel-pool mirror is
// indistinguishable from the SDF render.
void applyDepthColor(C_VoxelSetNew &voxelSet, IRRender::ShapeType type, vec4 sdfParams) {
    auto sdfType = static_cast<IRMath::SDF::ShapeType>(type);
    vec3 boundingHalf = IRMath::SDF::boundingHalf(sdfType, sdfParams);
    // Match GPU: in iso camera convention, smaller d = closer, so visible
    // window is [-dColor, +dColor/3] and front → t=0 (red), back → t=1.
    float dColor = boundingHalf.x + boundingHalf.y + boundingHalf.z;
    float denom = IRMath::max((4.0f / 3.0f) * dColor, 1.0f);

    for (int i = 0; i < voxelSet.numVoxels_; ++i) {
        if (voxelSet.voxels_[i].color_.alpha_ == 0)
            continue;
        ivec3 cellPos = ivec3(glm::round(voxelSet.positions_[i].pos_));
        float d = static_cast<float>(cellPos.x + cellPos.y + cellPos.z);
        float t = glm::clamp((d + dColor) / denom, 0.0f, 1.0f);
        vec3 rgb = hsvToRgbCpu(vec3(0.66f * t, 1.0f, 1.0f));
        Color c{
            static_cast<std::uint8_t>(glm::clamp(rgb.x, 0.0f, 1.0f) * 255.0f),
            static_cast<std::uint8_t>(glm::clamp(rgb.y, 0.0f, 1.0f) * 255.0f),
            static_cast<std::uint8_t>(glm::clamp(rgb.z, 0.0f, 1.0f) * 255.0f),
            255
        };
        voxelSet.voxels_[i].color_ = c;
    }
}

// Create a voxel-pool entity carved to match an SDF shape.  Allocates a
// centered box of the given halfExtent, then deactivates every voxel whose
// SDF value exceeds the 0.5 surface threshold.
EntityId createVoxelPoolShape(
    vec3 position, IRRender::ShapeType type, vec4 shapeParams, Color color, ivec3 halfExtent
) {
    ivec3 size = halfExtent * 2 + ivec3(1);
    EntityId entity =
        IREntity::createEntity(C_LocalTransform{position}, C_VoxelSetNew{size, color, true});
    auto &vs = IREntity::getComponent<C_VoxelSetNew>(entity);

    auto sdfType = static_cast<IRMath::SDF::ShapeType>(type);
    vec4 sdfParams = IRMath::SDF::effectiveParams(sdfType, shapeParams);

    int activeCount = 0;
    for (int i = 0; i < vs.numVoxels_; ++i) {
        vec3 localPos = vs.positions_[i].pos_;
        float sdf = IRMath::SDF::evaluate(localPos, sdfType, sdfParams);
        if (sdf > IRMath::SDF::kSurfaceThreshold) {
            vs.voxels_[i].deactivate();
        } else {
            ++activeCount;
        }
    }
    // Debug tint is opt-in: --depth-color visualizes z-bands; --checkerboard
    // distinguishes adjacent same-color voxels. Default is plain shape color
    // — the checkerboard path was flickering frame-to-frame and breaking
    // visual regression tests.
    if (g_depthColor) {
        applyDepthColor(vs, type, sdfParams);
    } else if (g_checkerboard) {
        applyCheckerboard(vs, color);
    }
    // The SDF-carving loop above writes alpha directly through
    // `vs.voxels_[i].deactivate()` rather than going through a
    // `C_VoxelSetNew` mutator method, so the pool's per-slot active mask
    // (consumed by `c_voxel_visibility_compact`) is still pinned to the
    // ctor's all-active state. Re-derive it from the live alphas so the
    // compact shader skips carved-away interior slots.
    vs.syncActiveMask();

    IR_LOG_INFO(
        "VoxelPool shape entity={} canvas={} total={} active={}",
        entity,
        vs.canvasEntity_,
        vs.numVoxels_,
        activeCount
    );
    return entity;
}

// Create an SDF shape entity at the given position.
EntityId createSDFShape(vec3 position, IRRender::ShapeType type, vec4 params, Color color) {
    C_ShapeDescriptor desc{type, params, color};
    // Same opt-in debug-tint toggle as createVoxelPoolShape, applied GPU-side
    // via shader flags. Default is no tint — the checkerboard path flickered
    // frame-to-frame on this side too.
    if (g_depthColor) {
        desc.flags_ |= IRRender::SHAPE_FLAG_DEPTH_COLOR;
    } else if (g_checkerboard) {
        desc.flags_ |= IRRender::SHAPE_FLAG_CHECKERBOARD;
    }
    EntityId entity = IREntity::createEntity(C_LocalTransform{position}, desc);
    auto &sd = IREntity::getComponent<C_ShapeDescriptor>(entity);
    IR_LOG_INFO(
        "SDF shape entity={} canvas={} type={} params=({},{},{},{})",
        entity,
        sd.canvasEntity_,
        static_cast<int>(type),
        params.x,
        params.y,
        params.z,
        params.w
    );
    return entity;
}

// Spawn one voxel cube routed through the GPU voxel-position prepass (#1396).
// The fixed 45° SO(3) rotation can only reach the rendered voxels via the
// prepass — UPDATE_VOXEL_SET_CHILDREN folds in translation only — so a rotated
// cube on screen is the smoke test that the prepass computed modelToWorld *
// localPos for every voxel in this set. Opt-in (--gpu-voxel-smoke) so the
// default scene stays byte-identical.
void createGpuVoxelTransformSmoke() {
    const vec3 axis = IRMath::normalize(vec3(0.3f, 0.7f, 0.5f));
    const vec4 rot = IRMath::quatAxisAngle(axis, IRMath::kQuarterPi);
    const vec3 position = vec3(0.0f, 0.0f, -14.0f);
    const ivec3 halfExtent = ivec3(5, 5, 5);
    const ivec3 size = halfExtent * 2 + ivec3(1);

    EntityId entity = IREntity::createEntity(
        C_LocalTransform{position, rot},
        C_VoxelSetNew{size, Color{120, 220, 160, 255}, true}
    );
    auto &vs = IREntity::getComponent<C_VoxelSetNew>(entity);

    // Slot 0 carries this set's SO(3)+translation each frame; every owned voxel
    // points at slot 0 so the prepass transforms it instead of the CPU path.
    constexpr std::uint32_t kSmokeSlot = 0u;
    vs.gpuTransformSlot_ = kSmokeSlot;
    auto &pool = IREntity::getComponent<C_VoxelPool>(vs.canvasEntity_);
    pool.setTransformIndexForRange(
        vs.voxelStartIdx_,
        static_cast<size_t>(vs.numVoxels_),
        kSmokeSlot
    );
    IR_LOG_INFO(
        "GPU voxel-transform smoke entity={} canvas={} voxels={} slot={}",
        entity,
        vs.canvasEntity_,
        vs.numVoxels_,
        kSmokeSlot
    );
}

// ---------------------------------------------------------------------------
// Shape definitions — each entry produces one voxel-pool entity and one
// SDF entity, placed side by side for visual comparison.
// ---------------------------------------------------------------------------

struct ShapeTestCase {
    const char *label_;
    IRRender::ShapeType type_;
    vec4 params_;
    ivec3 halfExtent_;
    Color color_;
};

void initEntities() {
    constexpr float kSpacingX = 16.0f;
    constexpr float kRowSeparationY = 12.0f;

    // params follow C_ShapeDescriptor conventions:
    //   BOX:          (sizeX, sizeY, sizeZ, 0)     — voxel counts per axis
    //   SPHERE:       (radius, radius, radius, 0)
    //   CYLINDER:     (radius, radius, height, 0)   — height = full height
    //   ELLIPSOID:    (2*rx, 2*ry, 2*rz, 0)        — diameters
    //   CONE:         (baseRadius, baseRadius, height, 0)
    //   TORUS:        (majorR, minorR, 0, 0)
    //   WEDGE:        (width, depth, height, 0)     — full sizes like BOX
    //   CURVED_PANEL: (width, depth, thickness, curvature)
    ShapeTestCase cases[] = {
        {"Box 7",
         IRRender::ShapeType::BOX,
         vec4(7, 7, 7, 0),
         ivec3(3, 3, 3),
         Color{100, 200, 220, 255}},

        {"Sphere r4",
         IRRender::ShapeType::SPHERE,
         vec4(4, 4, 4, 0),
         ivec3(5, 5, 5),
         Color{220, 180, 100, 255}},

        {"Cylinder",
         IRRender::ShapeType::CYLINDER,
         vec4(3, 3, 7, 0),
         ivec3(4, 4, 4),
         Color{100, 220, 140, 255}},

        {"Ellipsoid",
         IRRender::ShapeType::ELLIPSOID,
         vec4(8, 6, 4, 0),
         ivec3(5, 4, 3),
         Color{200, 130, 220, 255}},

        {"Cone",
         IRRender::ShapeType::CONE,
         vec4(4, 4, 8, 0),
         ivec3(5, 5, 4),
         Color{220, 140, 100, 255}},

        {"Torus",
         IRRender::ShapeType::TORUS,
         vec4(4, 2, 0, 0),
         ivec3(7, 7, 3),
         Color{100, 180, 220, 255}},

        {"Wedge",
         IRRender::ShapeType::WEDGE,
         vec4(7, 7, 7, 0),
         ivec3(4, 4, 4),
         Color{180, 220, 100, 255}},

        {"CurvedPanel",
         IRRender::ShapeType::CURVED_PANEL,
         vec4(8, 8, 2, 0.5f),
         ivec3(5, 5, 5),
         Color{220, 100, 180, 255}},
    };
    constexpr int kNumCases = sizeof(cases) / sizeof(cases[0]);

    for (int i = 0; i < kNumCases; ++i) {
        auto &tc = cases[i];
        float xPos = i * kSpacingX;

        IR_LOG_INFO("--- {} ---", tc.label_);

        createVoxelPoolShape(
            vec3(xPos, 0.0f, 0.0f),
            tc.type_,
            tc.params_,
            tc.color_,
            tc.halfExtent_
        );

        createSDFShape(vec3(xPos, kRowSeparationY, 0.0f), tc.type_, tc.params_, tc.color_);
    }

    // Co-located trio, coarse-first: zoom1=blue(LOD_4 only), zoom4=green(LOD_2) tops,
    // zoom16=red(LOD_0) tops.
    constexpr float kLodFixtureY = -16.0f;
    constexpr vec4 kLodSphereParams = vec4(3, 3, 3, 0);
    struct LodFixture {
        IRRender::LodLevel lodMin_;
        Color color_;
        const char *label_;
    };
    const LodFixture lodFixtures[] = {
        {IRRender::LodLevel::LOD_4, Color{80, 130, 240, 255}, "LOD_4 (always visible)"},
        {IRRender::LodLevel::LOD_2, Color{80, 240, 100, 255}, "LOD_2 (zoom>=4)"},
        {IRRender::LodLevel::LOD_0, Color{240, 80, 80, 255}, "LOD_0 (zoom>=16 only)"},
    };
    constexpr int kNumLodFixtures = sizeof(lodFixtures) / sizeof(lodFixtures[0]);
    for (int i = 0; i < kNumLodFixtures; ++i) {
        const auto &lf = lodFixtures[i];
        IR_LOG_INFO("--- {} ---", lf.label_);
        C_ShapeDescriptor desc{IRRender::ShapeType::SPHERE, kLodSphereParams, lf.color_};
        desc.lodMin_ = lf.lodMin_;
        IREntity::createEntity(C_LocalTransform{vec3(0.0f, kLodFixtureY, 0.0f)}, desc);
    }

    // Rotation test: SDF shapes with non-identity entity rotation,
    // paired with unrotated copies for visual comparison.
    constexpr float kRotFixtureY = -32.0f;
    constexpr float kRotPairSpacing = 14.0f;
    struct RotFixture {
        const char *label_;
        IRRender::ShapeType type_;
        vec4 params_;
        vec3 axis_;
        float angleDeg_;
        Color color_;
    };
    const RotFixture rotFixtures[] = {
        {"Box 45° Z",
         IRRender::ShapeType::BOX,
         vec4(7, 7, 7, 0),
         vec3(0, 0, 1),
         45.0f,
         Color{100, 200, 220, 255}},
        {"Cylinder 30° Z",
         IRRender::ShapeType::CYLINDER,
         vec4(3, 3, 7, 0),
         vec3(0, 0, 1),
         30.0f,
         Color{100, 220, 140, 255}},
        {"Ellipsoid 45° Y",
         IRRender::ShapeType::ELLIPSOID,
         vec4(8, 6, 4, 0),
         vec3(0, 1, 0),
         45.0f,
         Color{200, 130, 220, 255}},
        {"Cone 60° X",
         IRRender::ShapeType::CONE,
         vec4(4, 4, 8, 0),
         vec3(1, 0, 0),
         60.0f,
         Color{220, 140, 100, 255}},
    };
    constexpr int kNumRotFixtures = sizeof(rotFixtures) / sizeof(rotFixtures[0]);
    for (int i = 0; i < kNumRotFixtures; ++i) {
        const auto &rf = rotFixtures[i];
        float xBase = i * (kRotPairSpacing * 2.0f);
        float angleRad = rf.angleDeg_ * IRMath::kPi / 180.0f;
        vec4 rot = IRMath::quatAxisAngle(rf.axis_, angleRad);

        createSDFShape(vec3(xBase, kRotFixtureY, 0.0f), rf.type_, rf.params_, rf.color_);

        C_ShapeDescriptor desc{rf.type_, rf.params_, rf.color_};
        if (g_depthColor)
            desc.flags_ |= IRRender::SHAPE_FLAG_DEPTH_COLOR;
        else if (g_checkerboard)
            desc.flags_ |= IRRender::SHAPE_FLAG_CHECKERBOARD;
        IREntity::createEntity(
            C_LocalTransform{vec3(xBase + kRotPairSpacing, kRotFixtureY, 0.0f), rot},
            desc
        );
    }

    // Floor so AO / sun-shadow lighting has a surface to fall on. +Z is
    // downward in this iso convention, so shape bottoms sit at max +z ≈ 4
    // (sphere r4, cone h8); floor sits just below at z ≈ 5.
    constexpr float kFloorZ = 5.0f;

    IR_LOG_INFO("--- Floor ---");
    EntityId floorEntity = createSDFShape(
        vec3((kNumCases - 1) * kSpacingX * 0.5f, kRowSeparationY * 0.5f, kFloorZ),
        IRRender::ShapeType::BOX,
        vec4(kNumCases * kSpacingX + 16.0f, kRowSeparationY + 24.0f, 2.0f, 0.0f),
        Color{150, 150, 160, 255}
    );
    IREntity::setComponent(floorEntity, C_LightBlocker{false, false, 0.0f});

    EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    IR_LOG_INFO("Active canvas entity: {}", mainCanvas);

    const ivec2 canvasSize = IREntity::getComponent<C_TriangleCanvasTextures>(mainCanvas).size_;
    IREntity::setComponent(mainCanvas, C_CanvasAOTexture{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasSunShadow{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasLightVolume{});

    // The voxel-pool canvas prefab doesn't include this component, so the
    // AO / sun-shadow / light-volume / lighting systems' archetype filter
    // wouldn't otherwise match the main canvas and they'd silently skip it.
    IREntity::setComponent(mainCanvas, C_TrixelCanvasRenderBehavior{});
    IREntity::setComponent(mainCanvas, C_CanvasFogOfWar{});

    // Default sun direction: high and slightly off-axis so every demo
    // shape casts a visible shadow without any further setup.
    IRRender::setSunDirection(vec3(0.35f, 0.85f, -0.4f));

    // Emissive point light placed between the shape rows so its colored
    // falloff is visible across both the voxel-pool and SDF copies of the
    // nearby shapes. Cyan reads cleanly against the warm shape palette.
    IREntity::createEntity(
        C_LocalTransform{vec3(40.0f, 6.0f, -2.0f)},
        C_LightSource{LightType::EMISSIVE, Color{80, 200, 255, 255}, 2.0f, static_cast<uint8_t>(30)}
    );

    // Seed a visible-circle at origin so the demo's shapes are rendered
    // while the surrounding floor fades to "unexplored" black — proves
    // the fog-of-war pass end-to-end. Radius chosen to cover the shape
    // row (kSpacingX * kNumCases / 2 ≈ 32) with some peripheral margin.
    IRPrefab::Fog::revealRadius(0, 0, 48);

    // --load-vxs: load a DENSE-mode .vxs file (frame 0) and place the voxel
    // set at the origin so it can be compared against the procedural shapes.
    if (!g_loadVxsPath.empty()) {
        auto loaded = IRAsset::loadDenseVoxelSet(g_loadVxsPath);
        if (!loaded.ok()) {
            IR_LOG_ERROR("--load-vxs: could not load '{}'", g_loadVxsPath);
        } else if (loaded.value_.dense_.voxels_.size() != loaded.value_.dense_.voxelCount()) {
            IR_LOG_ERROR("--load-vxs: voxel count mismatch in '{}'", g_loadVxsPath);
        } else {
            auto voxelSet = IRPrefab::DenseVoxel::toComponent(loaded.value_.dense_);
            EntityId vxsEntity = IREntity::createEntity(
                C_LocalTransform{vec3(-20.0f, -8.0f, 0.0f)},
                std::move(voxelSet)
            );
            IR_LOG_INFO(
                "--load-vxs: loaded '{}' -> entity {} ({} voxels)",
                g_loadVxsPath,
                vxsEntity,
                loaded.value_.dense_.voxelCount()
            );
        }
    }

    if (g_gpuVoxelSmoke) {
        IR_LOG_INFO("--- GPU voxel-position prepass smoke (#1396) ---");
        createGpuVoxelTransformSmoke();
    }
}

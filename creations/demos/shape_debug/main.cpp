#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/ir_render.hpp>

#include <cmath>
#include <cstring>
#include <cstdlib>
// COMPONENTS
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/components/component_occupancy_grid.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>

// SYSTEMS
#include <irreden/update/systems/system_update_positions_global.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_build_occupancy_grid.hpp>
#include <irreden/render/systems/system_compute_voxel_ao.hpp>
#include <irreden/render/systems/system_compute_sun_shadow.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_lighting_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_camera_mouse_pan.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>

// COMMAND SUITES
#include <irreden/common/command_suite_camera.hpp>
#include <irreden/common/command_suite_capture.hpp>

namespace {

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, vec2(0, 0), "zoom1_origin"},
    {2.0f, vec2(0, 0), "zoom2_origin"},
    {4.0f, vec2(0, 0), "zoom4_origin"},
    {1.0f, vec2(1, 0), "zoom1_odd_offset"},
    {8.0f, vec2(0, 0), "zoom8_origin"},
    {4.0f, vec2(3, 5), "zoom4_offset_3_5"},
};

int g_autoWarmupFrames = 0;  // 0 = --auto-screenshot not requested
bool g_depthColor = false;
int g_autoProfileFrames = 0;  // 0 = disabled
int g_autoProfileCount = 0;
float g_initialZoom = 0.0f;  // 0 = use engine default
IRRender::DebugOverlayMode g_debugOverlay = IRRender::DebugOverlayMode::NONE;

} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &g_autoWarmupFrames);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--auto-profile") == 0) {
            g_autoProfileFrames = 300;  // default
            if (i + 1 < argc) {
                int frames = std::atoi(argv[i + 1]);
                if (frames > 0) {
                    g_autoProfileFrames = frames;
                    ++i;
                }
            }
        } else if (std::strcmp(argv[i], "--depth-color") == 0) {
            g_depthColor = true;
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
        }
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
        IR_LOG_INFO("Initial zoom: requested={}, actual={} (snapped to nearest power of two)",
                    g_initialZoom, actualZoom.x);
    }
    if (g_debugOverlay != IRRender::DebugOverlayMode::NONE) {
        IRRender::setDebugOverlay(g_debugOverlay);
    }
    IREngine::gameLoop();
    return 0;
}

void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::GLOBAL_POSITION_3D>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>()}
    );
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );

    std::list<IRSystem::SystemId> renderPipeline = {
        IRSystem::createSystem<IRSystem::CAMERA_MOUSE_PAN>(),
        IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
        IRSystem::createSystem<IRSystem::BUILD_OCCUPANCY_GRID>(),
        IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
        IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_2>(),
        IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
        IRSystem::createSystem<IRSystem::COMPUTE_VOXEL_AO>(),
        IRSystem::createSystem<IRSystem::COMPUTE_SUN_SHADOW>(),
        IRSystem::createSystem<IRSystem::COMPUTE_LIGHT_VOLUME>(),
        IRSystem::createSystem<IRSystem::LIGHTING_TO_TRIXEL>(),
        IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
        IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
    };

    if (g_autoProfileFrames > 0) {
        IRSystem::SystemId autoProfileId = IRSystem::createSystem<C_VoxelSetNew>(
            "AutoProfile",
            [](C_VoxelSetNew &) {},
            []() {
                ++g_autoProfileCount;
                if (g_autoProfileCount >= g_autoProfileFrames) {
                    IR_LOG_INFO("Auto-profile: {} frames collected, exiting",
                                g_autoProfileFrames);
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

// ---------------------------------------------------------------------------
// CPU SDF functions — exact mirrors of the GPU versions in
// c_shapes_to_trixel.glsl so the voxel-pool carving matches the analytical
// surface the shader produces.
// ---------------------------------------------------------------------------

float cpuSdfBox(vec3 p, vec3 halfExtents) {
    vec3 d = glm::abs(p) - halfExtents;
    return glm::max(d.x, glm::max(d.y, d.z));
}

float cpuSdfSphere(vec3 p, float radius) {
    return glm::length(p) - radius;
}

float cpuSdfCylinder(vec3 p, float radius, float halfHeight) {
    vec2 d = glm::abs(vec2(glm::length(vec2(p.x, p.y)), p.z)) -
             vec2(radius, halfHeight);
    return glm::min(glm::max(d.x, d.y), 0.0f) +
           glm::length(glm::max(d, vec2(0.0f)));
}

float cpuSdfEllipsoid(vec3 p, vec3 radii) {
    if (radii.x <= 0.0f || radii.y <= 0.0f || radii.z <= 0.0f) return 1.0f;
    float k0 = glm::length(p / radii);
    if (k0 < 1e-6f) return -glm::min(radii.x, glm::min(radii.y, radii.z));
    float k1 = glm::length(p / (radii * radii));
    return k0 * (k0 - 1.0f) / k1;
}

float cpuSdfCone(vec3 p, float baseRadius, float halfHeight) {
    float t = glm::clamp((p.z + halfHeight) / (2.0f * halfHeight), 0.0f, 1.0f);
    float radiusAtZ = baseRadius * (1.0f - t);
    float dRadial = glm::length(vec2(p.x, p.y)) - radiusAtZ;
    float dZ = std::abs(p.z) - halfHeight;
    float dOutside = glm::length(glm::max(vec2(dRadial, dZ), vec2(0.0f)));
    float dInside = glm::min(glm::max(dRadial, dZ), 0.0f);
    return dOutside + dInside;
}

float cpuSdfTorus(vec3 p, float majorR, float minorR) {
    float q = glm::length(vec2(p.x, p.y)) - majorR;
    return glm::length(vec2(q, p.z)) - minorR;
}

float cpuSdfWedge(vec3 p, vec3 halfExtents) {
    float boxD = cpuSdfBox(p, halfExtents);
    float planeD = p.z - halfExtents.z * (1.0f - p.x / glm::max(halfExtents.x, 0.001f));
    return glm::max(boxD, planeD);
}

float cpuSdfCurvedPanel(vec3 p, vec3 halfExtents, float curvature) {
    float nx = p.x / glm::max(halfExtents.x, 0.001f);
    float zMid = curvature * halfExtents.x * nx * nx;
    float dThickness = std::abs(p.z - zMid) - halfExtents.z;
    float dX = std::abs(p.x) - halfExtents.x;
    float dY = std::abs(p.y) - halfExtents.y;
    float dOutside = glm::length(glm::max(vec3(dX, dY, dThickness), vec3(0.0f)));
    float dInside = glm::min(glm::max(dX, glm::max(dY, dThickness)), 0.0f);
    return dOutside + dInside;
}

float cpuEvaluateSDF(vec3 localPos, IRRender::ShapeType type, vec4 params) {
    vec3 halfSize = vec3(params) * 0.5f;
    switch (type) {
        case IRRender::ShapeType::BOX:
            return cpuSdfBox(localPos, halfSize);
        case IRRender::ShapeType::SPHERE:
            return cpuSdfSphere(localPos, params.x);
        case IRRender::ShapeType::CYLINDER:
            return cpuSdfCylinder(localPos, params.x, halfSize.z);
        case IRRender::ShapeType::ELLIPSOID:
            return cpuSdfEllipsoid(localPos, halfSize);
        case IRRender::ShapeType::CONE:
            return cpuSdfCone(localPos, params.x, halfSize.z);
        case IRRender::ShapeType::TORUS:
            return cpuSdfTorus(localPos, params.x, params.y);
        case IRRender::ShapeType::WEDGE:
            return cpuSdfWedge(localPos, halfSize);
        case IRRender::ShapeType::CURVED_PANEL:
            return cpuSdfCurvedPanel(localPos, halfSize, params.w);
        default:
            return cpuSdfBox(localPos, halfSize);
    }
}

void applyCheckerboard(C_VoxelSetNew &voxelSet, Color baseColor) {
    for (int i = 0; i < voxelSet.numVoxels_; ++i) {
        if (voxelSet.voxels_[i].color_.alpha_ == 0) continue;
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
vec3 computeBoundingHalfCpu(IRRender::ShapeType type, vec4 params) {
    // Must mirror the GLSL branch in c_shapes_to_trixel.glsl exactly.
    switch (type) {
        case IRRender::ShapeType::SPHERE:
            return vec3(params.x);
        case IRRender::ShapeType::CYLINDER:
        case IRRender::ShapeType::CONE:
            return vec3(params.x, params.x, params.z * 0.5f);
        case IRRender::ShapeType::TORUS: {
            float xyR = params.x + params.y;
            return vec3(xyR, xyR, params.y);
        }
        case IRRender::ShapeType::CURVED_PANEL: {
            vec3 hs = vec3(params) * 0.5f;
            hs.z += std::abs(params.w) * hs.x;
            return hs;
        }
        default:  // BOX, ELLIPSOID, WEDGE, TAPERED_BOX, CUSTOM_SDF
            return vec3(params) * 0.5f;
    }
}

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
void applyDepthColor(C_VoxelSetNew &voxelSet, IRRender::ShapeType type,
                     vec4 sdfParams) {
    vec3 boundingHalf = computeBoundingHalfCpu(type, sdfParams);
    // Match GPU: in iso camera convention, smaller d = closer, so visible
    // window is [-dColor, +dColor/3] and front → t=0 (red), back → t=1.
    float dColor = boundingHalf.x + boundingHalf.y + boundingHalf.z;
    float denom = std::max((4.0f / 3.0f) * dColor, 1.0f);

    for (int i = 0; i < voxelSet.numVoxels_; ++i) {
        if (voxelSet.voxels_[i].color_.alpha_ == 0) continue;
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
    vec3 position,
    IRRender::ShapeType type,
    vec4 shapeParams,
    Color color,
    ivec3 halfExtent
) {
    ivec3 size = halfExtent * 2 + ivec3(1);
    EntityId entity = IREntity::createEntity(
        C_Position3D{position},
        C_VoxelSetNew{size, color, true}
    );
    auto &vs = IREntity::getComponent<C_VoxelSetNew>(entity);

    // The GPU shader applies effectiveSize = params - 1 for BOX only.
    // Replicate that so our CPU SDF evaluation uses the same half-extents.
    vec4 sdfParams = shapeParams;
    if (type == IRRender::ShapeType::BOX) {
        sdfParams = vec4(vec3(shapeParams) - 1.0f, shapeParams.w);
    }

    int activeCount = 0;
    for (int i = 0; i < vs.numVoxels_; ++i) {
        vec3 localPos = vs.positions_[i].pos_;
        float sdf = cpuEvaluateSDF(localPos, type, sdfParams);
        if (sdf > 0.5f) {
            vs.voxels_[i].deactivate();
        } else {
            ++activeCount;
        }
    }
    if (g_depthColor) {
        applyDepthColor(vs, type, sdfParams);
    }

    IR_LOG_INFO(
        "VoxelPool shape entity={} canvas={} total={} active={}",
        entity, vs.canvasEntity_, vs.numVoxels_, activeCount
    );
    return entity;
}

// Create an SDF shape entity at the given position.
EntityId createSDFShape(
    vec3 position,
    IRRender::ShapeType type,
    vec4 params,
    Color color
) {
    C_ShapeDescriptor desc{type, params, color};
    if (g_depthColor) {
        desc.flags_ |= IRRender::SHAPE_FLAG_DEPTH_COLOR;
    }
    EntityId entity = IREntity::createEntity(C_Position3D{position}, desc);
    auto &sd = IREntity::getComponent<C_ShapeDescriptor>(entity);
    IR_LOG_INFO(
        "SDF shape entity={} canvas={} type={} params=({},{},{},{})",
        entity, sd.canvasEntity_, static_cast<int>(type),
        params.x, params.y, params.z, params.w
    );
    return entity;
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
        {"Box 7",       IRRender::ShapeType::BOX,
         vec4(7, 7, 7, 0),       ivec3(3, 3, 3),
         Color{100, 200, 220, 255}},

        {"Sphere r4",   IRRender::ShapeType::SPHERE,
         vec4(4, 4, 4, 0),       ivec3(5, 5, 5),
         Color{220, 180, 100, 255}},

        {"Cylinder",    IRRender::ShapeType::CYLINDER,
         vec4(3, 3, 7, 0),       ivec3(4, 4, 4),
         Color{100, 220, 140, 255}},

        {"Ellipsoid",   IRRender::ShapeType::ELLIPSOID,
         vec4(8, 6, 4, 0),       ivec3(5, 4, 3),
         Color{200, 130, 220, 255}},

        {"Cone",        IRRender::ShapeType::CONE,
         vec4(4, 4, 8, 0),       ivec3(5, 5, 4),
         Color{220, 140, 100, 255}},

        {"Torus",       IRRender::ShapeType::TORUS,
         vec4(4, 2, 0, 0),       ivec3(7, 7, 3),
         Color{100, 180, 220, 255}},

        {"Wedge",       IRRender::ShapeType::WEDGE,
         vec4(7, 7, 7, 0),       ivec3(4, 4, 4),
         Color{180, 220, 100, 255}},

        {"CurvedPanel", IRRender::ShapeType::CURVED_PANEL,
         vec4(8, 8, 2, 0.5f),    ivec3(5, 5, 5),
         Color{220, 100, 180, 255}},
    };
    constexpr int kNumCases = sizeof(cases) / sizeof(cases[0]);

    for (int i = 0; i < kNumCases; ++i) {
        auto &tc = cases[i];
        float xPos = i * kSpacingX;

        IR_LOG_INFO("--- {} ---", tc.label_);

        createVoxelPoolShape(
            vec3(xPos, 0.0f, 0.0f), tc.type_, tc.params_, tc.color_,
            tc.halfExtent_
        );

        createSDFShape(
            vec3(xPos, kRowSeparationY, 0.0f), tc.type_, tc.params_, tc.color_
        );
    }

    // Floor so AO / sun-shadow lighting has a surface to fall on. +Z is
    // downward in this iso convention, so shape bottoms sit at max +z ≈ 4
    // (sphere r4, cone h8); floor sits just below at z ≈ 5.
    constexpr float kFloorZ = 5.0f;

    IR_LOG_INFO("--- Floor ---");
    createSDFShape(
        vec3((kNumCases - 1) * kSpacingX * 0.5f, kRowSeparationY * 0.5f, kFloorZ),
        IRRender::ShapeType::BOX,
        vec4(kNumCases * kSpacingX + 16.0f, kRowSeparationY + 24.0f, 2.0f, 0.0f),
        Color{150, 150, 160, 255}
    );

    EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    IR_LOG_INFO("Active canvas entity: {}", mainCanvas);

    IREntity::setComponent(mainCanvas, C_OccupancyGrid{256});
    const ivec2 canvasSize =
        IREntity::getComponent<C_TriangleCanvasTextures>(mainCanvas).size_;
    IREntity::setComponent(mainCanvas, C_CanvasAOTexture{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasSunShadow{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasLightVolume{});

    // The voxel-pool canvas prefab doesn't include this component, so the
    // AO / sun-shadow / light-volume / lighting systems' archetype filter
    // wouldn't otherwise match the main canvas and they'd silently skip it.
    IREntity::setComponent(mainCanvas, C_TrixelCanvasRenderBehavior{});

    // Default sun direction: high and slightly off-axis so every demo
    // shape casts a visible shadow without any further setup.
    IRRender::setSunDirection(vec3(0.35f, 0.85f, 0.4f));

    // Emissive point light placed between the shape rows so its colored
    // falloff is visible across both the voxel-pool and SDF copies of the
    // nearby shapes. Cyan reads cleanly against the warm shape palette.
    IREntity::createEntity(
        C_Position3D{vec3(40.0f, 6.0f, -2.0f)},
        C_LightSource{
            LightType::EMISSIVE,
            Color{80, 200, 255, 255},
            2.0f,
            static_cast<uint8_t>(30)
        }
    );
}

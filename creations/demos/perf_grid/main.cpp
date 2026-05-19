#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_script.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/render/camera.hpp>

// Components
#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/update/components/component_periodic_idle.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

// Systems
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>
#include <irreden/render/systems/system_build_light_occlusion_grid.hpp>
#include <irreden/render/systems/system_camera_mouse_pan.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_compute_sun_shadow.hpp>
#include <irreden/render/systems/system_compute_voxel_ao.hpp>
#include <irreden/render/systems/system_fog_to_trixel.hpp>
#include <irreden/render/systems/system_lighting_to_trixel.hpp>
#include <irreden/render/systems/system_perf_stats_overlay.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_screen_residual_rotate.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_text_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/common/systems/system_modifier_decay.hpp>
#include <irreden/update/systems/system_apply_position_offset.hpp>
#include <irreden/update/systems/system_periodic_idle.hpp>
#include <irreden/update/systems/system_periodic_idle_position_offset.hpp>
#include <irreden/update/systems/system_update_positions_global.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>

// Command suites
#include <irreden/common/command_suite_camera.hpp>
#include <irreden/common/command_suite_capture.hpp>

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <numbers>
#include <string>

// IRPerfGrid stress test: voxel_set vs sdf share the same lattice (positions,
// colors, periodic-idle wave). They are *not* lighting-equivalent today:
// BUILD_LIGHT_OCCLUSION_GRID only walks the voxel pool; sdf mode never allocates
// pool voxels, so the occupancy bitfield stays empty and AO + GPU light
// propagation see no occluders. Expect sdf to look brighter / less creased
// than voxel_set until Phase 3 (#428 AO via trixelDistances, #364 SDF in LOS).
// See docs/perf/metal_perf_grid_baseline.md § "IRPerfGrid mode parity".

using namespace IRComponents;
using namespace IREntity;
using namespace IRMath;

namespace {

enum class PerfGridMode {
    VoxelSet,
    Sdf,
};

struct PerfGridSettings {
    PerfGridMode mode_ = PerfGridMode::VoxelSet;
    int gridSize_ = 64;
    float spacing_ = 2.0f;
    float waveAmplitude_ = 6.0f;
    float wavePeriodSeconds_ = 4.0f;
    bool waveOffscreen_ = false;
    float initialZoom_ = 0.5f;
};

struct CliOverrides {
    bool modeSet_ = false;
    PerfGridMode mode_ = PerfGridMode::VoxelSet;
    bool gridSizeSet_ = false;
    int gridSize_ = 64;
    bool zoomSet_ = false;
    float zoom_ = 0.5f;
};

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {0.5f, vec2(0, 0), "fit_grid"},
    {1.0f, vec2(0, 0), "zoom1_origin"},
    // Profiler-overlay regression shot — captures the perf_stats overlay
    // backing T-275 acceptance: future overlay-breaking diffs (font, layout,
    // CPU/GPU readout) trip the render-verify image compare.
    {0.5f, vec2(0, 0), "profiler_overlay"},
};

PerfGridSettings g_settings{};
CliOverrides g_cliOverrides{};
int g_autoProfileFrames = 0;
int g_autoProfileCount = 0;
int g_autoWarmupFrames = 0;

PerfGridMode parseMode(const std::string &value) {
    if (value == "voxel_set" || value == "voxel") {
        return PerfGridMode::VoxelSet;
    }
    if (value == "sdf" || value == "shape") {
        return PerfGridMode::Sdf;
    }
    IR_LOG_WARN("Unknown perf_grid mode '{}'; using voxel_set", value);
    return PerfGridMode::VoxelSet;
}

const char *modeName(PerfGridMode mode) {
    return mode == PerfGridMode::VoxelSet ? "voxel_set" : "sdf";
}

template <typename T> void readLuaValue(sol::table table, const char *key, T &out) {
    sol::object value = table[key];
    if (value.valid() && value.is<T>()) {
        out = value.as<T>();
    }
}

void applyConfigTable() {
    IRScript::LuaScript configScript{IREngine::resolveScriptPath("config.lua").c_str()};
    sol::table perfGrid = configScript.getTable("perf_grid");
    if (!perfGrid.valid()) {
        return;
    }

    std::string mode = modeName(g_settings.mode_);
    readLuaValue(perfGrid, "mode", mode);
    g_settings.mode_ = parseMode(mode);
    readLuaValue(perfGrid, "grid_size", g_settings.gridSize_);
    readLuaValue(perfGrid, "spacing", g_settings.spacing_);
    readLuaValue(perfGrid, "wave_amplitude", g_settings.waveAmplitude_);
    readLuaValue(perfGrid, "wave_period_seconds", g_settings.wavePeriodSeconds_);
    readLuaValue(perfGrid, "wave_offscreen", g_settings.waveOffscreen_);
}

void applyCliOverrides() {
    if (g_cliOverrides.modeSet_) {
        g_settings.mode_ = g_cliOverrides.mode_;
    }
    if (g_cliOverrides.gridSizeSet_) {
        g_settings.gridSize_ = g_cliOverrides.gridSize_;
    }
    if (g_cliOverrides.zoomSet_) {
        g_settings.initialZoom_ = g_cliOverrides.zoom_;
    }
}

void parseArgs(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &g_autoWarmupFrames);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--auto-profile") == 0) {
            g_autoProfileFrames = 300;
            if (i + 1 < argc) {
                int frames = std::atoi(argv[i + 1]);
                if (frames > 0) {
                    g_autoProfileFrames = frames;
                    ++i;
                }
            }
        } else if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            g_cliOverrides.mode_ = parseMode(argv[i + 1]);
            g_cliOverrides.modeSet_ = true;
            ++i;
        } else if (std::strcmp(argv[i], "--grid-size") == 0 && i + 1 < argc) {
            int gridSize = std::atoi(argv[i + 1]);
            if (gridSize > 0) {
                g_cliOverrides.gridSize_ = gridSize;
                g_cliOverrides.gridSizeSet_ = true;
            }
            ++i;
        } else if (std::strcmp(argv[i], "--zoom") == 0 && i + 1 < argc) {
            float zoom = static_cast<float>(std::atof(argv[i + 1]));
            if (zoom > 0.0f) {
                g_cliOverrides.zoom_ = zoom;
                g_cliOverrides.zoomSet_ = true;
            }
            ++i;
        }
    }
}

void validateSettings() {
    g_settings.gridSize_ = std::max(1, g_settings.gridSize_);
    g_settings.spacing_ = std::max(0.25f, g_settings.spacing_);
    g_settings.wavePeriodSeconds_ = std::max(0.1f, g_settings.wavePeriodSeconds_);

    const int poolEdge = IRRender::VoxelPoolConfig::getEdge();
    if (g_settings.mode_ == PerfGridMode::VoxelSet && g_settings.gridSize_ > poolEdge) {
        IR_LOG_WARN(
            "voxel_set mode requested grid_size={}, but the global voxel pool holds only {}^3 "
            "single-voxel entities. Clamping grid_size to {}.",
            g_settings.gridSize_,
            poolEdge,
            poolEdge
        );
        g_settings.gridSize_ = poolEdge;
    }

    const int entityCount = g_settings.gridSize_ * g_settings.gridSize_ * g_settings.gridSize_;
    if (g_settings.mode_ == PerfGridMode::VoxelSet &&
        entityCount == IRRender::VoxelPoolConfig::getTotalSize()) {
        IR_LOG_WARN(
            "perf_grid voxel_set mode will allocate all {} voxels in the global pool; "
            "do not add particle or other voxel-pool consumers to this scene.",
            IRRender::VoxelPoolConfig::getTotalSize()
        );
    }
}

Color colorForCell(int x, int y, int z, int gridSize) {
    const float denom = static_cast<float>(std::max(gridSize - 1, 1));
    return Color{
        static_cast<std::uint8_t>(80 + 120.0f * (static_cast<float>(x) / denom)),
        static_cast<std::uint8_t>(120 + 100.0f * (static_cast<float>(y) / denom)),
        static_cast<std::uint8_t>(160 + 80.0f * (static_cast<float>(z) / denom)),
        255
    };
}

C_PeriodicIdle makeWaveIdle(int x, int y, int z) {
    const float tau = 2.0f * std::numbers::pi_v<float>;
    const float wavelength = std::max(8.0f, static_cast<float>(g_settings.gridSize_) * 0.5f);
    const float phase = tau * static_cast<float>(x + y + z) / wavelength;
    const float amplitude =
        g_settings.waveOffscreen_ ? g_settings.waveAmplitude_ * 6.0f : g_settings.waveAmplitude_;

    C_PeriodicIdle idle{vec3(0.0f, 0.0f, amplitude), g_settings.wavePeriodSeconds_, phase};
    idle.addStageDurationSeconds(
        0.0f,
        g_settings.wavePeriodSeconds_ * 0.5f,
        -1.0f,
        1.0f,
        IREasingFunctions::kSineEaseInOut
    );
    idle.addStageDurationSeconds(
        g_settings.wavePeriodSeconds_ * 0.5f,
        g_settings.wavePeriodSeconds_ * 0.5f,
        1.0f,
        -1.0f,
        IREasingFunctions::kSineEaseInOut
    );
    return idle;
}

vec3 positionForCell(int x, int y, int z) {
    const float center = (static_cast<float>(g_settings.gridSize_) - 1.0f) * 0.5f;
    return (vec3(x, y, z) - vec3(center)) * g_settings.spacing_;
}

void createGridEntities() {
    const int n = g_settings.gridSize_;
    const int expectedEntities = n * n * n;
    IR_LOG_INFO(
        "Creating perf_grid mode={} grid_size={} entity_count={} spacing={} wave_amplitude={} "
        "wave_period={}",
        modeName(g_settings.mode_),
        n,
        expectedEntities,
        g_settings.spacing_,
        g_settings.waveAmplitude_,
        g_settings.wavePeriodSeconds_
    );
    if (g_settings.mode_ == PerfGridMode::Sdf) {
        IR_LOG_INFO(
            "perf_grid sdf: geometry is SDF-only — voxel pool stays empty, so occupancy-driven AO "
            "and light-volume LOS do not see these boxes. Visuals differ from voxel_set; see "
            "docs/perf/metal_perf_grid_baseline.md (IRPerfGrid mode parity)."
        );
    }

    for (int z = 0; z < n; ++z) {
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                const vec3 pos = positionForCell(x, y, z);
                const Color color = colorForCell(x, y, z, n);
                C_PeriodicIdle idle = makeWaveIdle(x, y, z);

                if (g_settings.mode_ == PerfGridMode::VoxelSet) {
                    IREntity::createEntity(
                        C_Position3D{pos},
                        C_VoxelSetNew{ivec3(1, 1, 1), color, false},
                        idle,
                        C_Modifiers{}
                    );
                } else {
                    IREntity::createEntity(
                        C_Position3D{pos},
                        C_ShapeDescriptor{
                            IRRender::ShapeType::BOX,
                            vec4(1.0f, 1.0f, 1.0f, 0.0f),
                            color
                        },
                        idle,
                        C_Modifiers{}
                    );
                }
            }
        }
    }
}

void configureLightingAndCanvas() {
    EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    const ivec2 canvasSize = IREntity::getComponent<C_TriangleCanvasTextures>(mainCanvas).size_;

    IREntity::setComponent(mainCanvas, C_CanvasAOTexture{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasSunShadow{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasLightVolume{});
    IREntity::setComponent(mainCanvas, C_TrixelCanvasRenderBehavior{});
    IREntity::setComponent(mainCanvas, C_CanvasFogOfWar{});

    IRRender::setSunDirection(vec3(0.35f, 0.85f, -0.4f));
    IREntity::createEntity(
        C_Position3D{vec3(0.0f, 0.0f, -64.0f)},
        C_LocalTransform{vec3(0.0f, 0.0f, -64.0f)},
        C_LightSource{
            LightType::EMISSIVE,
            Color{90, 200, 255, 255},
            2.0f,
            static_cast<uint8_t>(180)
        }
    );
    IRPrefab::Fog::revealRadius(0, 0, 128);
}

} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    parseArgs(argc, argv);

    IR_LOG_INFO("Starting creation: perf_grid");
    IREngine::init(argv[0]);
    applyConfigTable();
    applyCliOverrides();
    validateSettings();

    if (g_autoProfileFrames > 0) {
        IREngine::enableFrameTiming(true);
    }

    initSystems();
    initCommands();
    initEntities();

    IRRender::setCameraPosition2DIso(vec2(0.0f, 0.0f));
    IRRender::setCameraZoom(g_settings.initialZoom_);
    IR_LOG_INFO(
        "Initial camera zoom: requested={}, actual={}",
        g_settings.initialZoom_,
        IRRender::getCameraZoom().x
    );

    IREngine::gameLoop();
    return 0;
}

void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::PERIODIC_IDLE>(),
         IRSystem::createSystem<IRSystem::MODIFIER_DECAY>(),
         IRSystem::createSystem<IRSystem::PERIODIC_IDLE_POSITION_OFFSET>(),
         IRSystem::createSystem<IRSystem::GLOBAL_POSITION_3D>(),
         IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
         IRSystem::createSystem<IRSystem::APPLY_POSITION_OFFSET>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>()}
    );
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );

    // perf_grid is the canonical profiler-overlay demo: it ships with both
    // CPU per-stage histogram and GPU timer-query reading enabled, so the
    // PERF_STATS_OVERLAY rendered at the end of the pipeline always carries
    // full timing data. A creation that wants a cheaper run can flip either
    // of these back off through `ir.render.setCpuTimingEnabled(false)` /
    // `setGpuTimingEnabled(false)` from a Lua config.
    IRProfile::cpuFrameHistogram().enabled_ = true;
    IRRender::gpuStageTiming().enabled_ = true;

    std::list<IRSystem::SystemId> renderPipeline = {
        IRSystem::createSystem<IRSystem::CAMERA_MOUSE_PAN>(),
        IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
        IRSystem::createSystem<IRSystem::BUILD_LIGHT_OCCLUSION_GRID>(),
        IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
        IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_2>(),
        IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
        IRSystem::createSystem<IRSystem::COMPUTE_VOXEL_AO>(),
        IRSystem::createSystem<IRSystem::BAKE_SUN_SHADOW_MAP>(),
        IRSystem::createSystem<IRSystem::COMPUTE_SUN_SHADOW>(),
        IRSystem::createSystem<IRSystem::COMPUTE_LIGHT_VOLUME>(),
        IRSystem::createSystem<IRSystem::LIGHTING_TO_TRIXEL>(),
        IRSystem::createSystem<IRSystem::FOG_TO_TRIXEL>(),
        // PERF_STATS_OVERLAY mutates the C_TextSegment of its tracked entity;
        // TEXT_TO_TRIXEL rasterizes the text onto the GUI canvas; the canvas
        // is composited into the framebuffer by TRIXEL_TO_FRAMEBUFFER. Order
        // must be overlay → text → trixel-to-fb for the HUD to land on
        // screen — matches the lighting demo wiring.
        IRSystem::createSystem<IRSystem::PERF_STATS_OVERLAY>(),
        IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>(),
        IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
        IRSystem::createSystem<IRSystem::SCREEN_SPACE_RESIDUAL_ROTATE>(),
    };

    if (g_autoProfileFrames > 0) {
        IRSystem::SystemId autoProfileId = IRSystem::createSystem<C_Name>(
            "AutoProfile",
            [](C_Name &) {},
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
    createGridEntities();
    configureLightingAndCanvas();
}

#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_script.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/profile/scope_timer.hpp>
#include <irreden/render/camera.hpp>

// Components
#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_local_transform.hpp>
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
#include <irreden/render/camera_controls.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_compute_sun_shadow.hpp>
#include <irreden/render/systems/system_compute_voxel_ao.hpp>
#include <irreden/render/systems/system_fog_to_trixel.hpp>
#include <irreden/render/systems/system_lighting_to_trixel.hpp>
#include <irreden/render/systems/system_perf_stats_overlay.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_text_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/common/systems/system_modifier_decay.hpp>
#include <irreden/update/systems/system_periodic_idle.hpp>
#include <irreden/update/systems/system_periodic_idle_position_offset.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>

// Command suites
#include <irreden/common/command_suite_capture.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    // T-287 / B1 verification modes: allocate ONE `C_VoxelSetNew` of
    // size `grid_size`³ instead of per-cell entities, then drive the
    // active-slot mask either dense (all on) or hollow (sphere shell only).
    // Use these together to measure the visibility-compaction cost as a
    // function of active-voxel count while pool-occupancy stays constant.
    DenseSet,
    HollowSet,
};

struct PerfGridSettings {
    PerfGridMode mode_ = PerfGridMode::VoxelSet;
    int gridSize_ = 64;
    float spacing_ = 2.0f;
    float waveAmplitude_ = 6.0f;
    float wavePeriodSeconds_ = 4.0f;
    bool waveOffscreen_ = false;
    float initialZoom_ = 0.5f;
    // Subdivision — config/preset-settable; CLI overrides both.
    // Only applied if explicit_ is true so the engine's built-in default
    // is undisturbed when neither config nor CLI specifies these.
    IRRender::SubdivisionMode subdivisionMode_ = IRRender::SubdivisionMode::FULL;
    bool subdivisionModeExplicit_ = false;
    int baseSubdivisions_ = 1;
    bool baseSubdivisionsExplicit_ = false;
};

struct CliOverrides {
    bool modeSet_ = false;
    PerfGridMode mode_ = PerfGridMode::VoxelSet;
    bool gridSizeSet_ = false;
    int gridSize_ = 64;
    bool zoomSet_ = false;
    float zoom_ = 0.5f;
    bool waveAmplitudeSet_ = false;
    float waveAmplitude_ = 0.0f;
    bool subdivisionModeSet_ = false;
    IRRender::SubdivisionMode subdivisionMode_ = IRRender::SubdivisionMode::FULL;
    bool baseSubdivisionsSet_ = false;
    int baseSubdivisions_ = 1;
    // Accepted and recorded for manifest/cell-ID purposes; ignored until T-221.
    int workerThreads_ = 0;
    std::string configPreset_; // path from --config-preset, empty if absent
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
    if (value == "dense_set") {
        return PerfGridMode::DenseSet;
    }
    if (value == "hollow_set") {
        return PerfGridMode::HollowSet;
    }
    IR_LOG_WARN("Unknown perf_grid mode '{}'; using voxel_set", value);
    return PerfGridMode::VoxelSet;
}

const char *modeName(PerfGridMode mode) {
    switch (mode) {
    case PerfGridMode::VoxelSet:
        return "voxel_set";
    case PerfGridMode::Sdf:
        return "sdf";
    case PerfGridMode::DenseSet:
        return "dense_set";
    case PerfGridMode::HollowSet:
        return "hollow_set";
    }
    return "voxel_set";
}

template <typename T> void readLuaValue(sol::table table, const char *key, T &out) {
    sol::object value = table[key];
    if (value.valid() && value.is<T>()) {
        out = value.as<T>();
    }
}

void applyPerfGridTable(sol::table perfGrid) {
    std::string mode = modeName(g_settings.mode_);
    readLuaValue(perfGrid, "mode", mode);
    g_settings.mode_ = parseMode(mode);
    readLuaValue(perfGrid, "grid_size", g_settings.gridSize_);
    readLuaValue(perfGrid, "spacing", g_settings.spacing_);
    readLuaValue(perfGrid, "wave_amplitude", g_settings.waveAmplitude_);
    readLuaValue(perfGrid, "wave_period_seconds", g_settings.wavePeriodSeconds_);
    readLuaValue(perfGrid, "wave_offscreen", g_settings.waveOffscreen_);
    // zoom, subdivision_mode, and base_subdivisions are also settable from
    // config.lua and preset files (not just CLI flags).
    readLuaValue(perfGrid, "zoom", g_settings.initialZoom_);

    std::string subModeStr;
    readLuaValue(perfGrid, "subdivision_mode", subModeStr);
    if (subModeStr == "none") {
        g_settings.subdivisionMode_ = IRRender::SubdivisionMode::NONE;
        g_settings.subdivisionModeExplicit_ = true;
    } else if (subModeStr == "position_only") {
        g_settings.subdivisionMode_ = IRRender::SubdivisionMode::POSITION_ONLY;
        g_settings.subdivisionModeExplicit_ = true;
    } else if (subModeStr == "full") {
        g_settings.subdivisionMode_ = IRRender::SubdivisionMode::FULL;
        g_settings.subdivisionModeExplicit_ = true;
    }

    int baseSub = 0;
    readLuaValue(perfGrid, "base_subdivisions", baseSub);
    // 0 treated as absent; valid subdivision counts are >= 1.
    if (baseSub > 0) {
        g_settings.baseSubdivisions_ = baseSub;
        g_settings.baseSubdivisionsExplicit_ = true;
    }
}

void applyConfigTable() {
    IRScript::LuaScript configScript{IREngine::resolveScriptPath("config.lua").c_str()};
    sol::table perfGrid = configScript.getTable("perf_grid");
    if (!perfGrid.valid()) {
        return;
    }
    applyPerfGridTable(perfGrid);
}

void applyConfigPreset(const std::string &presetPath) {
    if (presetPath.empty()) {
        return;
    }
    IRScript::LuaScript presetScript{presetPath.c_str()};
    sol::table perfGrid = presetScript.getTable("perf_grid");
    if (!perfGrid.valid()) {
        IR_LOG_WARN("--config-preset '{}': no perf_grid table found, skipping", presetPath);
        return;
    }
    IR_LOG_INFO("Applying config preset: {}", presetPath);
    applyPerfGridTable(perfGrid);
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
    if (g_cliOverrides.waveAmplitudeSet_) {
        g_settings.waveAmplitude_ = g_cliOverrides.waveAmplitude_;
    }
    // CLI subdivision flags supersede config/preset values.
    if (g_cliOverrides.subdivisionModeSet_) {
        g_settings.subdivisionMode_ = g_cliOverrides.subdivisionMode_;
        g_settings.subdivisionModeExplicit_ = true;
    }
    if (g_cliOverrides.baseSubdivisionsSet_) {
        g_settings.baseSubdivisions_ = g_cliOverrides.baseSubdivisions_;
        g_settings.baseSubdivisionsExplicit_ = true;
    }
}

void parseArgs(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &g_autoWarmupFrames);
    g_cliOverrides.configPreset_ = IREngine::parseConfigPresetArg(argc, argv);
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
        } else if (std::strcmp(argv[i], "--wave-amplitude") == 0 && i + 1 < argc) {
            // 0.0 = static scene (no per-frame voxel motion). Useful for
            // isolating per-frame upload cost in profiler runs.
            g_cliOverrides.waveAmplitude_ = static_cast<float>(std::atof(argv[i + 1]));
            g_cliOverrides.waveAmplitudeSet_ = true;
            ++i;
        } else if (std::strcmp(argv[i], "--subdivision-mode") == 0 && i + 1 < argc) {
            std::string mode = argv[i + 1];
            if (mode == "none") {
                g_cliOverrides.subdivisionMode_ = IRRender::SubdivisionMode::NONE;
                g_cliOverrides.subdivisionModeSet_ = true;
            } else if (mode == "position_only") {
                g_cliOverrides.subdivisionMode_ = IRRender::SubdivisionMode::POSITION_ONLY;
                g_cliOverrides.subdivisionModeSet_ = true;
            } else if (mode == "full") {
                g_cliOverrides.subdivisionMode_ = IRRender::SubdivisionMode::FULL;
                g_cliOverrides.subdivisionModeSet_ = true;
            } else {
                IR_LOG_WARN(
                    "Unknown --subdivision-mode '{}'; expected none|position_only|full",
                    mode
                );
            }
            ++i;
        } else if (std::strcmp(argv[i], "--base-subdivisions") == 0 && i + 1 < argc) {
            int sub = std::atoi(argv[i + 1]);
            if (sub > 0) {
                g_cliOverrides.baseSubdivisions_ = sub;
                g_cliOverrides.baseSubdivisionsSet_ = true;
            }
            ++i;
        } else if (std::strcmp(argv[i], "--worker-threads") == 0 && i + 1 < argc) {
            // Accepted for cell-ID purposes by perf_grid_matrix.sh; ignored
            // until T-221 wires enkiTS thread-pool sizing.
            int wt = std::atoi(argv[i + 1]);
            if (wt >= 0) {
                g_cliOverrides.workerThreads_ = wt;
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

    if (g_settings.mode_ == PerfGridMode::DenseSet || g_settings.mode_ == PerfGridMode::HollowSet) {
        // Single big `C_VoxelSetNew` of size n³ centered at the origin — same
        // pool occupancy as voxel_set mode, but every voxel shares the entity
        // so the only thing varying is the active-mask bit pattern. Dense
        // leaves every slot active; hollow drops the interior so the mask
        // covers only the cube's outer shell (~6n² / n³ → ≤10% for n ≥ 60),
        // exercising the T-287 compaction path on the same voxel buffer.
        const Color color = colorForCell(n / 2, n / 2, n / 2, n);
        const ivec3 size{n, n, n};
        const vec3 originOffset{-(n - 1) * 0.5f * g_settings.spacing_};
        EntityId rootEntity = IREntity::createEntity(
            C_LocalTransform{originOffset},
            C_VoxelSetNew{size, color, true},
            C_Modifiers{}
        );
        if (g_settings.mode_ == PerfGridMode::HollowSet) {
            auto setOpt = IREntity::getComponentOptional<C_VoxelSetNew>(rootEntity);
            if (setOpt.has_value()) {
                C_VoxelSetNew &voxelSet = *setOpt.value();
                voxelSet.deactivateAll();
                // Reactivate only the cube's six bounding faces.
                voxelSet.fillPlane(0, 0, color);
                voxelSet.fillPlane(0, n - 1, color);
                voxelSet.fillPlane(1, 0, color);
                voxelSet.fillPlane(1, n - 1, color);
                voxelSet.fillPlane(2, 0, color);
                voxelSet.fillPlane(2, n - 1, color);
            }
        }
        return;
    }

    for (int z = 0; z < n; ++z) {
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                const vec3 pos = positionForCell(x, y, z);
                const Color color = colorForCell(x, y, z, n);
                C_PeriodicIdle idle = makeWaveIdle(x, y, z);

                if (g_settings.mode_ == PerfGridMode::VoxelSet) {
                    IREntity::createEntity(
                        C_LocalTransform{pos},
                        C_VoxelSetNew{ivec3(1, 1, 1), color, false},
                        idle,
                        C_Modifiers{}
                    );
                } else {
                    IREntity::createEntity(
                        C_LocalTransform{pos},
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
    applyConfigPreset(g_cliOverrides.configPreset_);
    applyCliOverrides();

    // entity_count_override in config.lua overrides grid_size when nonzero
    // and the caller hasn't already set --grid-size explicitly.
    if (!g_cliOverrides.gridSizeSet_) {
        const int eco = IREngine::entityCountOverride();
        if (eco > 0) {
            const int cbrtCount = static_cast<int>(std::cbrt(static_cast<double>(eco)) + 0.5);
            if (cbrtCount > 0) {
                g_settings.gridSize_ = cbrtCount;
                IR_LOG_INFO("entity_count_override={} → grid_size={}", eco, cbrtCount);
            }
        }
    }

    validateSettings();

    if (g_autoProfileFrames > 0) {
        IREngine::enableFrameTiming(true);
    }

    if (g_settings.subdivisionModeExplicit_) {
        IRRender::setSubdivisionMode(g_settings.subdivisionMode_);
    }
    if (g_settings.baseSubdivisionsExplicit_) {
        IRRender::setVoxelRenderSubdivisions(g_settings.baseSubdivisions_);
    }

    // Render the GUI canvas at the main canvas resolution (default is half).
    // The text renderer's smallest fontSize is 1 trixel-per-bitmap-pixel, so
    // doubling the GUI trixel grid effectively halves the on-screen glyph
    // height — needed to make the perf overlay legible without dominating
    // the viewport. perf_grid uses no widgets, so the higher-resolution
    // GUI canvas has no cost to other consumers.
    IRRender::setGuiScale(1);

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
         IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>()}
    );
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );

    // PERF_STATS_OVERLAY implicitly enables both timing histograms at
    // beginTick (see system_perf_stats_overlay.hpp). The flip is idempotent;
    // a creation that wants a cheaper run can flip either back off through
    // `ir.render.setCpuTimingEnabled(false)` / `setGpuTimingEnabled(false)`
    // from a Lua config after the overlay's first tick.

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
            // PERF_STATS_OVERLAY mutates the C_TextSegment of its tracked entity;
            // TEXT_TO_TRIXEL rasterizes the text onto the GUI canvas; the canvas
            // is composited into the framebuffer by TRIXEL_TO_FRAMEBUFFER. Order
            // must be overlay → text → trixel-to-fb for the HUD to land on
            // screen — matches the lighting demo wiring.
            IRSystem::createSystem<IRSystem::PERF_STATS_OVERLAY>(),
            IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
        }
    );

    if (g_autoProfileFrames > 0) {
        IRSystem::SystemId autoProfileId = IRSystem::createSystem<C_Name>(
            "AutoProfile",
            [](C_Name &) {},
            []() {
                ++g_autoProfileCount;
                if (g_autoProfileCount >= g_autoProfileFrames) {
                    IR_LOG_INFO("Auto-profile: {} frames collected, exiting", g_autoProfileFrames);
                    // Dump the last completed frame's CPU + GPU per-stage
                    // ms so PR bodies can quote concrete before/after numbers
                    // without screenshotting the HUD. Single-frame value, but
                    // representative at the steady state perf_grid settles
                    // into after warmup.
                    const auto &cpu = IRProfile::cpuFrameHistogram();
                    const auto &gpu = IRRender::gpuStageTiming();
                    IR_LOG_INFO(
                        "Auto-profile stats — FPS:{:.1f} frame:{:.3f}ms",
                        IRTime::renderFps(),
                        IRTime::renderFrameTimeMs()
                    );
                    IR_LOG_INFO(
                        "Auto-profile CPU — voxelStage1:{:.3f} voxelStage2:{:.3f} "
                        "voxelCompact:{:.3f} input:{:.3f} update:{:.3f} render:{:.3f}",
                        cpu.lastFrameMs("voxelStage1"),
                        cpu.lastFrameMs("voxelStage2"),
                        cpu.lastFrameMs("voxelCompact"),
                        cpu.lastFrameMs("input"),
                        cpu.lastFrameMs("update"),
                        cpu.lastFrameMs("render")
                    );
                    // Self-describing per-scope dump: every IR_PROFILE_SCOPE
                    // that ran last frame, sorted by total ms descending. Lets
                    // a profiling pass localize cost inside a system tick
                    // without re-editing this hard-coded list each time.
                    {
                        std::vector<std::pair<std::string_view, double>> scopes;
                        for (const auto &[name, stats] : cpu.lastFrame()) {
                            scopes.emplace_back(name, stats.totalMs_);
                        }
                        std::sort(scopes.begin(), scopes.end(), [](const auto &a, const auto &b) {
                            return a.second > b.second;
                        });
                        for (const auto &[name, ms] : scopes) {
                            IR_LOG_INFO("Auto-profile CPU-scope — {}: {:.3f}ms", name, ms);
                        }
                    }
                    IR_LOG_INFO(
                        "Auto-profile GPU — voxelStage1:{:.3f} voxelStage2:{:.3f} "
                        "voxelCompact:{:.3f}",
                        gpu.voxelStage1Ms_,
                        gpu.voxelStage2Ms_,
                        gpu.voxelCompactMs_
                    );
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
    IRPrefab::Camera::registerStandardKeyboardCommands();
    IRCommand::registerCaptureCommands();
}

void initEntities() {
    createGridEntities();
    configureLightingAndCanvas();
}

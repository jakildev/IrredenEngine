// Lua-driven ECS perf-parity demo. The wave-animation system body lives in
// `main.lua`; cmake/lua_codegen reads the file at build time and emits a
// typed `IRSystem::createSystem<C_LuaWaveState>` specialisation under the
// default CODEGEN mode. Switching the cmake cache var
// `-DIR_LUA_ECS_DEFAULT_MODE=EVAL` flips the same source to register the
// system at runtime via the LuaJIT-backed sol2 dispatch path instead. Both
// builds produce the same populated lattice and tick the wave kernel; the
// only difference is per-row dispatch cost. The C++ baseline lives at
// `creations/demos/perf_grid/main.cpp` — same lattice, same wave shape,
// hand-written `system_periodic_idle.tick()` body for the parity gate.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_script.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/render/camera.hpp>

// Codegen-emitted: C_LuaWaveState struct + Lua binding +
// IRScript::CodegenRegistry::registerCodegenComponents / kDefaultEcsMode /
// CodegenSystemIds. Path is wired by irreden_lua_codegen() in CMakeLists.
#include "lua_perf_grid_codegen.hpp"

// Engine components touched by the demo's grid + render pipeline.
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_local_transform_lua.hpp>
#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/modifier.hpp>
#include <irreden/common/transform_modifier_fields.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

// Prefab systems composed into the demo's pipelines (mirrors perf_grid's
// shape so visual output matches). Each include is required for the
// matching `System<NAME>::create()` specialisation to link.
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>
#include <irreden/render/systems/system_build_light_occlusion_grid.hpp>
#include <irreden/render/camera_controls.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_compute_sun_shadow.hpp>
#include <irreden/render/systems/system_compute_voxel_ao.hpp>
#include <irreden/render/systems/system_fog_to_trixel.hpp>
#include <irreden/render/systems/system_lighting_to_trixel.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>

#include <algorithm>
#include <numbers>
#include <string>

using namespace IRComponents;
using namespace IREntity;
using namespace IRMath;

namespace {

struct LuaPerfGridSettings {
    int gridSize_ = 16;
    float spacing_ = 2.0f;
    float waveAmplitude_ = 6.0f;
    float wavePeriodSeconds_ = 4.0f;
    float initialZoom_ = 0.5f;
};

struct CliOverrides {
    bool gridSizeSet_ = false;
    int gridSize_ = 16;
    bool zoomSet_ = false;
    float zoom_ = 0.5f;
    bool subdivisionModeSet_ = false;
    IRRender::SubdivisionMode subdivisionMode_ = IRRender::SubdivisionMode::FULL;
    bool baseSubdivisionsSet_ = false;
    int baseSubdivisions_ = 1;
};

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {0.5f, vec2(0, 0), 0.0f, "fit_grid"},
    {1.0f, vec2(0, 0), 0.0f, "zoom1_origin"},
};

LuaPerfGridSettings g_settings{};
CliOverrides g_cliOverrides{};
int g_autoProfileFrames = 0;
int g_autoProfileCount = 0;
int g_autoWarmupFrames = 0;

template <typename T> void readLuaValue(sol::table table, const char *key, T &out) {
    sol::object value = table[key];
    if (value.valid() && value.is<T>()) {
        out = value.as<T>();
    }
}

void applyConfigTable(IRScript::LuaScript &configScript) {
    sol::table perfGrid = configScript.getTable("lua_perf_grid");
    if (!perfGrid.valid()) {
        return;
    }
    readLuaValue(perfGrid, "grid_size", g_settings.gridSize_);
    readLuaValue(perfGrid, "spacing", g_settings.spacing_);
    readLuaValue(perfGrid, "wave_amplitude", g_settings.waveAmplitude_);
    readLuaValue(perfGrid, "wave_period_seconds", g_settings.wavePeriodSeconds_);
    readLuaValue(perfGrid, "initial_zoom", g_settings.initialZoom_);
}

void applyCliOverrides() {
    if (g_cliOverrides.gridSizeSet_) {
        g_settings.gridSize_ = g_cliOverrides.gridSize_;
    }
    if (g_cliOverrides.zoomSet_) {
        g_settings.initialZoom_ = g_cliOverrides.zoom_;
    }
}

// Declare lua_perf_grid's custom flags on the engine-owned parser. Must run
// before IREngine::init(argc, argv), which performs the single strict parse of
// engine-common + these flags (see engine/CLAUDE.md "CLI args go through
// IRArgs").
void registerArgs() {
    IRArgs::Parser &args = IREngine::args();
    args.optionalInt(
        "--auto-profile",
        "Per-system frame timing for N frames (default 300 when bare); writes the profile report "
        "on exit",
        300
    );
    args.integer("--grid-size", "Override the grid edge in cells (> 0)", 0);
    args.number("--zoom", "Override the initial camera zoom (> 0)", 0.0f);
    args.string(
        "--subdivision-mode",
        "Voxel subdivision mode override (none|position_only|full)",
        ""
    );
    args.integer(
        "--base-subdivisions",
        "Override the base voxel-render subdivision factor (> 0)",
        0
    );
}

// Read the parsed flags into g_autoProfileFrames + g_cliOverrides. Runs inside
// the lua-bindings callback (which fires during init, after the parse) so
// g_cliOverrides is populated before applyCliOverrides() consumes it. Replaces
// the retired hand-rolled parseArgs.
void applyArgs() {
    const IRArgs::Parser &args = IREngine::args();
    if (args.wasProvided("--auto-profile")) {
        g_autoProfileFrames = args.getInt("--auto-profile");
    }
    if (args.wasProvided("--grid-size")) {
        const int gridSize = args.getInt("--grid-size");
        if (gridSize > 0) {
            g_cliOverrides.gridSize_ = gridSize;
            g_cliOverrides.gridSizeSet_ = true;
        }
    }
    if (args.wasProvided("--zoom")) {
        const float zoom = args.getFloat("--zoom");
        if (zoom > 0.0f) {
            g_cliOverrides.zoom_ = zoom;
            g_cliOverrides.zoomSet_ = true;
        }
    }
    if (args.wasProvided("--subdivision-mode")) {
        const std::string mode = args.getString("--subdivision-mode");
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
            IR_LOG_WARN("Unknown --subdivision-mode '{}'; expected none|position_only|full", mode);
        }
    }
    if (args.wasProvided("--base-subdivisions")) {
        const int sub = args.getInt("--base-subdivisions");
        if (sub > 0) {
            g_cliOverrides.baseSubdivisions_ = sub;
            g_cliOverrides.baseSubdivisionsSet_ = true;
        }
    }
}

void validateSettings() {
    g_settings.gridSize_ = std::max(1, g_settings.gridSize_);
    g_settings.spacing_ = std::max(0.25f, g_settings.spacing_);
    g_settings.wavePeriodSeconds_ = std::max(0.1f, g_settings.wavePeriodSeconds_);
    const int poolEdge = IRRender::VoxelPoolConfig::getEdge();
    if (g_settings.gridSize_ > poolEdge) {
        IR_LOG_WARN(
            "lua_perf_grid grid_size={} exceeds voxel pool {}^3; clamping to {}.",
            g_settings.gridSize_,
            poolEdge,
            poolEdge
        );
        g_settings.gridSize_ = poolEdge;
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

vec3 positionForCell(int x, int y, int z) {
    const float center = (static_cast<float>(g_settings.gridSize_) - 1.0f) * 0.5f;
    return (vec3(x, y, z) - vec3(center)) * g_settings.spacing_;
}

C_LuaWaveState makeWaveState(int x, int y, int z) {
    // Mirrors creations/demos/perf_grid/main.cpp:makeWaveIdle() — same
    // amplitude (vec3(0,0,amp_z)), same phase wavelength, same 2-stage
    // SineEaseInOut setup, same C_PeriodicIdle constructor pre-computed
    // angleIncrementPerTick = 2*pi / period / kFPS.
    const float tau = 2.0f * std::numbers::pi_v<float>;
    const float pi = std::numbers::pi_v<float>;
    const float wavelength = std::max(8.0f, static_cast<float>(g_settings.gridSize_) * 0.5f);
    const float phase = tau * static_cast<float>(x + y + z) / wavelength;
    const float period = g_settings.wavePeriodSeconds_;
    const float angleInc = tau / period / static_cast<float>(IRConstants::kFPS);

    // Field order on the codegen-emitted struct + LuaWaveState.new(...)
    // constructor is alphabetical (T-106 invariant).
    return C_LuaWaveState{
        /* amp_x            */ 0.0f,
        /* amp_y            */ 0.0f,
        /* amp_z            */ g_settings.waveAmplitude_,
        /* angle            */ phase,
        /* angle_inc        */ angleInc,
        /* current_stage    */ 0,
        /* cycle_completed  */ false,
        /* out_x            */ 0.0f,
        /* out_y            */ 0.0f,
        /* out_z            */ 0.0f,
        /* pause_requested  */ false,
        /* paused           */ false,
        /* period           */ period,
        /* resume_countdown */ 0.0f,
        /* s0_end_angle     */ pi,
        /* s0_end_t         */ 1.0f,
        /* s0_reversed      */ false,
        /* s0_start_angle   */ 0.0f,
        /* s0_start_t       */ -1.0f,
        /* s1_end_angle     */ tau,
        /* s1_end_t         */ -1.0f,
        /* s1_reversed      */ false,
        /* s1_start_angle   */ pi,
        /* s1_start_t       */ 1.0f,
        /* tick_count       */ 0,
    };
}

void createGridEntities() {
    const int n = g_settings.gridSize_;
    const int expectedEntities = n * n * n;
    IR_LOG_INFO(
        "Creating lua_perf_grid grid_size={} entity_count={} spacing={} wave_amplitude={} "
        "wave_period={}",
        n,
        expectedEntities,
        g_settings.spacing_,
        g_settings.waveAmplitude_,
        g_settings.wavePeriodSeconds_
    );

    for (int z = 0; z < n; ++z) {
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                IREntity::createEntity(
                    C_LocalTransform{positionForCell(x, y, z)},
                    C_VoxelSetNew{ivec3(1, 1, 1), colorForCell(x, y, z, n), false},
                    C_Modifiers{},
                    makeWaveState(x, y, z)
                );
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
    IRPrefab::Fog::attachToCanvas(mainCanvas, 128);

    IRRender::setSunDirection(vec3(0.35f, 0.85f, -0.4f));
    IREntity::createEntity(
        C_LocalTransform{vec3(0.0f, 0.0f, -64.0f)},
        C_LightSource{
            LightType::EMISSIVE,
            Color{90, 200, 255, 255},
            2.0f,
            // Mirrors the perf_grid C++ baseline (was 180; silently
            // capped at kLightVolumePropagateIterations=32). 24 reads
            // as the same blue glow at default zoom and pairs with the
            // adaptive propagate budget for a perf win on Linux/GL.
            static_cast<uint8_t>(24)
        }
    );
}

// Template helper used only in the CODEGEN branch below — passes ids by
// reference so the `LuaWaveTick` member access is dependent on the
// template parameter and only type-checked when actually instantiated.
// In EVAL builds the codegen tool emits `CodegenSystemIds {}` (empty);
// the discarded `if constexpr` branch never instantiates this template,
// so the missing field never causes a compile error.
template <class Ids> IRSystem::SystemId waveTickFromIds(const Ids &ids) {
    return ids.LuaWaveTick;
}

// CODEGEN-mode wave-tick id resolution. In CODEGEN builds the codegen tool
// emitted `createSystem_LuaWaveTick` and a matching `LuaWaveTick` field on
// `CodegenSystemIds`; in EVAL builds main.lua's runtime
// `IRSystem.registerSystem` returns the dynamic SystemId and parks it as
// the Lua global `LuaWaveTickSysId`.
IRSystem::SystemId resolveLuaWaveTickId(IRScript::LuaScript &script) {
    using IRScript::EcsMode;
    if constexpr (IRScript::CodegenRegistry::kDefaultEcsMode == EcsMode::CODEGEN) {
        const auto ids = IRScript::CodegenRegistry::registerCodegenSystems();
        return waveTickFromIds(ids);
    } else {
        const sol::object obj = script.lua()["LuaWaveTickSysId"];
        if (!obj.valid() || !obj.is<lua_Integer>()) {
            IR_LOG_ERROR(
                "lua_perf_grid: LuaWaveTickSysId missing after main.lua "
                "(EVAL build expects IRSystem.registerSystem to return a "
                "non-zero SystemId and main.lua to assign it to the global)."
            );
            return IRSystem::SystemId{0};
        }
        return static_cast<IRSystem::SystemId>(obj.as<lua_Integer>());
    }
}

void registerLuaBindings() {
    IREngine::registerLuaBindings([](IRScript::LuaScript &script) {
        // Binding callback runs from World::setupLuaBindings (post-manager-
        // construction, pre-gameLoop). Everything sequenced below is safe
        // here: EntityManager / SystemManager / RenderManager are live, the
        // active canvas entity exists, and `IREngine::resolveScriptPath` is
        // wired (g_scriptsDir was set by the surrounding init() call).
        script.bindLuaDrivenEcs();

        // Pre-bind C_LocalTransform so an EVAL-mode wave-tick body could
        // touch it via `arch.C_LocalTransform` if a future revision inlines
        // the position update — currently the wave system writes to its own
        // Lua-defined component only, but the binding is cheap and matches
        // lua_pipeline_demo's component-pack pattern.
        script.registerTypeFromTraits<IRComponents::C_LocalTransform>();

        // T-106..T-108: pre-register every Lua-defined component declared
        // in main.lua as a C++ struct + binding. The runtime `IRComponent.
        // register('LuaWaveState', ...)` call inside main.lua is then
        // idempotent and resolves to the same handle.
        IRScript::CodegenRegistry::registerCodegenComponents(script);

        // Mirror the build-time default into the runtime so unmarked
        // `IRSystem.registerSystem` calls in main.lua follow the same
        // dispatch as the codegen tool emitted.
        script.setEcsDefaultMode(IRScript::CodegenRegistry::kDefaultEcsMode);

        // Apply demo-side config overrides from config.lua (runs against
        // the engine's WorldConfig-style table loader, separate Lua state).
        IRScript::LuaScript demoConfig{IREngine::resolveScriptPath("config.lua").c_str()};
        applyConfigTable(demoConfig);
        // Read CLI flags into g_cliOverrides here — this callback fires during
        // IREngine::init AFTER the parse, so the values are available and land
        // before applyCliOverrides() (CLI wins over config.lua).
        applyArgs();
        applyCliOverrides();
        validateSettings();

        // Run main.lua. In CODEGEN builds the registerSystem call no-ops at
        // runtime (returns 0); in EVAL builds it registers via the dynamic
        // path and parks the SystemId as the `LuaWaveTickSysId` global so
        // resolveLuaWaveTickId can pick it up below.
        script.scriptFile(IREngine::resolveScriptPath("main.lua").c_str());

        const IRSystem::SystemId waveSysId = resolveLuaWaveTickId(script);

        // Bridge: read the wave-eased vec3 out of C_LuaWaveState (written by
        // waveSysId above) and upsert it as the entity's TRANSFORM_TRANSLATION
        // ADD modifier. PROPAGATE_TRANSFORM next folds the modifier into
        // C_WorldTransform.translation_. Mirrors PERIODIC_IDLE_POSITION_OFFSET
        // for the C++ perf_grid demo, but reads the Lua-codegen-produced
        // C_LuaWaveState instead of C_PeriodicIdle. Without this bridge the
        // wave runs but never reaches the rendered position (T-302 / T-300
        // retired the legacy C_PositionOffset3D path, so a creation-side
        // writer is the only way to drive per-frame additive translation).
        const IRSystem::SystemId luaWaveOffsetId =
            IRSystem::createSystem<C_LuaWaveState, C_Modifiers>(
                "LuaWaveStateToOffset",
                [](IREntity::EntityId entity, C_LuaWaveState &wave, C_Modifiers &mods) {
                    IRPrefab::Modifier::upsertBySourceInPlace(
                        mods,
                        IRPrefab::TransformModifier::translationField(),
                        IRComponents::TransformKind::ADD,
                        vec3(wave.out_x_, wave.out_y_, wave.out_z_),
                        entity
                    );
                }
            );

        IRSystem::registerPipeline(
            IRTime::Events::UPDATE,
            {
                waveSysId,
                luaWaveOffsetId,
                IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
                IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
            }
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
            }
        );

        if (g_autoProfileFrames > 0) {
            IRSystem::SystemId autoProfileId = IRSystem::createSystem<C_Name>(
                "AutoProfile",
                [](C_Name &) {},
                []() {
                    ++g_autoProfileCount;
                    if (g_autoProfileCount >= g_autoProfileFrames) {
                        IR_LOG_INFO(
                            "Auto-profile: {} frames collected, exiting",
                            g_autoProfileFrames
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
        IRPrefab::Camera::registerStandardKeyboardCommands();

        createGridEntities();
        configureLightingAndCanvas();

        IRRender::setCameraPosition2DIso(vec2(0.0f, 0.0f));
        IRRender::setCameraZoom(g_settings.initialZoom_);
        IR_LOG_INFO(
            "Initial camera zoom: requested={}, actual={}",
            g_settings.initialZoom_,
            IRRender::getCameraZoom().x
        );
    });
}

} // namespace

int main(int argc, char **argv) {
    registerArgs();
    IR_LOG_INFO(
        "Starting creation: lua_perf_grid (default mode={})",
        IRScript::CodegenRegistry::kDefaultEcsMode == IRScript::EcsMode::CODEGEN ? "CODEGEN"
                                                                                 : "EVAL"
    );
    registerLuaBindings();
    IREngine::init(argc, argv);
    g_autoWarmupFrames = IREngine::args().autoScreenshotWarmupFrames();
    if (g_autoProfileFrames > 0) {
        IREngine::enableFrameTiming(true);
    }
    if (g_cliOverrides.subdivisionModeSet_) {
        IRRender::setSubdivisionMode(g_cliOverrides.subdivisionMode_);
    }
    if (g_cliOverrides.baseSubdivisionsSet_) {
        IRRender::setVoxelRenderSubdivisions(g_cliOverrides.baseSubdivisions_);
    }
    IREngine::gameLoop();
    return 0;
}

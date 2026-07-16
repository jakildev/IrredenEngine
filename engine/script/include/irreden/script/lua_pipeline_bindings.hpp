#ifndef LUA_PIPELINE_BINDINGS_H
#define LUA_PIPELINE_BINDINGS_H

// T-102: Lua bindings for IRSystem::registerPipeline + the SystemName enum.
// `IRSystem.SystemName.LIFETIME` is an integer table (the underlying
// `SystemName` enum is C-style; binding it via sol's `new_enum` would
// re-encode the same integers as a typed usertype but the consumer
// pattern — passing the value through `IRSystem.systemId(...)` and
// then into `IRSystem.registerPipeline` — only ever reads it as
// `lua_Integer`, so the table-of-integers form is simpler and
// matches the pattern already used for `TextAlignH` / `TextAlignV`
// in default's lua_bindings.cpp).
//
// `IRSystem.systemId(SystemName.X)` returns the runtime `SystemId` of a
// prefab system that the C++ creation registered up-front via
// `LuaScript::registerPrefabSystem<N>()`. Calling it for a name the
// creation didn't register raises a Lua error pointing back at the
// missing registration call (mirrors the same fail-fast principle as
// the Lua component-pack — Lua sees only what C++ explicitly wired in).
//
// `IRSystem.registerPipeline(event, {ids})` accepts any mix of:
//   - prefab system ids returned by `IRSystem.systemId(name)`
//   - dynamic system ids returned by `IRSystem.registerSystem({...})`
//   - game-side system ids (the game's own enum table is bound the
//     same way under e.g. `IRGameSystem.GameSystemName`; the engine
//     side stays oblivious).
// Order in the list is execution order. See `engine/system/CLAUDE.md`
// "Pipelines" for the underlying semantics.

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/script/lua_script.hpp>

#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>

namespace IRScript::detail {

// Shared range-check for the pipeline-composition bindings — every one
// takes an `event` integer that must name a valid `IRTime::Events`.
// Raises a Lua-visible error (forwarded via lua_error under
// SOL_EXCEPTIONS_ALWAYS_UNSAFE) naming the binding so the diagnostic
// points at the caller. `fn` is the Lua-facing function name.
inline void requireValidPipelineEvent(const char *fn, lua_Integer event) {
    if (event < static_cast<lua_Integer>(IRTime::UPDATE) ||
        event > static_cast<lua_Integer>(IRTime::END)) {
        throw sol::error{
            std::string{fn} + ": invalid event " + std::to_string(event) +
            " — must be IRTime.UPDATE / RENDER / INPUT / START / END"
        };
    }
}

// Bind `IRTime.UPDATE / RENDER / INPUT / START / END` as integer table
// entries. The underlying `IRTime::Events` enum is C-style; `IR_BIND_TIME`
// derives each key from the enum name via stringization (same pattern as
// `IR_BIND_SYS` below) so the table key stays in sync with the enum.
inline void bindIRTimeEvents(LuaScript &script) {
    sol::state &lua = script.lua();
    if (lua["IRTime"].valid()) {
        return; // idempotent
    }
    sol::table t = lua.create_table();
#define IR_BIND_TIME(name) t[#name] = static_cast<lua_Integer>(IRTime::name)
    IR_BIND_TIME(UPDATE);
    IR_BIND_TIME(RENDER);
    IR_BIND_TIME(INPUT);
    IR_BIND_TIME(START);
    IR_BIND_TIME(END);
#undef IR_BIND_TIME
    lua["IRTime"] = t;
}

// Bind every value in the engine's `SystemName` enum as an integer
// under `IRSystem.SystemName`. Hand-listed against
// `engine/system/include/irreden/system/ir_system_types.hpp` — keep the
// two in sync. A new prefab system added there must also be appended
// here, otherwise `IRSystem.SystemName.NEW_NAME` in Lua resolves to
// nil and `IRSystem.systemId` raises an "unknown system name" error
// even when the C++ side called `registerPrefabSystem<NEW_NAME>()`.
inline void bindSystemNameEnum(LuaScript &script) {
    sol::state &lua = script.lua();
    if (!lua["IRSystem"].valid()) {
        lua["IRSystem"] = lua.create_table();
    }
    if (lua["IRSystem"]["SystemName"].valid()) {
        return; // idempotent
    }
    sol::table t = lua.create_table();
#define IR_BIND_SYS(name) t[#name] = static_cast<lua_Integer>(IRSystem::name)
    IR_BIND_SYS(NULL_SYSTEM);
    IR_BIND_SYS(EXAMPLE);
    IR_BIND_SYS(INPUT_KEY_MOUSE);
    IR_BIND_SYS(INPUT_MIDI_MESSAGE_IN);
    IR_BIND_SYS(OUTPUT_MIDI_MESSAGE_OUT);
    IR_BIND_SYS(INPUT_GAMEPAD);
    IR_BIND_SYS(ENTITY_HOVER_DETECT);
    IR_BIND_SYS(HITBOX_MOUSE_TEST);
    IR_BIND_SYS(HITBOX_MOUSE_TEST_GUI);
    IR_BIND_SYS(SCREEN_VIEW);
    IR_BIND_SYS(VELOCITY_3D);
    IR_BIND_SYS(ACCELERATION_3D);
    IR_BIND_SYS(GRAVITY_3D);
    IR_BIND_SYS(GOTO_3D);
    IR_BIND_SYS(PERIODIC_IDLE);
    IR_BIND_SYS(PERIODIC_IDLE_POSITION_OFFSET);
    IR_BIND_SYS(PERIODIC_IDLE_MIDI_TRIGGER);
    IR_BIND_SYS(PERIODIC_IDLE_NOTE_BURST);
    IR_BIND_SYS(COLLISION_EVENT_CLEAR);
    IR_BIND_SYS(COLLISION_NOTE_PLATFORM);
    IR_BIND_SYS(DISPATCH_LUA_OVERLAP);
    IR_BIND_SYS(REACTIVE_RETURN_3D);
    IR_BIND_SYS(CONTACT_MIDI_TRIGGER);
    IR_BIND_SYS(CONTACT_NOTE_BURST);
    IR_BIND_SYS(CONTACT_TRIGGER_GLOW);
    IR_BIND_SYS(VELOCITY_DRAG);
    IR_BIND_SYS(MIDI_DELAY_PROCESS);
    IR_BIND_SYS(PLANT_GROW);
    IR_BIND_SYS(VOXEL_SQUASH_STRETCH);
    IR_BIND_SYS(UPDATE_VOXEL_SET_CHILDREN);
    IR_BIND_SYS(REBUILD_GRID_VOXELS);
    IR_BIND_SYS(PROPAGATE_CANVAS_ROTATION);
    IR_BIND_SYS(PROPAGATE_TRANSFORM);
    IR_BIND_SYS(PROPAGATE_CHUNK_MEMBERSHIP);
    IR_BIND_SYS(VOXEL_SET_RESHAPER);
    IR_BIND_SYS(VOXEL_POOL);
    IR_BIND_SYS(LIFETIME);
    IR_BIND_SYS(VIDEO_ENCODER);
    IR_BIND_SYS(MIDI_SEQUENCE_OUT);
    IR_BIND_SYS(PARTICLE_SPAWNER);
    IR_BIND_SYS(RHYTHMIC_LAUNCH);
    IR_BIND_SYS(SPAWN_GLOW);
    IR_BIND_SYS(ACTION_ANIMATION);
    IR_BIND_SYS(ANIMATION_COLOR);
    IR_BIND_SYS(ANIMATION_MOTION_COLOR_SHIFT);
    IR_BIND_SYS(SPRING_PLATFORM);
    IR_BIND_SYS(SPRING_COLOR);
    IR_BIND_SYS(SPRITE_ANIMATION_ADVANCE);
    IR_BIND_SYS(BUILD_SPATIAL_INDEX);
    IR_BIND_SYS(MODIFIER_DECAY);
    IR_BIND_SYS(GLOBAL_MODIFIER_DECAY);
    IR_BIND_SYS(LAMBDA_MODIFIER_DECAY);
    IR_BIND_SYS(MODIFIER_RESOLVE_GLOBAL);
    IR_BIND_SYS(MODIFIER_RESOLVE_EXEMPT);
    IR_BIND_SYS(MODIFIER_RESOLVE_LAMBDA);
    IR_BIND_SYS(RENDERING_SCREEN_VIEW);
    IR_BIND_SYS(RENDERING_TILE_SELECTOR);
    IR_BIND_SYS(VOXEL_TO_TRIXEL_STAGE_1);
    IR_BIND_SYS(TRIXEL_TO_TRIXEL);
    IR_BIND_SYS(TRIXEL_TO_FRAMEBUFFER_FRAME_DATA);
    IR_BIND_SYS(TRIXEL_TO_FRAMEBUFFER);
    IR_BIND_SYS(GUI_TEXT_RENDER);
    IR_BIND_SYS(TEXT_TO_TRIXEL);
    IR_BIND_SYS(FRAMEBUFFER_TO_SCREEN);
    IR_BIND_SYS(SPRITE_TO_SCREEN);
    IR_BIND_SYS(DEBUG_OVERLAY);
    IR_BIND_SYS(RENDERING_VELOCITY_2D_ISO);
    IR_BIND_SYS(TEXTURE_SCROLL);
    IR_BIND_SYS(UPDATE_VOXEL_POSITIONS_GPU);
    IR_BIND_SYS(UPDATE_GPU_PARTICLES);
    IR_BIND_SYS(RENDER_GPU_PARTICLES_TO_TRIXEL);
    IR_BIND_SYS(SHAPES_TO_TRIXEL);
    IR_BIND_SYS(BUILD_LIGHT_OCCLUSION_GRID);
    IR_BIND_SYS(COMPUTE_VOXEL_AO);
    IR_BIND_SYS(RESOLVE_PER_AXIS_SCREEN_DEPTH);
    IR_BIND_SYS(BAKE_SUN_SHADOW_MAP);
    IR_BIND_SYS(COMPUTE_SUN_SHADOW);
    IR_BIND_SYS(COMPUTE_LIGHT_VOLUME);
    IR_BIND_SYS(LIGHTING_TO_TRIXEL);
    IR_BIND_SYS(FOG_TO_TRIXEL);
    IR_BIND_SYS(CAMERA_MOUSE_PAN);
    IR_BIND_SYS(DEBUG_CULLING_MINIMAP);
    IR_BIND_SYS(PERF_STATS_OVERLAY);
    IR_BIND_SYS(ENTITY_CANVAS_TO_FRAMEBUFFER);
    IR_BIND_SYS(WIDGET_LUA_DISPATCH);
#undef IR_BIND_SYS
    lua["IRSystem"]["SystemName"] = t;
}

// T-223: bind `IRSystem::Concurrency` as an integer table at
// `IRSystem.Concurrency.{SERIAL, PARALLEL_FOR, MAIN_THREAD}`. The same
// rationale as `IRSystem.SystemName` — strings drift; enum-keyed table
// stays in sync with the C++ definition. Used by the optional
// `concurrency` field on `IRSystem.registerSystem({...})` (EVAL +
// CODEGEN paths) so Lua code can request a policy that mirrors the
// hand-written C++ `createSystem<...>(...)` trailing `Concurrency` arg.
inline void bindConcurrencyEnum(LuaScript &script) {
    sol::state &lua = script.lua();
    if (!lua["IRSystem"].valid()) {
        lua["IRSystem"] = lua.create_table();
    }
    if (lua["IRSystem"]["Concurrency"].valid()) {
        return; // idempotent
    }
    sol::table t = lua.create_table();
#define IR_BIND_CONCURRENCY(name) t[#name] = static_cast<lua_Integer>(IRSystem::Concurrency::name)
    IR_BIND_CONCURRENCY(SERIAL);
    IR_BIND_CONCURRENCY(PARALLEL_FOR);
    IR_BIND_CONCURRENCY(MAIN_THREAD);
#undef IR_BIND_CONCURRENCY
    lua["IRSystem"]["Concurrency"] = t;
}

// Bind `IRSystem.systemId(name)` and `IRSystem.registerPipeline(event, ids)`.
// `prefabSystemIds` is the per-LuaScript map populated by C++-side
// `registerPrefabSystem<N>()` calls; passed by pointer so the closure
// reads the live map rather than a snapshot.
inline void bindRegisterPipelineAndSystemId(
    LuaScript &script, const std::unordered_map<int, IRSystem::SystemId> *prefabSystemIds
) {
    sol::state &lua = script.lua();
    if (!lua["IRSystem"].valid()) {
        lua["IRSystem"] = lua.create_table();
    }
    if (lua["IRSystem"]["systemId"].valid()) {
        return; // idempotent
    }

    lua["IRSystem"]["systemId"] = [prefabSystemIds](lua_Integer name) -> lua_Integer {
        auto it = prefabSystemIds->find(static_cast<int>(name));
        if (it == prefabSystemIds->end()) {
            throw sol::error{
                "IRSystem.systemId: unknown SystemName " + std::to_string(name) +
                " — the C++ creation must call "
                "LuaScript::registerPrefabSystem<...>() for this prefab system "
                "before main.lua references it"
            };
        }
        return static_cast<lua_Integer>(it->second);
    };

    lua["IRSystem"]["registerPipeline"] = [](lua_Integer event, sol::table ids) {
        requireValidPipelineEvent("IRSystem.registerPipeline", event);
        std::list<IRSystem::SystemId> pipeline;
        for (auto &kv : ids) {
            sol::optional<lua_Integer> id = kv.second.as<sol::optional<lua_Integer>>();
            if (!id) {
                throw sol::error{
                    "IRSystem.registerPipeline: every entry in the system list must be "
                    "a SystemId integer (returned by IRSystem.systemId or "
                    "IRSystem.registerSystem)"
                };
            }
            pipeline.push_back(static_cast<IRSystem::SystemId>(*id));
        }
        IRSystem::registerPipeline(static_cast<IRTime::Events>(event), std::move(pipeline));
    };

    // #1814: clear an event's pipeline (no systems run for it). The scene-
    // transition counterpart to registerPipeline — a Lua scene machine clears
    // the previous scene's pipeline, then registers the next scene's.
    //
    //     IRSystem.clearPipeline(IRTime.UPDATE)
    lua["IRSystem"]["clearPipeline"] = [](lua_Integer event) {
        requireValidPipelineEvent("IRSystem.clearPipeline", event);
        IRSystem::clearPipeline(static_cast<IRTime::Events>(event));
    };

    // T-224: groups-aware variant. Outer table is the sequence of
    // groups, each inner table a parallel group. The cross-system
    // validator runs at `World::start()` (engine-side hook); a typo
    // that lands a conflicting group raises a Lua-visible
    // std::runtime_error at start time.
    //
    //     IRSystem.registerPipelineGroups(IRTime.UPDATE, {
    //         { sysA, sysB },   -- parallel group
    //         { sysC },         -- serial
    //     })
    lua["IRSystem"]["registerPipelineGroups"] = [](lua_Integer event, sol::table groups) {
        requireValidPipelineEvent("IRSystem.registerPipelineGroups", event);
        std::vector<std::vector<IRSystem::SystemId>> built;
        built.reserve(groups.size());
        for (auto &outerKv : groups) {
            sol::optional<sol::table> innerOpt = outerKv.second.as<sol::optional<sol::table>>();
            if (!innerOpt) {
                throw sol::error{"IRSystem.registerPipelineGroups: each group must be a table of "
                                 "SystemId integers"};
            }
            std::vector<IRSystem::SystemId> group;
            for (auto &innerKv : *innerOpt) {
                sol::optional<lua_Integer> id = innerKv.second.as<sol::optional<lua_Integer>>();
                if (!id) {
                    throw sol::error{
                        "IRSystem.registerPipelineGroups: every entry in a group must be "
                        "a SystemId integer (returned by IRSystem.systemId or "
                        "IRSystem.registerSystem)"
                    };
                }
                group.push_back(static_cast<IRSystem::SystemId>(*id));
            }
            built.push_back(std::move(group));
        }
        IRSystem::registerPipelineGroups(static_cast<IRTime::Events>(event), std::move(built));
    };

    // #1540: append a single system onto an ALREADY-REGISTERED event
    // pipeline as its own serial group, WITHOUT replacing the systems
    // already there. registerPipeline / registerPipelineGroups replace
    // the event's whole list; this composes. The supported path when the
    // C++ pipeline is built before the script runs (e.g. the midi
    // runtime's initSystems() runs before main.lua) and Lua wants to add
    // one UPDATE / RENDER system.
    //
    //     IRSystem.appendSystem(IRTime.UPDATE, luaSysId)
    lua["IRSystem"]["appendSystem"] = [](lua_Integer event, lua_Integer sysId) {
        requireValidPipelineEvent("IRSystem.appendSystem", event);
        IRSystem::appendToPipeline(
            static_cast<IRTime::Events>(event),
            static_cast<IRSystem::SystemId>(sysId)
        );
    };

    // #1540: position-aware variants — insert `sysId` as its own serial
    // group immediately before / after `anchorId` (a SystemId already in
    // `event`'s pipeline). Same single-system, own-group semantics as
    // appendSystem; asserts in debug if the anchor isn't in the pipeline
    // or the system is already present (release: bad anchor front-inserts,
    // duplicate ticks twice).
    //
    //     local anchor = IRSystem.systemId(IRSystem.SystemName.LIFETIME)
    //     IRSystem.insertSystemBefore(IRTime.UPDATE, luaSysId, anchor)
    //     IRSystem.insertSystemAfter(IRTime.UPDATE, luaSysId, anchor)
    lua["IRSystem"]["insertSystemBefore"] =
        [](lua_Integer event, lua_Integer sysId, lua_Integer anchorId) {
            requireValidPipelineEvent("IRSystem.insertSystemBefore", event);
            IRSystem::insertIntoPipelineBefore(
                static_cast<IRTime::Events>(event),
                static_cast<IRSystem::SystemId>(sysId),
                static_cast<IRSystem::SystemId>(anchorId)
            );
        };

    lua["IRSystem"]["insertSystemAfter"] =
        [](lua_Integer event, lua_Integer sysId, lua_Integer anchorId) {
            requireValidPipelineEvent("IRSystem.insertSystemAfter", event);
            IRSystem::insertIntoPipelineAfter(
                static_cast<IRTime::Events>(event),
                static_cast<IRSystem::SystemId>(sysId),
                static_cast<IRSystem::SystemId>(anchorId)
            );
        };

    // #2404: per-system update cadence. A Lua throttle policy sets a
    // system to run 1-in-N phase ticks (`setSystemCadence`), staggers its
    // initial phase (`setSystemCadenceOffset`), and — for a throttled
    // Lua-registered system that must stay numerically correct at the
    // reduced rate — reads how many phase ticks / how much fixed-step
    // time its current execution covers (`getAccumulatedTicks` /
    // `accumulatedDeltaTime`, amendment 2). `sysId` is any SystemId (from
    // IRSystem.systemId or IRSystem.registerSystem).
    lua["IRSystem"]["setSystemCadence"] = [](lua_Integer sysId, lua_Integer cadence) {
        if (cadence < 1) {
            throw sol::error{"IRSystem.setSystemCadence: cadence must be >= 1 (1 = every tick)"};
        }
        IRSystem::setSystemCadence(
            static_cast<IRSystem::SystemId>(sysId),
            static_cast<std::uint32_t>(cadence)
        );
    };
    lua["IRSystem"]["getSystemCadence"] = [](lua_Integer sysId) -> lua_Integer {
        return static_cast<lua_Integer>(
            IRSystem::getSystemCadence(static_cast<IRSystem::SystemId>(sysId))
        );
    };
    lua["IRSystem"]["setSystemCadenceOffset"] = [](lua_Integer sysId, lua_Integer offset) {
        if (offset < 0) {
            throw sol::error{"IRSystem.setSystemCadenceOffset: offset must be >= 0"};
        }
        IRSystem::setSystemCadenceOffset(
            static_cast<IRSystem::SystemId>(sysId),
            static_cast<std::uint32_t>(offset)
        );
    };
    lua["IRSystem"]["getSystemCadenceOffset"] = [](lua_Integer sysId) -> lua_Integer {
        return static_cast<lua_Integer>(
            IRSystem::getSystemCadenceOffset(static_cast<IRSystem::SystemId>(sysId))
        );
    };
    lua["IRSystem"]["getAccumulatedTicks"] = [](lua_Integer sysId) -> lua_Integer {
        return static_cast<lua_Integer>(
            IRSystem::getAccumulatedTicks(static_cast<IRSystem::SystemId>(sysId))
        );
    };
    lua["IRSystem"]["accumulatedDeltaTime"] = [](lua_Integer sysId) -> double {
        return IRSystem::accumulatedDeltaTime(static_cast<IRSystem::SystemId>(sysId));
    };
}

} // namespace IRScript::detail

#endif /* LUA_PIPELINE_BINDINGS_H */

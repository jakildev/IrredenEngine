#pragma once

#include <irreden/ir_entity.hpp>

namespace IRScript {

/// Discriminant tag for typed Lua configuration values (@ref LuaValue).
/// `INTEGER` is an engine extension — Lua integers are a subtype of `number`;
/// the engine exposes them as a distinct type for clarity.
enum LuaType {
    NIL,
    BOOLEAN,
    INTEGER, // DEFINED BY IRREDEN
    NUMBER,
    STRING,
    ENUM,
    USERDATA,
    FUNCTION,
    THREAD,
    TABLE
};

/// Thin wrapper around an `EntityId` for passing entities across the Lua/C++ boundary.
/// sol2 registers this type so Lua scripts can receive and hold entity handles.
struct LuaEntity {
    IREntity::EntityId entity;

    LuaEntity(IREntity::EntityId entity)
        : entity{entity} {}

    LuaEntity()
        : entity{0} {}
};

/// Resolution mode for `IRSystem.registerSystem({...})` calls without an
/// explicit `mode = "..."` field. Set per-creation via
/// `LuaScript::setEcsDefaultMode()`; the codegen tool emits a matching
/// `kDefaultEcsMode` constant from the build-time
/// `IR_LUA_ECS_DEFAULT_MODE` cache var so build-side and runtime-side
/// dispatch agree. T-108 architect plan; see `engine/script/CLAUDE.md`
/// "Lua-driven ECS modes".
enum class EcsMode {
    CODEGEN, ///< Default. Runtime registration is a no-op; the codegen-emitted
             ///< `createSystem_<NAME>()` already created the system.
    EVAL,    ///< Runtime registers via `IRSystem::createSystemDynamic` (the existing T-100/T-101
             ///< path); body is hot-reloadable via `IRSystem.replaceSystemBody`.
};

/// Callable type that constructs a single component from entity-creation parameters.
template <typename Component>
using ComponentFunction = std::function<Component(IREntity::CreateEntityCallbackParams)>;

/// Callable type that spawns a batch of entities given per-component factory functions.
/// Limited to up to 12 component types (hardcoded in the batch-create binding).
template <typename... Components>
using EntityBatchFunction = std::function<std::vector<LuaEntity>(ComponentFunction<Components>...)>;

} // namespace IRScript
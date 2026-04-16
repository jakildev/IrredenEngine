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

/// Callable type that constructs a single component from entity-creation parameters.
template <typename Component>
using ComponentFunction = std::function<Component(IREntity::CreateEntityCallbackParams)>;

/// Callable type that spawns a batch of entities given per-component factory functions.
/// Limited to up to 12 component types (hardcoded in the batch-create binding).
template <typename... Components>
using EntityBatchFunction = std::function<std::vector<LuaEntity>(ComponentFunction<Components>...)>;

} // namespace IRScript
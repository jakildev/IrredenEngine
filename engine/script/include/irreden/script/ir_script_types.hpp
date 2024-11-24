#pragma once

#include <irreden/ir_entity.hpp>

namespace IRScript {

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

     struct LuaEntity {
        IREntity::EntityId entity;
    };

    template <typename Component>
    using ComponentFunction = std::function<Component(IREntity::CreateEntityCallbackParams)>;

    template <typename... Components>
    using EntityBatchFunction = std::function<std::vector<LuaEntity>(ComponentFunction<Components>...)>;

} // namespace IRScript
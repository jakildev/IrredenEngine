#pragma once

#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/script/ir_script_types.hpp>
#include <irreden/script/lua_value.hpp>
#include <irreden/script/lua_config.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {

// template <typename... Components>
// std::vector<LuaEntity> wrapCreateEntityBatchWithFunctions(
//     IRMath::ivec3 partitions,
//     sol::protected_function functions...
// ) {

//     return [](IRMath::ivec3 partitions, sol::protected_function functions...) {
//         std::vector<IREntity::EntityId> entities = IREntity::createEntityBatchWithFunctions(
//             partitions,
//             wrapLuaFunction<Components>(functions)...
//         );

//         std::vector<LuaEntity> luaEntities;
//         luaEntities.resize(entities.size());
//         for (int i = 0; i < entities.size(); i++) {
//             luaEntities[i].entity = entities[i];
//         }
//         return luaEntities;
//     };

// }

// Wrapper for wrapping Lua functions based on the component type
// template<typename Component>
// auto wrapLuaFunction(sol::protected_function luaFunc) {
//     // Wrap the Lua function in a way specific to each component
//     return [luaFunc](auto... args) {
//         return luaFunc(args...);
//     };
// }

} // namespace IRScript

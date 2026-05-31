#ifndef LUA_SPATIAL_BINDINGS_H
#define LUA_SPATIAL_BINDINGS_H

#include <irreden/ir_math.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/spatial/spatial_query.hpp>

#include <sol/sol.hpp>

#include <cstddef>
#include <vector>

namespace IRScript::detail {

// Exposes IRSpatial.queryRadius(center, radius) -> array of {id, x, y, z}
// records over the C_SpatialIndex singleton, mirroring the IR<Module> Lua
// table convention (IRModifier, IRSystem, ...). The position is returned
// INLINE on each record so the Lua caller resolves neighbours without a
// per-candidate foreign read (IREntity.getLuaField per pair) — the footgun
// the spatial subsystem exists to remove. `id` is a raw EntityId integer,
// the same shape arch.entityAt(i) / IRModifier.add already pass around.
//
// queryAabb is intentionally C++-only in v1 — rect-region Lua queries are a
// future extension; nothing in the #1354 motivation needed them.
inline void bindSpatialApi(LuaScript &script) {
    sol::state &lua = script.lua();
    if (!lua["IRSpatial"].valid()) {
        lua["IRSpatial"] = lua.create_table();
    }

    // The scratch vector is captured (not local) so the C++ query stays
    // allocation-free after warm-up; the only unavoidable per-call cost is
    // the Lua result table sol2 must build at the boundary. `&script`
    // captures the LuaScript referent, stable for the script's lifetime.
    lua["IRSpatial"]["queryRadius"] =
        [&script,
         hits = std::vector<IRPrefab::Spatial::SpatialHit>{}](
            sol::object centerObj, double radius
        ) mutable -> sol::table {
        sol::state_view sv{script.lua().lua_state()};
        const IRMath::vec3 center = IRScript::vec3FromLua(centerObj);
        IRPrefab::Spatial::queryRadius(center, static_cast<float>(radius), hits);

        sol::table result = sv.create_table(static_cast<int>(hits.size()), 0);
        for (std::size_t i = 0; i < hits.size(); ++i) {
            sol::table record = sv.create_table(0, 4);
            record["id"] = static_cast<lua_Integer>(hits[i].id_);
            record["x"] = hits[i].pos_.x;
            record["y"] = hits[i].pos_.y;
            record["z"] = hits[i].pos_.z;
            result[static_cast<lua_Integer>(i + 1)] = record; // Lua arrays 1-indexed
        }
        return result;
    };
}

} // namespace IRScript::detail

#endif /* LUA_SPATIAL_BINDINGS_H */

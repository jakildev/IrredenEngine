#ifndef COMPONENT_VOXEL_SET_LUA_H
#define COMPONENT_VOXEL_SET_LUA_H

#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_VoxelSetNew> = true;

// The 3-arg Lua ctor takes an `EntityAnchor`; the C++ `bool` overload is
// deliberately not bound.
//
// Registering both would put a `bool` and an integer-backed enum in one sol2
// overload set, where a Lua boolean and a Lua integer are mutually
// convertible at the binding boundary — so which arm a 3-arg call binds
// depends on declaration order rather than on the value's type, and picking
// wrong is SILENT: `false` would construct anchor 0 and `true` anchor 1,
// which coincidentally match CORNER/CENTER, so the bug would surface only
// once a third anchor is passed as a boolean-ish value.
//
// The C++ `bool centerAroundOrigin` ctor stays for C++ callers; only the Lua
// surface is anchor-only.
//
// The **4-arg** form appends the ctor's `targetCanvas`, which selects the
// canvas whose pool the set allocates from instead of the *active* one. That
// is what makes the Lua surface reachable headlessly: the 2- and 3-arg forms
// route through the asserting `IRPrefab::VoxelPool::activeCanvasEntity()` and
// so need a live RenderManager, while a test that creates a `C_VoxelPool`
// canvas itself can pass that entity in and assert the placement a Lua caller
// actually gets (`test/script/lua_entity_anchor_test.cpp`) — the "silently
// binds the wrong arm" failure is only observable through a constructed set's
// baked positions.
template <> inline void bindLuaType<IRComponents::C_VoxelSetNew>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_VoxelSetNew,
        IRComponents::C_VoxelSetNew(
            IRMath::ivec3,
            IRMath::Color,
            IRComponents::EntityAnchor,
            IREntity::EntityId
        ),
        IRComponents::C_VoxelSetNew(IRMath::ivec3, IRMath::Color, IRComponents::EntityAnchor),
        IRComponents::C_VoxelSetNew(IRMath::ivec3, IRMath::Color)>("C_VoxelSetNew");
}
} // namespace IRScript

#endif /* COMPONENT_VOXEL_SET_LUA_H */

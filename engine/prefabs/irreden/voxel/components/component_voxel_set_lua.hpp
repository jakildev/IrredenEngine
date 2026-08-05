#ifndef COMPONENT_VOXEL_SET_LUA_H
#define COMPONENT_VOXEL_SET_LUA_H

#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_VoxelSetNew> = true;

// The 3-arg Lua ctor takes an `EntityAnchor`, NOT the legacy `bool` (#2563).
//
// Registering both would put a `bool` and an integer-backed enum in one sol2
// overload set, where a Lua boolean and a Lua integer are mutually
// convertible at the binding boundary — so which arm a 3-arg call binds
// depends on declaration order rather than on the value's type, and picking
// wrong is SILENT: `false` would construct anchor 0 and `true` anchor 1,
// which coincidentally match CORNER/CENTER, so the bug would surface only
// once a third anchor is passed as a boolean-ish value.
//
// That ambiguity cannot be pinned by a headless test either — the ctor
// allocates from the *active* canvas via the asserting
// `IRPrefab::VoxelPool::activeCanvasEntity()`, so constructing one from Lua
// needs a live RenderManager. An untestable ambiguity is not worth carrying
// for an arm nothing calls: a tree-wide sweep of every `C_VoxelSetNew.new`
// in a `.lua` file (3 sites, all in `creations/demos/default/`) found all of
// them on the 2-arg form and none passing a bool. C++ back-compat is
// untouched — the `bool centerAroundOrigin` ctor is still there for the 78
// C++ call sites; only the Lua surface is anchor-only.
template <> inline void bindLuaType<IRComponents::C_VoxelSetNew>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_VoxelSetNew,
        IRComponents::C_VoxelSetNew(IRMath::ivec3, IRMath::Color, IRComponents::EntityAnchor),
        IRComponents::C_VoxelSetNew(IRMath::ivec3, IRMath::Color)>("C_VoxelSetNew");
}
} // namespace IRScript

#endif /* COMPONENT_VOXEL_SET_LUA_H */

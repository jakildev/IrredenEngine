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
// An ambiguity nothing calls is not worth carrying: a tree-wide sweep of
// every `C_VoxelSetNew.new` in a `.lua` file (3 sites, all in
// `creations/demos/default/`) found all of them on the 2-arg form and none
// passing a bool. C++ back-compat is untouched — the `bool centerAroundOrigin`
// ctor is still there for the 78 C++ call sites; only the Lua surface is
// anchor-only.
//
// The **4-arg** form appends the ctor's `targetCanvas`, which selects the
// canvas whose pool the set allocates from instead of the *active* one. That
// is what makes the Lua surface reachable headlessly: the 2- and 3-arg forms
// route through the asserting `IRPrefab::VoxelPool::activeCanvasEntity()` and
// so need a live RenderManager, while a test that creates a `C_VoxelPool`
// canvas itself can pass that entity in and assert the placement a Lua caller
// actually gets (`test/script/lua_entity_anchor_test.cpp`). Without it the
// anchor-only decision above would be an untested assertion — the exact
// "silently binds the wrong arm" failure it exists to prevent is only
// observable through a constructed set's baked positions.
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

#ifndef LUA_WORLD_SNAPSHOT_BINDINGS_H
#define LUA_WORLD_SNAPSHOT_BINDINGS_H

#include <irreden/script/lua_script.hpp>
#include <irreden/world/world_snapshot.hpp>

#include <irreden/ir_profile.hpp>

#include <sol/sol.hpp>

#include <string>

namespace IRScript::detail {

// Exposes the ECS world snapshot (persist P7, #2218, epic #667) as the
// `IRPersist` Lua table: whole-world binary save/load over the process-default
// SaveRegistry (IRWorld::makeDefaultSaveRegistry). Include-only glue mirroring
// bindRenderGlue / bindAudioApi — every binding is a thin forward to an
// IRWorld:: entry point; no persistence logic lives here, and there is no link
// edge to IrredenEngineWorld (World links Scripting; a link back would cycle —
// IRWorld:: symbols resolve at final-executable link, the same generator-
// expression include-only pattern the render/audio glue uses).
//
// Surface:
//   IRPersist.saveWorld(path) -> bool   serialize the live world to `path`
//                                       (+ a `.json` sidecar). false (logged)
//                                       on I/O failure; never throws for a
//                                       string path.
//   IRPersist.loadWorld(path) -> bool   restore an IRWS file into the live
//                                       world. false (logged) on a
//                                       missing/corrupt file or an id
//                                       collision; the world is left unmodified
//                                       on any failure (Save Format Rule #5).
//
// FRAME-BOUNDARY CONTRACT (documented, not enforced — exactly like the
// IRWorld.resetGameplay binding this mirrors): loadWorld is a structural
// teardown+rebuild and saveWorld reads the whole archetype graph, so BOTH must
// run at a frame boundary — a startup script, a command handler, or a
// scene-transition step — and NEVER from inside a Lua system tick /
// DISPATCH_LUA_* callback (the getComponent-mid-iteration UB engine/entity
// CLAUDE.md flags for resetGameplay applies identically). loadWorld does NOT
// reset the world itself: call `IRWorld.resetGameplay()` immediately before it,
// so the restore lands into the cleared graph the snapshot was projected
// against — loading into a still-populated world aborts on an id collision and
// returns false.
//
// `path` is a raw filesystem path (world snapshots are dev/tool artifacts), NOT
// routed through userDataDir like IRSave. A non-string argument raises a Lua
// error via sol coercion — the same I/O-fails->bool / misuse->error split the
// sibling IRSave persistence bindings use.
//
// A reloaded C_VoxelSetNew lands in staged mode (pendingVoxels_ populated,
// numVoxels_ == 0) — this binding does not attach it to a live pool. The
// caller's UPDATE pipeline must run SEED_STAGED_VOXELS (see
// system_seed_staged_voxels.hpp / engine/prefabs/irreden/voxel/CLAUDE.md
// "C_VoxelSetNew headless / staged mode") for the set to seed a pool span and
// render on a later frame; without it a loaded voxel set stays invisible.
inline void bindWorldSnapshotApi(LuaScript &script) {
    sol::state &lua = script.lua();
    if (!lua["IRPersist"].valid()) {
        lua["IRPersist"] = lua.create_table();
    }
    sol::table persist = lua["IRPersist"];

    persist["saveWorld"] = [](const std::string &path) -> bool {
        const IRAsset::BinaryStatus status = IRWorld::saveWorld(path);
        if (!status.ok()) {
            IRE_LOG_ERROR("IRPersist.saveWorld({}) failed: {}", path, status.message_);
            return false;
        }
        return true;
    };

    persist["loadWorld"] = [](const std::string &path) -> bool {
        const IRWorld::LoadResult result = IRWorld::loadWorld(path);
        if (!result.ok()) {
            IRE_LOG_ERROR("IRPersist.loadWorld({}) failed: {}", path, result.status_.message_);
            return false;
        }
        return true;
    };
}

} // namespace IRScript::detail

#endif /* LUA_WORLD_SNAPSHOT_BINDINGS_H */

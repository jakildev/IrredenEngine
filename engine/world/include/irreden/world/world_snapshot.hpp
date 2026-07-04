#ifndef WORLD_SNAPSHOT_H
#define WORLD_SNAPSHOT_H

/// ECS world snapshot — the `IRWS` container: save/load of the live entity
/// world to a single binary file (persist P2, #2213, epic #667). This is
/// the *entity-level* save path; it is distinct from
/// `chunk_persistence.hpp`'s per-chunk `.vxs` voxel-pool save (which
/// persists a streaming chunk's voxel slice, not entities/components).
///
/// ## File layout — `IRWS`
///
/// Standard `engine/asset/` container: 12-byte `AssetHeader`
/// (`magic="IRWS"`, `version`, `chunkCount`) + a chunk table + chunk
/// bodies (`chunk_header.hpp`). Unknown chunk tags are skipped on load
/// (Rule #1), so later phases add chunks without a version bump. Four
/// chunks, each built with the `binary_io.hpp` primitives:
///
///   - **`CMPN`** — component name table. `varuint count`, then `count`
///     length-prefixed save-names ordered ascending. A component's index
///     in this list is its *local index*, referenced compactly by `ARCH`
///     and `SNGL`. Names (not the session-local ComponentId) are the
///     on-disk identity — Rule #2.
///   - **`ARCH`** — archetypes + entities + component columns. Per
///     archetype: its projected component set (local indices), its entity
///     IDs (ascending raw ids), then one column per component
///     (`u32 saveVersion` + `varuint byteLength` + rows in entity order).
///     The per-column `(version, byteLength)` header is the P5 migration
///     seam: an unresolvable column is skipped by byte length, not fatal.
///   - **`SNGL`** — singleton components, restored *by value* onto the
///     live (post-reset preserved) singleton entity, not by re-inserting
///     an archetype row. Per entry: local index + saved raw entity id +
///     `u32 saveVersion` + `varuint byteLength` + value blob.
///   - **`META`** — `varuint nextEntityId` watermark + `varuint`
///     total saved entity count.
///
/// A `.json` sidecar (Rule #6, write-only) is emitted beside the binary
/// for human inspection; it is never read back.
///
/// ## Save projection + exclusion
///
/// Only components with a `SaveRegistry` entry (opted in via
/// `SaveTrait<C>` *and* registered) are written; an entity's saved
/// archetype is the **projection** of its live archetype onto those
/// components (opted-out and unregistered components drop out). Two live
/// archetypes that differ only by a dropped component merge into one saved
/// archetype. The walker excludes exactly what `resetGameplay`
/// (`destroyAllExceptPreserved`) preserves — singleton entities (they ride
/// `SNGL`), `C_Persistent`-tagged entities (recreated by their owner on
/// load), and component-backing entities — so the documented load contract
///
///     IREntity::resetGameplay();   // frame boundary
///     IRWorld::loadWorld(reg, path);
///
/// is collision-free by construction: everything the save wrote is exactly
/// what the reset destroyed. Entity IDs are restored **exact** (never
/// remapped) — they never recycle, so a component's embedded EntityId
/// fields stay valid and P3 relation triples resolve.
///
/// **Singleton save restriction (single-component only).** A singleton
/// entity is persisted *only* by its singleton component type (the one keyed
/// in `EntityManager::singletonEntityCache`): the walker excludes singleton
/// entities from the `ARCH` walk wholesale, and `SNGL` writes just that one
/// keyed component by value. So if a singleton entity ever carries an
/// *additional* opted-in component beyond its singleton type, that extra
/// component's data is silently dropped on save — it lands neither in `ARCH`
/// (the entity is excluded) nor in `SNGL` (which serializes only the keyed
/// type). Nothing in-tree does this today (see `engine/entity/CLAUDE.md`
/// "Singleton components"); this documents it as an intentional restriction,
/// not a latent bug. Keep singleton entities single-component, or promote the
/// extra state onto a normal (non-singleton) gameplay entity.
///
/// ## Determinism, errors
///
/// Same-world double-save is byte-identical (criterion W-8 subset): the
/// name table, archetype blocks, entities, and singletons are all sorted
/// on stable keys. Every read returns `IRAsset::BinaryStatus` — bad magic,
/// truncation, version-too-new, and a live-id collision all abort with a
/// recoverable status and **zero** world mutation (Rule #5); unknown chunk
/// tags and unknown component names are skipped with counts, not errors.
///
/// The `(SaveRegistry&, path)` signature is the honest P2 surface — a
/// registry decides what participates. A `(path)`-only convenience wrapper
/// over a process-default registry (for the P7 Lua binding) layers on once
/// engine components carry serializers.

#include <irreden/world/save_registry.hpp>

#include <irreden/asset/binary_io.hpp>
#include <irreden/entity/ir_entity_types.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace IRWorld {

inline constexpr std::array<char, 4> kWorldSnapshotMagic{'I', 'R', 'W', 'S'};
inline constexpr std::uint32_t kWorldSnapshotVersion = 1;

/// Outcome of `loadWorld`. `status_.ok()` gates everything else; on a
/// non-ok status the world is unmodified (Rule #5) and the counts are 0.
struct LoadResult {
    IRAsset::BinaryStatus status_;
    std::uint64_t entitiesRestored_ = 0;
    std::uint64_t singletonsRestored_ = 0;
    // Columns / singletons whose save-name did not resolve in the registry
    // (skipped by byte length, not an error — the file may predate a
    // component's registration, or postdate its removal).
    std::uint64_t columnsSkipped_ = 0;
    std::uint64_t singletonsSkipped_ = 0;
    // saved raw EntityId -> live EntityId for every restored singleton.
    // Identity in the same-session contract; the hand-off P3 needs to
    // translate relation triples that reference a singleton entity.
    std::unordered_map<IREntity::EntityId, IREntity::EntityId> singletonAliases_;

    bool ok() const {
        return status_.ok();
    }
};

/// Serialize the live entity world to `path` (+ `path + ".json"` sidecar).
/// Flushes deferred structural changes first (frame-boundary contract).
/// Returns the writer's failure status; `ok()` means the file is complete.
IRAsset::BinaryStatus saveWorld(const SaveRegistry &registry, const std::string &path);

/// Load an `IRWS` file into the live world. Two-phase: parse + validate
/// everything (any collision or corruption aborts with zero mutation),
/// then apply. Call after `IREntity::resetGameplay()` at a frame boundary.
LoadResult loadWorld(const SaveRegistry &registry, const std::string &path);

} // namespace IRWorld

#endif /* WORLD_SNAPSHOT_H */

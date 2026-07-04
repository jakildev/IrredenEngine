#ifndef SAVE_REGISTRY_H
#define SAVE_REGISTRY_H

/// Type-erased bridge from P1's compile-time `SaveTrait<C>` fact table to
/// the runtime archetype columns the world snapshot walks. Registration is
/// the only place a concrete component type `C` is named; everything after
/// works through `ComponentId` + these erased hooks, so the walker/loader
/// in `world_snapshot.cpp` compile once regardless of how many components
/// opt in.
///
/// One `SaveComponentEntry` per opted-in component captures:
///   - `saveName_`  — stable on-disk identity (`SaveTrait<C>::kSaveName`).
///   - `saveVersion_` — schema version (`saveVersion<C>()`).
///   - `componentId_` — the session-local `ComponentId` (resolved lazily
///     at registration; a fallback identity only, never the on-disk key).
///   - `writeRow_` / `appendRow_` — one component instance ↔ bytes, via
///     the `SaveSerialize<C>` customization point, against a live column.
///   - the singleton hooks (`getOrCreateSingletonEntity_`, `writeSingleton_`,
///     `readIntoEntity_`) for the SNGL chunk's by-value round-trip.
///
/// `registerComponent<C>()` is a no-op for an opted-out `C` (the body is
/// `if constexpr`-gated on `shouldSave<C>()`), so a caller may hand it any
/// component type without first checking the trait — and, crucially, an
/// opted-out type never instantiates `SaveSerialize<C>`, so a component
/// with no serializer only breaks the build if it actually opts in.
///
/// Scope note (P2): this layer is the *mechanism*. P2 proves it with
/// trivially-copyable test components (see `test/world/world_snapshot_test.cpp`);
/// wiring every engine component in `AllEngineComponents` — each of which
/// needs a `SaveSerialize<C>` specialization for its non-POD fields — is
/// downstream work, not this slice.

#include <irreden/world/save_migration.hpp>
#include <irreden/world/save_serialize.hpp>
#include <irreden/world/save_trait.hpp>

#include <irreden/asset/binary_io.hpp>
#include <irreden/entity/i_component_data.hpp>
#include <irreden/entity/ir_entity_types.hpp>
#include <irreden/ir_entity.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace IRWorld {

/// The three type-erased read hooks for one on-disk *version* of a component
/// — how its bytes turn back into a live value. The current version and each
/// retired version (a P5 migrator) share this shape, so the load-time version
/// dispatch (`SaveComponentEntry::readerForVersion`) can hand back whichever
/// applies as one uniform handle. The zero-mutation `decodeRow_` dry run the
/// load's phase-2 gate runs over every column exercises the exact same read
/// path as `appendRow_` / `readIntoEntity_`, so a length-valid-but-corrupt
/// column fails validation rather than aborting mid-apply.
struct ColumnReadHooks {
    // Deserialize one instance and append it to a live column.
    std::function<IRAsset::BinaryStatus(IRAsset::BinaryReader &, IREntity::IComponentData *)>
        appendRow_;
    // Decode one serialized instance and discard it, reporting only the status.
    std::function<IRAsset::BinaryStatus(IRAsset::BinaryReader &)> decodeRow_;
    // Deserialize one instance and overwrite it onto a live entity (SNGL read).
    std::function<IRAsset::BinaryStatus(IRAsset::BinaryReader &, IREntity::EntityId)>
        readIntoEntity_;
};

/// One opted-in component's type-erased save/load hooks. Function objects
/// are stateless (they close over the compile-time type only), so building
/// the registry is a handful of small allocations done once per save/load
/// session — never on a per-frame path.
struct SaveComponentEntry {
    std::string saveName_;
    std::uint32_t saveVersion_ = 0;
    IREntity::ComponentId componentId_ = IREntity::kNullComponent;

    // Serialize the component at `row` of a live column.
    std::function<void(IRAsset::BinaryWriter &, IREntity::IComponentData *, int)> writeRow_;

    // Read hooks for the *current* schema version (`saveVersion_`) — the
    // `SaveSerialize<C>::read` fast path.
    ColumnReadHooks reader_;
    // Read hooks for each retired on-disk version, keyed by that version
    // (persist P5, #2216). Populated from `SaveMigration<C>::migrators()`; a
    // component that never changed its schema leaves this empty. The current
    // version is NOT keyed here — `reader_` owns it.
    std::unordered_map<std::uint32_t, ColumnReadHooks> migratorReaders_;

    // Lazily get-or-create the singleton entity owning this component.
    std::function<IREntity::EntityId()> getOrCreateSingletonEntity_;
    // Serialize the component value held by a live entity (SNGL write).
    std::function<void(IRAsset::BinaryWriter &, IREntity::EntityId)> writeSingleton_;

    /// Resolve the read hooks for a column/singleton written at @p diskVersion,
    /// the P5 migration dispatch. Four cases (the unknown-component name case —
    /// no entry at all — is handled by the loader before this is reached):
    ///   - `diskVersion == saveVersion_` — current fast path (`reader_`).
    ///   - `diskVersion <  saveVersion_` — a registered migrator, or (miss) a
    ///     hard `MigratorMissing` error: reading old bytes at the current
    ///     layout silently corrupts, so this is the one case that must fail.
    ///   - `diskVersion >  saveVersion_` — `VersionTooNew` (a future writer).
    /// On success returns non-null and leaves @p status untouched; on failure
    /// returns nullptr and sets @p status to the diagnostic.
    const ColumnReadHooks *
    readerForVersion(std::uint32_t diskVersion, IRAsset::BinaryStatus &status) const {
        if (diskVersion == saveVersion_) {
            return &reader_;
        }
        if (diskVersion > saveVersion_) {
            status = IRAsset::BinaryStatus::error(
                IRAsset::BinaryIOError::VersionTooNew,
                saveName_ + ": on-disk version " + std::to_string(diskVersion) +
                    " is newer than this build reads (v" + std::to_string(saveVersion_) + ")"
            );
            return nullptr;
        }
        const auto it = migratorReaders_.find(diskVersion);
        if (it != migratorReaders_.end()) {
            return &it->second;
        }
        status = IRAsset::BinaryStatus::error(
            IRAsset::BinaryIOError::MigratorMissing,
            missingMsg(diskVersion)
        );
        return nullptr;
    }

  private:
    // Diagnostic for a known component at an older version with no migrator.
    std::string missingMsg(std::uint32_t diskVersion) const {
        std::string msg = saveName_ + ": no migrator for on-disk version " +
                          std::to_string(diskVersion) + " (this build reads v" +
                          std::to_string(saveVersion_) +
                          "; register a SaveMigration<C> reader for it)";
        if (!migratorReaders_.empty()) {
            std::uint32_t lowest = diskVersion;
            for (const auto &kv : migratorReaders_) {
                if (kv.first < lowest) {
                    lowest = kv.first;
                }
            }
            msg += " — registered migrators start at v" + std::to_string(lowest);
        }
        return msg;
    }
};

class SaveRegistry {
  public:
    /// Register component `C` if — and only if — it opts in. Opted-out
    /// types (and unregistered-decision types) no-op without instantiating
    /// `SaveSerialize<C>`. Re-registering the same save-name is ignored so
    /// a caller can register defensively.
    template <typename C> void registerComponent() {
        if constexpr (shouldSave<C>()) {
            const char *name = saveName<C>();
            if (name == nullptr || m_byName.contains(name)) {
                return;
            }
            SaveComponentEntry entry;
            entry.saveName_ = name;
            entry.saveVersion_ = saveVersion<C>();
            entry.componentId_ = IREntity::getComponentType<C>();
            entry.writeRow_ = [](IRAsset::BinaryWriter &w, IREntity::IComponentData *col, int row) {
                SaveSerialize<C>::write(
                    w,
                    IREntity::castComponentDataPointer<C>(col)->dataVector[row]
                );
            };
            // Current-version reader: the SaveSerialize<C>::read fast path.
            entry.reader_ =
                buildReader<C>([](IRAsset::BinaryReader &r) { return SaveSerialize<C>::read(r); });
            // Retired-version readers (persist P5, #2216) — one erased reader
            // per SaveMigration<C> entry. Empty for a component whose schema
            // never changed; SaveMigration<C> is instantiated only inside this
            // shouldSave<C>() branch, so an opted-out C never needs one.
            for (auto &versioned : SaveMigration<C>::migrators()) {
                entry.migratorReaders_.emplace(
                    versioned.first,
                    buildReader<C>(std::move(versioned.second))
                );
            }
            entry.getOrCreateSingletonEntity_ = []() -> IREntity::EntityId {
                return IREntity::singletonEntity<C>();
            };
            entry.writeSingleton_ = [](IRAsset::BinaryWriter &w, IREntity::EntityId entity) {
                SaveSerialize<C>::write(w, IREntity::getComponent<C>(entity));
            };

            const std::size_t index = m_entries.size();
            m_byName.emplace(entry.saveName_, index);
            m_byComponentId.emplace(entry.componentId_, index);
            m_entries.push_back(std::move(entry));
        }
    }

    /// Registered entry for a session-local `ComponentId`, or nullptr if
    /// the component is not opted in (used by the save-side walker, which
    /// starts from an archetype's live ComponentIds).
    const SaveComponentEntry *findByComponentId(IREntity::ComponentId id) const {
        auto it = m_byComponentId.find(id);
        return it == m_byComponentId.end() ? nullptr : &m_entries[it->second];
    }

    /// Registered entry for a stable save-name, or nullptr if unresolvable
    /// (used by the load-side, which starts from the file's CMPN names — an
    /// unresolvable name is a skipped column, not a fatal error).
    const SaveComponentEntry *findByName(const std::string &saveName) const {
        auto it = m_byName.find(saveName);
        return it == m_byName.end() ? nullptr : &m_entries[it->second];
    }

    const std::vector<SaveComponentEntry> &entries() const {
        return m_entries;
    }

    std::size_t size() const {
        return m_entries.size();
    }

  private:
    // Erase a per-version read function (the SaveSerialize<C>::read current
    // fast path, or a SaveMigration<C> retired-version reader) into the three
    // generic column-read hooks. Shared so the current reader and every
    // migrator are built identically — the only difference is which `readFn`
    // decodes the bytes.
    template <typename C> static ColumnReadHooks buildReader(ColumnMigratorFn<C> readFn) {
        ColumnReadHooks hooks;
        hooks.appendRow_ = [readFn](IRAsset::BinaryReader &r, IREntity::IComponentData *col)
            -> IRAsset::BinaryStatus {
            IRAsset::Result<C> res = readFn(r);
            if (!res.ok()) {
                return res.status_;
            }
            IREntity::castComponentDataPointer<C>(col)->dataVector.push_back(std::move(res.value_));
            return IRAsset::BinaryStatus::success();
        };
        hooks.decodeRow_ = [readFn](IRAsset::BinaryReader &r) -> IRAsset::BinaryStatus {
            return readFn(r).status_;
        };
        hooks.readIntoEntity_ =
            [readFn](IRAsset::BinaryReader &r, IREntity::EntityId entity) -> IRAsset::BinaryStatus {
            IRAsset::Result<C> res = readFn(r);
            if (!res.ok()) {
                return res.status_;
            }
            IREntity::setComponent<C>(entity, std::move(res.value_));
            return IRAsset::BinaryStatus::success();
        };
        return hooks;
    }

    std::vector<SaveComponentEntry> m_entries;
    std::unordered_map<std::string, std::size_t> m_byName;
    std::unordered_map<IREntity::ComponentId, std::size_t> m_byComponentId;
};

} // namespace IRWorld

#endif /* SAVE_REGISTRY_H */

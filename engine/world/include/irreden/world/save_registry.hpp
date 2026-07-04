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
    // Deserialize one instance and append it to a live column.
    std::function<IRAsset::BinaryStatus(IRAsset::BinaryReader &, IREntity::IComponentData *)>
        appendRow_;

    // Lazily get-or-create the singleton entity owning this component.
    std::function<IREntity::EntityId()> getOrCreateSingletonEntity_;
    // Serialize the component value held by a live entity (SNGL write).
    std::function<void(IRAsset::BinaryWriter &, IREntity::EntityId)> writeSingleton_;
    // Deserialize one instance and overwrite it onto a live entity (SNGL read).
    std::function<IRAsset::BinaryStatus(IRAsset::BinaryReader &, IREntity::EntityId)>
        readIntoEntity_;
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
            entry.appendRow_ = [](IRAsset::BinaryReader &r,
                                  IREntity::IComponentData *col) -> IRAsset::BinaryStatus {
                IRAsset::Result<C> res = SaveSerialize<C>::read(r);
                if (!res.ok()) {
                    return res.status_;
                }
                IREntity::castComponentDataPointer<C>(col)->dataVector.push_back(
                    std::move(res.value_)
                );
                return IRAsset::BinaryStatus::success();
            };
            entry.getOrCreateSingletonEntity_ = []() -> IREntity::EntityId {
                return IREntity::singletonEntity<C>();
            };
            entry.writeSingleton_ = [](IRAsset::BinaryWriter &w, IREntity::EntityId entity) {
                SaveSerialize<C>::write(w, IREntity::getComponent<C>(entity));
            };
            entry.readIntoEntity_ = [](IRAsset::BinaryReader &r,
                                       IREntity::EntityId entity) -> IRAsset::BinaryStatus {
                IRAsset::Result<C> res = SaveSerialize<C>::read(r);
                if (!res.ok()) {
                    return res.status_;
                }
                IREntity::setComponent<C>(entity, std::move(res.value_));
                return IRAsset::BinaryStatus::success();
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
    std::vector<SaveComponentEntry> m_entries;
    std::unordered_map<std::string, std::size_t> m_byName;
    std::unordered_map<IREntity::ComponentId, std::size_t> m_byComponentId;
};

} // namespace IRWorld

#endif /* SAVE_REGISTRY_H */

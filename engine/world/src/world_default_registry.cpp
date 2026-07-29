#include <irreden/world/world_snapshot.hpp>

// Heavy include: the audited per-component save-policy table + every component
// type's definition. Confined to this one TU (never a widely-included header,
// per save_component_inventory.hpp's own note) so promoting the process-default
// registry to more components only recompiles this file.
#include <irreden/world/save_component_inventory.hpp>

// The SaveSerialize<C> specializations for every opted-in component that owns
// heap storage. These are NOT pulled by the component headers (that would drag
// engine/world into every prefab domain's include graph), so they must be in
// scope wherever a registry registers those components — otherwise
// registerComponent<C> finds no usable SaveSerialize<C> and the friendly
// static_assert in save_registry.hpp fires. C_VoxelSetNew keeps its own
// header (persist P6) because its read path has a pool-interaction contract
// the rest do not.
#include <irreden/audio/save_serializers_audio.hpp>
#include <irreden/common/save_serializers_common.hpp>
#include <irreden/demo/save_serializers_demo.hpp>
#include <irreden/render/save_serializers_render.hpp>
#include <irreden/update/save_serializers_update.hpp>
#include <irreden/voxel/save_serializers_voxel.hpp>
#include <irreden/voxel/voxel_set_serialize.hpp>

#include <tuple>
#include <type_traits>

namespace IRWorld {

namespace {

// Walk the audited inventory and hand every entry to registerComponent<C>.
// The opt-outs no-op (registerComponent's `if constexpr (shouldSave<C>())`
// gate) without instantiating their serializer, so membership is exactly the
// opted-in set — derived from save_component_inventory.hpp, never curated
// here. Every opted-in entry DOES instantiate SaveSerialize<C>, which makes
// this TU the completeness gate: a future IR_SAVE_OPT_IN with no serializer
// breaks this build with registerComponent's friendly static_assert instead
// of silently dropping the component from every Lua-driven save.
template <typename... Cs>
void registerAll(SaveRegistry &registry, std::type_identity<std::tuple<Cs...>>) {
    (registry.registerComponent<Cs>(), ...);
}

} // namespace

// The process-default SaveRegistry (persist P7 #2218; membership derived from
// the inventory in #2242) that the no-registry-argument saveWorld(path) /
// loadWorld(path) overloads — and through them the IRPersist Lua binding —
// forward to. There is deliberately no per-component register line here; the
// generic walk is what keeps membership derived and doubles as the
// serializer-completeness gate.
SaveRegistry makeDefaultSaveRegistry() {
    SaveRegistry registry;
    registerAll(registry, std::type_identity<AllEngineComponents>{});
    return registry;
}

IRAsset::BinaryStatus saveWorld(const std::string &path) {
    return saveWorld(makeDefaultSaveRegistry(), path);
}

LoadResult loadWorld(const std::string &path) {
    return loadWorld(makeDefaultSaveRegistry(), path);
}

} // namespace IRWorld

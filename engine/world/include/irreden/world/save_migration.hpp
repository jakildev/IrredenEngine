#ifndef SAVE_MIGRATION_H
#define SAVE_MIGRATION_H

/// Per-component schema-migration customization point for the ECS world
/// snapshot (persist P5, #2216, epic #667). P1 (`save_trait.hpp`) decides
/// *whether* a component is saved and its current `kSaveVersion`; P2's
/// `save_serialize.hpp` decides *how* the current version's bytes read; this
/// header decides how a **retired** on-disk version reads.
///
/// The `ARCH`/`SNGL` chunks stamp every column with the `kSaveVersion` it was
/// written at. When an old snapshot is loaded into a newer build whose
/// component has since bumped its version, the load-time dispatch (see
/// `save_registry.hpp`'s `SaveComponentEntry::readerForVersion`) needs a reader
/// for the *disk-side* layout — reading old bytes at the current layout would
/// silently corrupt (Save Format Extensibility Rule #3). `SaveMigration<C>`
/// is where a component declares that reader.
///
/// The primary template declares **no** migrators — correct for any component
/// that has never changed its serialized schema (`kSaveVersion == 1`, or a
/// later version reached purely by additive fields the current
/// `SaveSerialize<C>::read` still parses). A component that retires a version
/// specializes this, mirroring the `SaveSerialize<C>` pattern:
///
/// ```cpp
/// // C_Foo evolved v1 -> v2 by appending a defaulted `damping_` field.
/// namespace IRWorld {
/// template <> struct SaveMigration<C_Foo> {
///     static std::vector<std::pair<std::uint32_t, ColumnMigratorFn<C_Foo>>>
///     migrators() {
///         return {
///             {1u, [](IRAsset::BinaryReader &r) -> IRAsset::Result<C_Foo> {
///                  // read exactly the v1 layout; default the appended field
///                  IRAsset::Result<float> vel = r.readF32();
///                  if (!vel.ok())
///                      return IRAsset::Result<C_Foo>::error(vel.status_.code_,
///                                                           vel.status_.message_);
///                  return IRAsset::Result<C_Foo>::success(C_Foo{vel.value_, 0.0f});
///              }},
///         };
///     }
/// };
/// } // namespace IRWorld
/// ```
///
/// Contract:
///   - **Direct per-version readers, never chained.** Each reader decodes its
///     own era's bytes straight to a current-build `C`; a v3 bump leaves the
///     v1/v2 readers untouched. No `v1 -> v2 -> v3` composition.
///   - **The current version is NOT listed here** — `SaveSerialize<C>::read`
///     handles `kSaveVersion`; list only the retired versions
///     (`[1 .. kSaveVersion - 1]`).
///   - A disk version below `kSaveVersion` with no entry here is a hard
///     `BinaryIOError::MigratorMissing` on load (never a silent read-at-current);
///     a disk version above `kSaveVersion` is `VersionTooNew`.

#include <irreden/asset/binary_io.hpp>

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace IRWorld {

/// Reader for one retired on-disk version of component `C`: consumes exactly
/// that version's bytes from @p r and yields a fully-populated current-build
/// `C` (fields added since are defaulted, renamed fields remapped). Type-bound
/// to `C`; the registry erases it into the generic column-read hooks.
template <typename C>
using ColumnMigratorFn = std::function<IRAsset::Result<C>(IRAsset::BinaryReader &)>;

/// Customization point declaring how to read component `C` at each retired
/// on-disk version. Specialize for a component that has bumped
/// `SaveTrait<C>::kSaveVersion`; the primary template declares none.
template <typename C> struct SaveMigration {
    /// `(fromVersion, reader)` pairs — one per retired version. Default: none.
    static std::vector<std::pair<std::uint32_t, ColumnMigratorFn<C>>> migrators() {
        return {};
    }
};

} // namespace IRWorld

#endif /* SAVE_MIGRATION_H */

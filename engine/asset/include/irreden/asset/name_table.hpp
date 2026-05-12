#ifndef IR_ASSET_NAME_TABLE_H
#define IR_ASSET_NAME_TABLE_H

/// Name-table chunk helper for Save Format Extensibility Rule #2:
/// **Enums travel by name + id.** Anywhere a registered enum is stored
/// (`ShapeType`, `ComponentId`, `RelationType`, `MaterialId`, ...) the
/// format writes both the numeric id and a string-name table at the head
/// of the chunk. On load the consumer prefers `name → current enum`
/// lookup; the numeric id is a fallback when the name table is absent or
/// the name has been removed from the enum.
///
/// A save written by a future build with a new `ShapeType::TORUS_KNOT`
/// loads on an older build as "unknown shape, skipped" — not "corrupt
/// file."
///
/// On-disk format (one chunk body):
///
///     varuint  count
///     repeat count times:
///       varuint  id
///       string   name
///
/// Strings are UTF-8 with a varuint byte-length prefix.

#include <irreden/asset/binary_io.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace IRAsset {

/// One (numeric-id, name) pair as it lives in a name-table chunk.
struct NameTableEntry {
    std::uint32_t id_ = 0;
    std::string name_;
};

/// Serialize @p entries into a name-table chunk body. Caller wraps the
/// result in a `ChunkPayload` with whatever tag the format uses
/// (`SHPN`, `CMPN`, `RELN`, ...).
BinaryStatus writeNameTable(BinaryWriter &w, std::span<const NameTableEntry> entries);

/// Read a name-table chunk body up to EOF/buffer-end. Stops cleanly at
/// the declared `count` even if more data follows (the chunk-table
/// `size_` already bounds the input slice).
Result<std::vector<NameTableEntry>> readNameTable(BinaryReader &r);

/// Bidirectional lookup over a name-table. Built from a vector of
/// entries; used by the consumer to translate disk-side ids to the
/// build's current enum values (or vice versa, on save).
class NameTable {
  public:
    NameTable() = default;
    explicit NameTable(std::vector<NameTableEntry> entries);

    void add(std::uint32_t id, std::string name);

    /// Returns the disk-side id for @p name, or `nullopt` if absent.
    std::optional<std::uint32_t> idByName(std::string_view name) const;

    /// Returns the disk-side name for @p id, or `nullopt` if absent.
    /// Invalidated by any subsequent call to `add()`.
    std::optional<std::string_view> nameById(std::uint32_t id) const;

    const std::vector<NameTableEntry> &entries() const {
        return m_entries;
    }

    bool empty() const {
        return m_entries.empty();
    }
    std::size_t size() const {
        return m_entries.size();
    }

  private:
    std::vector<NameTableEntry> m_entries;
    std::unordered_map<std::string, std::uint32_t> m_byName;
    std::unordered_map<std::uint32_t, std::size_t> m_byId;
};

} // namespace IRAsset

#endif /* IR_ASSET_NAME_TABLE_H */

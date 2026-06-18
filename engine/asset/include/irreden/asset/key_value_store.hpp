#ifndef IR_ASSET_KEY_VALUE_STORE_H
#define IR_ASSET_KEY_VALUE_STORE_H

/// `.irkv` v1 — a flat, named key/value persistence surface for high
/// scores + settings (issue #1819). Built entirely on the shared
/// `BinaryWriter` / `BinaryReader` + chunk-table primitives in this module
/// — NOT the ECS world snapshot (#199 / epic #667), which walks the
/// archetype graph and lives in `engine/world/`. A store is a flat map of
/// string keys to typed scalar / list values; gameplay reaches it from Lua
/// via the `IRSave` table (`engine/script/.../lua_persistence_bindings.hpp`).
///
/// On-disk layout
/// --------------
///
///     AssetHeader  { magic = "IRKV", version = 1, chunkCount }
///     ChunkTableEntry[chunkCount]
///     KVPR chunk body:
///         varuint  entryCount
///         repeat entryCount times:
///             string  key              // varuint-prefixed UTF-8
///             uint8   valueTag         // ValueType: 0=NUMBER 1=BOOL 2=STRING 3=LIST
///             value payload, by tag:
///                 NUMBER → float64
///                 BOOL   → uint8 (0 / 1)
///                 STRING → string
///                 LIST   → varuint elemCount, then per element:
///                              uint8 elemTag (NUMBER | BOOL | STRING — no nesting)
///                              elem payload (float64 / uint8 / string)
///
/// Forward compatibility: a future writer that adds a new top-level chunk
/// is skipped silently by an older loader (Extensibility Rule #1). A value
/// tag this loader doesn't recognize (e.g. a future writer adds a new
/// value type) cannot be length-skipped, so the read stops with a
/// recoverable `UnknownTag` error and the file-level loader returns an
/// empty store (Rule #5) — never a crash.
///
/// Version history
/// ---------------
/// v1 (initial) — NUMBER / BOOL / STRING / LIST values in the KVPR chunk.
///
/// Numbers are stored as float64: LuaJIT has no integer subtype (all Lua
/// numbers are doubles), and a double represents whole numbers exactly to
/// 2^53 — far beyond any high score. No int64 value type until a real
/// consumer needs values above 2^53.

#include <irreden/asset/binary_io.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace IRAsset {

/// On-disk value discriminant. The numeric values double as the variant
/// alternative indices of `Value` / `ListElem` below (NUMBER=0, BOOL=1,
/// STRING=2, LIST=3) so `static_cast<ValueType>(v.index())` is the tag.
enum class ValueType : std::uint8_t {
    NUMBER = 0,
    BOOL = 1,
    STRING = 2,
    LIST = 3,
};

/// A scalar list element — number, bool, or string. Lists do not nest in
/// v1, so a list element is exactly these three (the first three `Value`
/// alternatives, in the same order).
using ListElem = std::variant<double, bool, std::string>;

/// A stored value: a scalar (number / bool / string) or a flat list of
/// scalars. Alternative order matches `ValueType` so `index()` is the tag.
using Value = std::variant<double, bool, std::string, std::vector<ListElem>>;

/// The on-disk tag for @p value (its variant alternative index).
inline ValueType valueType(const Value &value) {
    return static_cast<ValueType>(value.index());
}

/// In-memory flat key/value store. One per logical save file (e.g.
/// "highscores", "settings"). Persisted via `saveKeyValueStore` /
/// `loadKeyValueStore` below.
class KeyValueStore {
  public:
    void set(const std::string &key, Value value) {
        m_entries[key] = std::move(value);
    }

    /// Returns a pointer to the stored value, or nullptr if @p key is absent.
    const Value *get(const std::string &key) const {
        const auto it = m_entries.find(key);
        return it == m_entries.end() ? nullptr : &it->second;
    }

    /// Typed convenience reads — return @p fallback when the key is absent
    /// or holds a different type.
    double getNumber(const std::string &key, double fallback = 0.0) const;
    bool getBool(const std::string &key, bool fallback = false) const;
    std::string getString(const std::string &key, const std::string &fallback = "") const;

    bool has(const std::string &key) const {
        return m_entries.find(key) != m_entries.end();
    }

    /// Removes @p key. Returns true if it was present.
    bool remove(const std::string &key) {
        return m_entries.erase(key) > 0;
    }

    void clear() {
        m_entries.clear();
    }

    std::size_t size() const {
        return m_entries.size();
    }

    std::vector<std::string> keys() const;

  private:
    std::unordered_map<std::string, Value> m_entries;
};

constexpr std::array<char, 4> kKeyValueMagic{'I', 'R', 'K', 'V'};
constexpr std::uint32_t kKeyValueFormatVersion = 1;

/// Buffer-mode write — exposed so tests (and any future in-memory consumer)
/// can exercise the format without touching disk. Mirrors `writeRig`.
BinaryStatus writeKeyValueStore(BinaryWriter &w, const KeyValueStore &store);

/// Buffer-mode read counterpart. Bad magic / truncation / a version above
/// `kKeyValueFormatVersion` / an unknown value tag surface as recoverable
/// errors (Rule #5); `value_` is an empty store on error.
Result<KeyValueStore> readKeyValueStore(BinaryReader &r);

/// File-mode save to @p path (a full path including the `.irkv` extension).
/// Creates the parent directory via `std::filesystem::create_directories`
/// first, since `userDataDir` / `joinPath` do not. Returns the writer's
/// failure state.
BinaryStatus saveKeyValueStore(const std::string &path, const KeyValueStore &store);

/// File-mode load from @p path. A missing file returns `OpenFailed`; a
/// corrupt / truncated / too-new file returns a recoverable error with an
/// empty store (Rule #5) — callers treat either as "no save → defaults".
Result<KeyValueStore> loadKeyValueStore(const std::string &path);

} // namespace IRAsset

#endif /* IR_ASSET_KEY_VALUE_STORE_H */

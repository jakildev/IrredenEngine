#ifndef SAVE_SERIALIZE_COMMON_H
#define SAVE_SERIALIZE_COMMON_H

/// Shared building blocks for `SaveSerialize<C>` specializations (#2242).
/// `save_serialize.hpp` handles the two ends of the spectrum — a whole
/// component that is trivially copyable, or one that hand-rolls everything.
/// Most heap-owning components sit in between: a few scalars plus a
/// `std::vector` of trivially-copyable records, a `std::vector<std::string>`,
/// or a string-keyed map. These helpers own the byte layout for those shapes
/// so ~20 serializers don't each re-derive the count-then-records loop.
///
/// Layout for every container helper: a `writeVarUInt` element count followed
/// by the elements. Scalars and strings use the `BinaryWriter` primitives
/// directly (`writeString` is already length-prefixed).
///
/// Determinism: `writeSortedStringMap` emits entries in ascending key order,
/// because an `unordered_map`'s iteration order is not a contract — writing it
/// raw would break the same-session double-save byte-identity requirement
/// (world-snapshot criterion 6). Vectors keep their authored order.
///
/// Read-side guard: element counts come off disk, so a corrupt or truncated
/// stream can claim an absurd count. The readers never pre-size to the
/// claimed count — they reserve a bounded amount and append per record, so a
/// bad count fails on the first short read instead of attempting a huge
/// allocation first.
///
/// Versioning note: a component's schema version is **not** the
/// `// IRAsset: serialized` + `kSaveVersion` member pair that `engine/asset/`
/// records use. World-snapshot components carry it on the trait instead —
/// `SaveTrait<C>::kSaveVersion`, set by `IR_SAVE_OPT_IN(Type, Version)` — and
/// changing a serializer's field layout means bumping that and registering a
/// `SaveMigration<C>` reader for the retired version, or an old save decodes
/// at the new layout and silently corrupts.

#include <irreden/asset/binary_io.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace IRWorld::detail {

/// Upper bound on how much a reader speculatively reserves before it has
/// actually read any records. Purely an allocation-churn guard; a larger
/// container still round-trips, it just grows geometrically past this point.
inline constexpr std::size_t kReadReserveCap = 4096;

/// How much to `reserve` for a container whose on-disk element count is
/// @p count. Clamped to `kReadReserveCap` so a corrupt count cannot turn into
/// a huge allocation before a single element has been read.
inline std::size_t boundedReserve(std::uint64_t count) {
    return static_cast<std::size_t>(count < kReadReserveCap ? count : kReadReserveCap);
}

/// varuint count + one raw byte image per element. `T` must be trivially
/// copyable — the same raw-image contract `SaveSerialize`'s trivially-copyable
/// arm uses for whole components.
template <typename T> void writeTrivialVector(IRAsset::BinaryWriter &w, const std::vector<T> &v) {
    static_assert(
        std::is_trivially_copyable_v<T>,
        "writeTrivialVector<T>: T is not trivially copyable — a raw byte image would persist "
        "dangling pointers. Write an element-wise loop for this type instead."
    );
    w.writeVarUInt(v.size());
    for (const T &element : v) {
        w.writeBytes(&element, sizeof(T));
    }
}

/// Inverse of `writeTrivialVector`. Leaves @p out untouched on failure.
template <typename T>
IRAsset::BinaryStatus readTrivialVector(IRAsset::BinaryReader &r, std::vector<T> &out) {
    static_assert(
        std::is_trivially_copyable_v<T>,
        "readTrivialVector<T>: T is not trivially copyable — see writeTrivialVector."
    );
    IRAsset::Result<std::uint64_t> count = r.readVarUInt();
    if (!count.ok()) {
        return count.status_;
    }
    std::vector<T> values;
    values.reserve(boundedReserve(count.value_));
    for (std::uint64_t i = 0; i < count.value_; ++i) {
        // Read into raw bytes and bit_cast rather than default-constructing a
        // T: a trivially-copyable record is not necessarily *default
        // constructible* (IRComponents::PeriodStage, for one, only has a
        // 5-argument constructor), and the byte image is the contract here
        // anyway.
        std::array<std::byte, sizeof(T)> raw{};
        IRAsset::BinaryStatus status = r.readBytes(raw.data(), sizeof(T));
        if (!status.ok()) {
            return status;
        }
        values.push_back(std::bit_cast<T>(raw));
    }
    out = std::move(values);
    return IRAsset::BinaryStatus::success();
}

/// varuint count + one length-prefixed string per element.
inline void writeStringVector(IRAsset::BinaryWriter &w, const std::vector<std::string> &v) {
    w.writeVarUInt(v.size());
    for (const std::string &element : v) {
        w.writeString(element);
    }
}

/// Inverse of `writeStringVector`. Leaves @p out untouched on failure.
inline IRAsset::BinaryStatus
readStringVector(IRAsset::BinaryReader &r, std::vector<std::string> &out) {
    IRAsset::Result<std::uint64_t> count = r.readVarUInt();
    if (!count.ok()) {
        return count.status_;
    }
    std::vector<std::string> values;
    values.reserve(boundedReserve(count.value_));
    for (std::uint64_t i = 0; i < count.value_; ++i) {
        IRAsset::Result<std::string> element = r.readString();
        if (!element.ok()) {
            return element.status_;
        }
        values.push_back(std::move(element.value_));
    }
    out = std::move(values);
    return IRAsset::BinaryStatus::success();
}

/// varuint count + `(string key, raw value image)` pairs in ascending key
/// order. Takes any string-keyed map; the sort is what makes an
/// `unordered_map` round-trip byte-identically across saves.
template <typename Map> void writeSortedStringMap(IRAsset::BinaryWriter &w, const Map &map) {
    using Value = typename Map::mapped_type;
    static_assert(
        std::is_trivially_copyable_v<Value>,
        "writeSortedStringMap: the mapped type is not trivially copyable — see "
        "writeTrivialVector."
    );
    std::vector<const typename Map::value_type *> ordered;
    ordered.reserve(map.size());
    for (const auto &entry : map) {
        ordered.push_back(&entry);
    }
    // Insertion sort over pointers: these maps are small (bind points on one
    // rig), and it keeps <algorithm> out of a header every serializer includes.
    for (std::size_t i = 1; i < ordered.size(); ++i) {
        const typename Map::value_type *key = ordered[i];
        std::size_t j = i;
        while (j > 0 && ordered[j - 1]->first > key->first) {
            ordered[j] = ordered[j - 1];
            --j;
        }
        ordered[j] = key;
    }
    w.writeVarUInt(ordered.size());
    for (const typename Map::value_type *entry : ordered) {
        w.writeString(entry->first);
        w.writeBytes(&entry->second, sizeof(Value));
    }
}

/// Inverse of `writeSortedStringMap`. Leaves @p out untouched on failure.
template <typename Map> IRAsset::BinaryStatus readStringMap(IRAsset::BinaryReader &r, Map &out) {
    using Value = typename Map::mapped_type;
    static_assert(
        std::is_trivially_copyable_v<Value>,
        "readStringMap: the mapped type is not trivially copyable — see writeSortedStringMap."
    );
    IRAsset::Result<std::uint64_t> count = r.readVarUInt();
    if (!count.ok()) {
        return count.status_;
    }
    Map values;
    for (std::uint64_t i = 0; i < count.value_; ++i) {
        IRAsset::Result<std::string> key = r.readString();
        if (!key.ok()) {
            return key.status_;
        }
        // Raw bytes + bit_cast, for the same reason as readTrivialVector.
        std::array<std::byte, sizeof(Value)> raw{};
        IRAsset::BinaryStatus status = r.readBytes(raw.data(), sizeof(Value));
        if (!status.ok()) {
            return status;
        }
        values.emplace(std::move(key.value_), std::bit_cast<Value>(raw));
    }
    out = std::move(values);
    return IRAsset::BinaryStatus::success();
}

} // namespace IRWorld::detail

/// Read-one-field-or-bail, for use inside a `SaveSerialize<C>::read` body.
///
/// Every read in a serializer is the same five lines: call a `BinaryReader`
/// accessor, return its status as a `Result<C>` error if it failed, else move
/// the value into a field. Written out longhand across ~25 serializers that
/// is ~750 lines in which the only variation is *which* `Result` gets
/// checked — precisely the shape where a copy-paste slip (checking the
/// previous field's result, or forgetting the check) is both easy to make and
/// invisible in review. These macros make the bail-out uniform.
///
/// Contract: the enclosing `read()` must (a) return `IRAsset::Result<C>` and
/// (b) have `using Res = IRAsset::Result<C>;` in scope — the convention every
/// serializer in the tree already follows. Statement macros, so they take a
/// trailing semicolon like any other statement.
#define IR_SAVE_READ(target, readExpr)                                                             \
    do {                                                                                           \
        auto irSaveField = (readExpr);                                                             \
        if (!irSaveField.ok()) {                                                                   \
            return Res::error(irSaveField.status_.code_, std::move(irSaveField.status_.message_)); \
        }                                                                                          \
        (target) = std::move(irSaveField.value_);                                                  \
    } while (false)

/// `IR_SAVE_READ` for a `bool` field: the wire form is a `u8`, so the value
/// needs the `!= 0` narrowing that a direct assignment would warn on.
#define IR_SAVE_READ_BOOL(target, readExpr)                                                        \
    do {                                                                                           \
        auto irSaveField = (readExpr);                                                             \
        if (!irSaveField.ok()) {                                                                   \
            return Res::error(irSaveField.status_.code_, std::move(irSaveField.status_.message_)); \
        }                                                                                          \
        (target) = irSaveField.value_ != 0;                                                        \
    } while (false)

/// `IR_SAVE_READ` for the `detail::read*` container helpers, which report a
/// bare `BinaryStatus` and write their result through an out-parameter rather
/// than returning a `Result`.
#define IR_SAVE_READ_STATUS(statusExpr)                                                            \
    do {                                                                                           \
        IRAsset::BinaryStatus irSaveStatus = (statusExpr);                                         \
        if (!irSaveStatus.ok()) {                                                                  \
            return Res::error(irSaveStatus.code_, std::move(irSaveStatus.message_));               \
        }                                                                                          \
    } while (false)

#endif /* SAVE_SERIALIZE_COMMON_H */

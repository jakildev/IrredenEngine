#ifndef SAVE_SERIALIZE_H
#define SAVE_SERIALIZE_H

/// Per-component binary (de)serialization customization point for the ECS
/// world snapshot (`world_snapshot.hpp`). P1 (`save_trait.hpp`) decides
/// *whether* a component is saved and at what schema version; this header
/// decides *how* one instance turns into bytes.
///
/// The primary template is **declared but never defined**; the two ways to
/// be serializable are both specializations of it:
///
///   - the constrained partial specialization below — a trivially-copyable
///     fast path: any component that is `std::is_trivially_copyable`
///     round-trips as a raw byte image, which covers the bulk of plain
///     gameplay data (positions, velocities, timers, flags, small PODs);
///   - an explicit full specialization, for a component that owns heap
///     storage (`std::string`, `std::vector`, resource handles) and is NOT
///     trivially copyable — a raw memcpy would persist dangling pointers.
///
/// Leaving the primary undefined is what makes serializability *detectable*:
/// `SaveSerializable<C>` below is satisfied exactly when one of those two
/// arms exists — a fact the compiler already knows, rather than a
/// hand-maintained companion trait (which would drift the moment a serializer
/// landed without its trait line — the silent-omission failure this concept
/// exists to kill).
///
/// The concept is a **gate, not a filter**: `makeDefaultSaveRegistry` hands
/// every `AllEngineComponents` entry to `registerComponent<C>`, which
/// `static_assert`s `SaveSerializable<C>` with a friendly diagnostic, so
/// "opted in but no serializer" is a build error. Do not "restore" a filter
/// here — silently dropping such a component from every save is the precise
/// failure this design exists to kill.
///
/// Determinism note: the byte image of a trivially-copyable struct
/// includes padding. Padding bytes are stable for a fixed in-memory value
/// within a session, so a same-session double-save is byte-identical
/// (world-snapshot criterion 6). Cross-session byte-stability of padding
/// is a separate concern deferred to P4/W-8.

#include <irreden/asset/binary_io.hpp>

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace IRWorld {

/// Customization point: `write` serializes one `const C&`; `read` pulls one
/// `C` back. Declared-not-defined on purpose — see the header note; a
/// component is serializable only via the trivially-copyable arm below or an
/// explicit full specialization.
template <typename C> struct SaveSerialize;

/// Trivially-copyable fast path: store the raw byte image little-endian-
/// agnostically (host layout — the snapshot is a same-machine round-trip
/// contract; cross-endian is out of scope).
template <typename C>
    requires std::is_trivially_copyable_v<C>
struct SaveSerialize<C> {
    static void write(IRAsset::BinaryWriter &w, const C &value) {
        w.writeBytes(&value, sizeof(C));
    }

    static IRAsset::Result<C> read(IRAsset::BinaryReader &r) {
        C value{};
        IRAsset::BinaryStatus status = r.readBytes(&value, sizeof(C));
        if (!status.ok()) {
            return IRAsset::Result<C>::error(status.code_, std::move(status.message_));
        }
        return IRAsset::Result<C>::success(value);
    }
};

/// True iff `SaveSerialize<C>` is usable — the trivially-copyable arm or an
/// explicit specialization. Self-detecting by construction: writing the
/// serializer IS the opt-in, so there is no second bookkeeping step to
/// forget. `makeDefaultSaveRegistry` filters `AllEngineComponents` on this,
/// and `SaveRegistry::registerComponent` asserts it.
template <typename C>
concept SaveSerializable =
    requires(IRAsset::BinaryWriter &w, const C &value, IRAsset::BinaryReader &r) {
        SaveSerialize<C>::write(w, value);
        { SaveSerialize<C>::read(r) } -> std::same_as<IRAsset::Result<C>>;
    };

} // namespace IRWorld

#endif /* SAVE_SERIALIZE_H */

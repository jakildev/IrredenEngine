#ifndef SAVE_SERIALIZE_H
#define SAVE_SERIALIZE_H

/// Per-component binary (de)serialization customization point for the ECS
/// world snapshot (`world_snapshot.hpp`). P1 (`save_trait.hpp`) decides
/// *whether* a component is saved and at what schema version; this header
/// decides *how* one instance turns into bytes.
///
/// The primary template is a trivially-copyable fast path: any component
/// that is `std::is_trivially_copyable` round-trips as a raw byte image,
/// which covers the bulk of plain gameplay data (positions, velocities,
/// timers, flags, small PODs). A component that owns heap storage
/// (`std::string`, `std::vector`, resource handles) is NOT trivially
/// copyable — a raw memcpy would persist dangling pointers — so it must
/// provide an explicit `SaveSerialize<C>` specialization. The
/// `static_assert` in the primary template turns "opted in but no
/// serializer" into a compile error rather than a silent corrupt save.
///
/// Determinism note: the byte image of a trivially-copyable struct
/// includes padding. Padding bytes are stable for a fixed in-memory value
/// within a session, so a same-session double-save is byte-identical
/// (world-snapshot criterion 6). Cross-session byte-stability of padding
/// is a separate concern deferred to P4/W-8.

#include <irreden/asset/binary_io.hpp>

#include <cstddef>
#include <type_traits>

namespace IRWorld {

/// Customization point: `write` serializes one `const C&`; `read` pulls
/// one `C` back. Specialize for any component that is not trivially
/// copyable. The default requires trivial-copyability and stores the raw
/// byte image little-endian-agnostically (host layout — the snapshot is a
/// same-machine round-trip contract; cross-endian is out of scope).
template <typename C> struct SaveSerialize {
    static_assert(
        std::is_trivially_copyable_v<C>,
        "SaveSerialize<C>: C is not trivially copyable — provide an explicit "
        "SaveSerialize<C> specialization (e.g. for components owning std::string / "
        "std::vector / resource handles). The primary template only handles PODs."
    );

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

} // namespace IRWorld

#endif /* SAVE_SERIALIZE_H */

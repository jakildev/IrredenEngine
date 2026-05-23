#ifndef SYSTEM_ACCESS_H
#define SYSTEM_ACCESS_H

#include <irreden/system/ir_system_types.hpp>

#include <cstddef>
#include <tuple>
#include <type_traits>

/// SystemAccess derivation — Phase 1 of the multithreading epic (#226).
///
/// `deriveAccessFromSignature<TickFn, Components...>()` returns a
/// constexpr descriptor of a system's component access set. T-222 will
/// consume the descriptor at registration time to validate that
/// PARALLEL_FOR systems don't write a component another concurrently-
/// scheduled system reads; T-224 will compose the same descriptor
/// across pipeline groups; T-225 will use the spawn/destroy flags to
/// route the right deferred-mutation path.
///
/// **Unused in this phase.** Pure unit-test surface for now.

namespace IRSystem {

// ----------------------------------------------------------------------
// Tag types
// ----------------------------------------------------------------------

/// Marker: the system creates entities. Composes with the inferred
/// write set; also flips `mutates_archetype_graph`.
struct Spawns {};

/// Marker: the system destroys entities. Same composition as `Spawns`.
struct Destroys {};

/// Marker: the system must run on the main thread regardless of the
/// scheduler's inferred safety. Overrides ParallelSafe.
struct MainThread {};

/// Marker: the caller declares the body parallel-safe even though the
/// inferred access set might suggest otherwise (e.g. the body touches
/// a manager singleton through a documented thread-safe surface).
struct ParallelSafe {};

/// Add component types to the inferred read set without listing them
/// in the iterating archetype filter — used when a system reads a
/// component on a *different* entity (looked up via a stored EntityId).
template <typename... Ts> struct AlsoReads {};

/// Symmetric to `AlsoReads` for writes on foreign entities.
template <typename... Ts> struct AlsoWrites {};

// ----------------------------------------------------------------------
// Type-identity key (constexpr-comparable across instantiations)
// ----------------------------------------------------------------------

namespace detail {

/// One static byte per template instantiation; `&value` is a unique
/// address per `T`, constexpr-comparable to itself across translation
/// units. Used as the access-set element so `SystemAccess` stays
/// constexpr without depending on the runtime `ComponentId` registry.
template <typename T> struct TypeKey {
    static constexpr int kSentinel = 0;
};

} // namespace detail

template <typename T>
inline constexpr const void *typeKey = &detail::TypeKey<std::remove_cv_t<T>>::kSentinel;

// ----------------------------------------------------------------------
// SystemAccess descriptor
// ----------------------------------------------------------------------

struct SystemAccess {
    /// Generous fixed cap so the struct stays a literal type. Tick
    /// functions in practice take 2-4 components; doubling to 16
    /// covers tagged `AlsoReads<...>`/`AlsoWrites<...>` chains
    /// without growing the struct's static footprint past 256 bytes.
    /// `appendRead`/`appendWrite` silently drop entries beyond this
    /// limit — intentional for Phase 1 simplicity (unreachable in
    /// practice with the current 2–4 component cap).
    static constexpr std::size_t kMaxAccess = 16;

    const void *reads_[kMaxAccess]{};
    std::size_t readCount_{0};
    const void *writes_[kMaxAccess]{};
    std::size_t writeCount_{0};

    bool usesEntityId_{false};
    bool isBatchForm_{false};
    bool mainThreadOnly_{false};
    bool parallelSafe_{false};
    bool spawns_{false};
    bool destroys_{false};
    bool mutatesArchetypeGraph_{false};

    constexpr bool readsType(const void *key) const {
        for (std::size_t i = 0; i < readCount_; ++i) {
            if (reads_[i] == key) {
                return true;
            }
        }
        return false;
    }
    constexpr bool writesType(const void *key) const {
        for (std::size_t i = 0; i < writeCount_; ++i) {
            if (writes_[i] == key) {
                return true;
            }
        }
        return false;
    }

    template <typename T> constexpr bool readsType() const { return readsType(typeKey<T>); }
    template <typename T> constexpr bool writesType() const { return writesType(typeKey<T>); }
};

// ----------------------------------------------------------------------
// Tag detection
// ----------------------------------------------------------------------

namespace detail {

template <typename T> struct IsExclude : std::false_type {};
template <typename... Ts> struct IsExclude<Exclude<Ts...>> : std::true_type {};

template <typename T> struct IsSpawnsTag : std::false_type {};
template <> struct IsSpawnsTag<Spawns> : std::true_type {};

template <typename T> struct IsDestroysTag : std::false_type {};
template <> struct IsDestroysTag<Destroys> : std::true_type {};

template <typename T> struct IsMainThreadTag : std::false_type {};
template <> struct IsMainThreadTag<MainThread> : std::true_type {};

template <typename T> struct IsParallelSafeTag : std::false_type {};
template <> struct IsParallelSafeTag<ParallelSafe> : std::true_type {};

template <typename T> struct IsAlsoReads : std::false_type {
    using Types = std::tuple<>;
};
template <typename... Ts> struct IsAlsoReads<AlsoReads<Ts...>> : std::true_type {
    using Types = std::tuple<Ts...>;
};

template <typename T> struct IsAlsoWrites : std::false_type {
    using Types = std::tuple<>;
};
template <typename... Ts> struct IsAlsoWrites<AlsoWrites<Ts...>> : std::true_type {
    using Types = std::tuple<Ts...>;
};

template <typename T>
inline constexpr bool kIsTag = IsExclude<T>::value || IsSpawnsTag<T>::value
                               || IsDestroysTag<T>::value || IsMainThreadTag<T>::value
                               || IsParallelSafeTag<T>::value || IsAlsoReads<T>::value
                               || IsAlsoWrites<T>::value;

// ----------------------------------------------------------------------
// Access-set assembly
// ----------------------------------------------------------------------

constexpr void appendRead(SystemAccess &out, const void *key) {
    if (out.readCount_ >= SystemAccess::kMaxAccess) {
        return;
    }
    for (std::size_t i = 0; i < out.readCount_; ++i) {
        if (out.reads_[i] == key) {
            return;
        }
    }
    out.reads_[out.readCount_++] = key;
}

constexpr void appendWrite(SystemAccess &out, const void *key) {
    if (out.writeCount_ >= SystemAccess::kMaxAccess) {
        return;
    }
    for (std::size_t i = 0; i < out.writeCount_; ++i) {
        if (out.writes_[i] == key) {
            return;
        }
    }
    out.writes_[out.writeCount_++] = key;
}

template <typename... Extras>
constexpr void applyExtraReads(SystemAccess &out, std::tuple<Extras...>) {
    (appendRead(out, typeKey<std::remove_cvref_t<Extras>>), ...);
}

template <typename... Extras>
constexpr void applyExtraWrites(SystemAccess &out, std::tuple<Extras...>) {
    (appendWrite(out, typeKey<std::remove_cvref_t<Extras>>), ...);
}

template <typename T> constexpr void applyComponent(SystemAccess &out) {
    if constexpr (IsExclude<T>::value) {
        // Exclude<...> is an archetype-matcher concern, not access.
        return;
    } else if constexpr (IsSpawnsTag<T>::value) {
        out.spawns_ = true;
        out.mutatesArchetypeGraph_ = true;
    } else if constexpr (IsDestroysTag<T>::value) {
        out.destroys_ = true;
        out.mutatesArchetypeGraph_ = true;
    } else if constexpr (IsMainThreadTag<T>::value) {
        out.mainThreadOnly_ = true;
    } else if constexpr (IsParallelSafeTag<T>::value) {
        out.parallelSafe_ = true;
    } else if constexpr (IsAlsoReads<T>::value) {
        applyExtraReads(out, typename IsAlsoReads<T>::Types{});
    } else if constexpr (IsAlsoWrites<T>::value) {
        applyExtraWrites(out, typename IsAlsoWrites<T>::Types{});
    } else if constexpr (std::is_const_v<T>) {
        // `const C_Foo` in the template pack → caller declares this
        // component read-only. Until creations land the const opt-in
        // (T-222 follow-up), most existing systems will land in the
        // writes set — that's the conservative-correct default.
        appendRead(out, typeKey<std::remove_cv_t<T>>);
    } else {
        appendWrite(out, typeKey<std::remove_cv_t<T>>);
    }
}

// Strip const/ref off the user's `Components...` pack before probing
// invocability — the existing concepts expect bare `T` (they add the
// `&` themselves).
template <typename T> using StripForConcept = std::remove_cvref_t<T>;

} // namespace detail

/// Constexpr trait. Returns the access descriptor for a system whose
/// tick signature is `TickFn` and whose template pack is
/// `Components...`. Composes:
///
/// - Per-component const-ness in the pack → reads vs writes.
/// - Tag types (`Spawns`, `Destroys`, `MainThread`, `ParallelSafe`,
///   `AlsoReads<...>`, `AlsoWrites<...>`) → boolean flags + extra
///   access entries on top of the inferred set.
/// - `Exclude<...>` → contributes nothing (archetype-matcher concern).
/// - Signature form (per-component vs entity-id vs batch) via the
///   existing `InvocableWithEntityId` / `InvocableWithNodeVectors`
///   concepts that drive `createSystem`'s dispatch.
template <typename TickFn, typename... Components>
constexpr SystemAccess deriveAccessFromSignature() {
    SystemAccess out{};
    (detail::applyComponent<Components>(out), ...);

    if constexpr (InvocableWithEntityId<TickFn, detail::StripForConcept<Components>...>) {
        out.usesEntityId_ = true;
    }
    if constexpr (InvocableWithNodeVectors<TickFn, detail::StripForConcept<Components>...>) {
        out.isBatchForm_ = true;
    }
    return out;
}

} // namespace IRSystem

#endif /* SYSTEM_ACCESS_H */

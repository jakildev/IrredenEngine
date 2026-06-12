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
    bool isRelationForm_{false};
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

    template <typename T> constexpr bool readsType() const {
        return readsType(typeKey<T>);
    }
    template <typename T> constexpr bool writesType() const {
        return writesType(typeKey<T>);
    }
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
inline constexpr bool kIsTag =
    IsExclude<T>::value || IsSpawnsTag<T>::value || IsDestroysTag<T>::value ||
    IsMainThreadTag<T>::value || IsParallelSafeTag<T>::value || IsAlsoReads<T>::value ||
    IsAlsoWrites<T>::value;

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

// Tag-filter scaffolding for the invocability probes below. The
// Components pack must be filtered because tag types (`MainThread`,
// `ParallelSafe`, `Spawns`, ...) and `Exclude<...>` are not real tick
// parameters; leaving them in front of `InvocableWithEntityId` /
// `InvocableWithNodeVectors` makes the probe fail on every system that
// mixes a real signature with tags, so `usesEntityId_` /
// `isBatchForm_` stay false. T-328 sub-task D.
//
// `IRSystem::detail::TypeList` is already defined in ir_system_types.hpp
// (used by the Exclude<...> partitioner); we reuse it here.

// Recursive head/tail filter: drop `Head` from the result iff
// `kIsTag<Head>` is true. Mirrors the same predicate `applyComponent`
// already uses to classify the pack.
template <typename... Components> struct FilterTagsImpl {
    using type = TypeList<>;
};

template <typename Head, typename... Tail> struct FilterTagsImpl<Head, Tail...> {
  private:
    using TailFiltered = typename FilterTagsImpl<Tail...>::type;

    template <typename TL> struct Prepend;
    template <typename... Existing> struct Prepend<TypeList<Existing...>> {
        using type = TypeList<Head, Existing...>;
    };

  public:
    using type =
        std::conditional_t<kIsTag<Head>, TailFiltered, typename Prepend<TailFiltered>::type>;
};

template <typename... Components> using FilterTags_t = typename FilterTagsImpl<Components...>::type;

// Apply the invocability probes against an already-filtered TypeList.
// Specialization on `TypeList<NonTags...>` unpacks the filtered pack
// so the existing concepts can be instantiated with bare `T` args.
template <typename TickFn, typename TL> struct ProbeSignatureForms;

template <typename TickFn, typename... NonTags>
struct ProbeSignatureForms<TickFn, TypeList<NonTags...>> {
    static constexpr bool usesEntityId = InvocableWithEntityId<TickFn, NonTags...>;
    static constexpr bool isBatchForm = InvocableWithNodeVectors<TickFn, NonTags...>;
};

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
///   concepts that drive `createSystem`'s dispatch. Tag types are
///   filtered out of the pack before probing so a tick that combines
///   a real signature (e.g. `void(EntityId, C_Foo&)`) with tags
///   (e.g. `ParallelSafe`, `MainThread`) still derives
///   `usesEntityId_` / `isBatchForm_` correctly.
///
/// `isRelationForm_` is NOT set here — the relation form depends on a
/// second template pack (`RelationComponents...`) that cannot be
/// disambiguated alongside `Components...` in a free-function template
/// (see the TODO at `InvocableWithOptionalRelations` in
/// ir_system_types.hpp). The createSystem wrapper has both packs in
/// scope and folds the bit in after the call.
template <typename TickFn, typename... Components>
constexpr SystemAccess deriveAccessFromSignature() {
    SystemAccess out{};
    (detail::applyComponent<Components>(out), ...);

    using NonTags = detail::FilterTags_t<detail::StripForConcept<Components>...>;
    using Probe = detail::ProbeSignatureForms<TickFn, NonTags>;
    if constexpr (Probe::usesEntityId) {
        out.usesEntityId_ = true;
    }
    if constexpr (Probe::isBatchForm) {
        out.isBatchForm_ = true;
    }
    return out;
}

// ----------------------------------------------------------------------
// Cross-system pipeline-group conflict check (T-224)
// ----------------------------------------------------------------------

/// One conflict surfaced between two systems in the same pipeline
/// group. Returned by `findPipelineGroupConflict` so the caller (the
/// SystemManager validator, or a unit test) can render a precise
/// diagnostic that names both systems and the offending component.
///
/// `kind_` selects which conflict fired; `componentKey_` is the
/// `typeKey<T>` of the offending component for the write/write,
/// write/read, and read/write cases (`nullptr` for the kind-only
/// `MAIN_THREAD_IN_GROUP` case).
///
/// `TWO_SPAWNERS` is retained for ABI/header stability — T-225 lifted
/// the rule that produced it, so `findPipelineGroupConflict` never
/// returns this kind anymore. Removing the enum value would break
/// existing exhaustive switches in downstream code; leave it.
enum class GroupConflictKind {
    NONE,
    MAIN_THREAD_IN_GROUP,
    WRITE_WRITE,
    WRITE_READ, // A writes, B reads
    READ_WRITE, // A reads, B writes
    TWO_SPAWNERS,
    /// T-225 lifted the single-mutator-with-sibling rule — per-worker
    /// deferred-mutation buffers make any mutator safe in a parallel
    /// group. Retained for exhaustive-switch ABI stability (like
    /// `TWO_SPAWNERS`); `findPipelineGroupConflict` never returns it.
    MUTATOR_IN_PARALLEL_GROUP,
};

struct GroupConflict {
    GroupConflictKind kind_{GroupConflictKind::NONE};
    std::size_t indexA_{0};
    std::size_t indexB_{0};
    const void *componentKey_{nullptr};
};

/// Returns the first conflict between distinct accesses in
/// `accesses[0..n)`. Returns `kind_ == NONE` when the group is clean.
///
/// The validator is the canonical "Phase 3" cross-system check from
/// the multithreading epic (#226 Layer 4). Scan order:
///
/// 1. `MAIN_THREAD_IN_GROUP` — checked first across all systems so
///    the diagnostic surfaces the strongest claim ("MAIN_THREAD never
///    co-executes") when multiple conflicts coexist.
/// 2. Pairwise (i<j) conflict checks. Within each pair, scanned in
///    order: component-set overlaps (`WRITE_WRITE`, `WRITE_READ`,
///    `READ_WRITE`). The kind names which side is the writer so
///    callers can render the directional message without re-querying
///    the accesses.
///
/// T-225 lifted both the pairwise `TWO_SPAWNERS` rule and the
/// broader `MUTATOR_IN_PARALLEL_GROUP` rule. Per-worker
/// deferred-mutation buffers route every `setComponentDeferred` /
/// `removeComponentDeferred` / `markEntityForDeletion` /
/// `createEntity` call into a worker-private slot; the main thread
/// drains every slot serially at the end of each group via
/// `flushStructuralChanges`. Any number of `Spawns` / `Destroys`
/// systems can therefore share a group safely as long as no
/// component-column conflict applies (rule 2 above).
///
/// Pure function over `SystemAccess` so unit tests can construct
/// fixtures without an `IRSystem::createSystem` path. No allocation,
/// constexpr-friendly. O(group² · components) in the worst case;
/// groups are tiny (≤8 in practice) so the cost is irrelevant.
inline GroupConflict findPipelineGroupConflict(const SystemAccess *accesses, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (accesses[i].mainThreadOnly_) {
            return GroupConflict{GroupConflictKind::MAIN_THREAD_IN_GROUP, i, i, nullptr};
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            const SystemAccess &a = accesses[i];
            const SystemAccess &b = accesses[j];
            // T-225: TWO_SPAWNERS and MUTATOR_IN_PARALLEL_GROUP are
            // no longer conflict conditions — per-worker deferred-
            // mutation buffers handle concurrent archetype-graph
            // mutation. Pairwise read/write conflicts below still apply.
            for (std::size_t wi = 0; wi < a.writeCount_; ++wi) {
                if (b.writesType(a.writes_[wi])) {
                    return GroupConflict{GroupConflictKind::WRITE_WRITE, i, j, a.writes_[wi]};
                }
            }
            for (std::size_t wi = 0; wi < a.writeCount_; ++wi) {
                if (b.readsType(a.writes_[wi])) {
                    return GroupConflict{GroupConflictKind::WRITE_READ, i, j, a.writes_[wi]};
                }
            }
            for (std::size_t wi = 0; wi < b.writeCount_; ++wi) {
                if (a.readsType(b.writes_[wi])) {
                    return GroupConflict{GroupConflictKind::READ_WRITE, i, j, b.writes_[wi]};
                }
            }
        }
    }
    return GroupConflict{};
}

} // namespace IRSystem

#endif /* SYSTEM_ACCESS_H */

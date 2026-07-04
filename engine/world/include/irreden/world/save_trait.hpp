#ifndef SAVE_TRAIT_H
#define SAVE_TRAIT_H

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <utility>

// Per-component save-policy trait. Pure compile-time fact table: no
// EntityManager/ComponentId includes, so a runtime serialize bridge can
// layer on top without a dependency cycle.

namespace IRWorld {

// Primary template = "no decision yet". Deliberately NOT "opt-out" — an
// engine component that never specializes this trait fails the
// completeness gate in save_component_inventory.hpp instead of silently
// being skipped by persistence.
template <typename C> struct SaveTrait {
    static constexpr bool kExplicit = false;
    static constexpr bool kSave = false;
    static constexpr std::uint32_t kSaveVersion = 0;
    // Stable on-disk identity (P2). The compile-time completeness gate
    // makes the primary template unreachable for any real save decision,
    // so `nullptr` here only ever surfaces if the macros are bypassed.
    static constexpr const char *kSaveName = nullptr;
};

template <typename C> constexpr bool shouldSave() {
    return SaveTrait<C>::kSave;
}

template <typename C> constexpr std::uint32_t saveVersion() {
    return SaveTrait<C>::kSaveVersion;
}

// Stable, compiler-independent on-disk name for component C: the source
// spelling captured by the IR_SAVE_OPT_IN/OPT_OUT macro at decision time.
// The world snapshot's CMPN name table keys columns by this string (Save
// Format Rule #2) so a file stays readable across sessions/compilers,
// where `typeid(C).name()` and the numeric ComponentId are not.
template <typename C> constexpr const char *saveName() {
    return SaveTrait<C>::kSaveName;
}

namespace detail {

template <typename Tuple, std::size_t... I>
constexpr bool allExplicitImpl(std::index_sequence<I...>) {
    return (... && SaveTrait<std::tuple_element_t<I, Tuple>>::kExplicit);
}

// True iff every type in Tuple has an explicit IR_SAVE_OPT_IN/OPT_OUT
// specialization — the compile-time completeness gate.
template <typename Tuple> constexpr bool allExplicit() {
    return allExplicitImpl<Tuple>(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

} // namespace detail

} // namespace IRWorld

// kSaveVersion lives on the trait, not the component struct: the
// world-snapshot serializes a *schema* defined by the (P2) per-component
// serialize function, not the struct's in-memory layout, so the schema's
// version belongs beside the policy decision.
#define IR_SAVE_OPT_IN(Type, Version)                                                              \
    namespace IRWorld {                                                                            \
    template <> struct SaveTrait<Type> {                                                           \
        static constexpr bool kExplicit = true;                                                    \
        static constexpr bool kSave = true;                                                        \
        static constexpr std::uint32_t kSaveVersion = (Version);                                   \
        static constexpr const char *kSaveName = #Type;                                            \
        static_assert(kSaveVersion >= 1, #Type " IR_SAVE_OPT_IN version must be >= 1");            \
    };                                                                                             \
    }

#define IR_SAVE_OPT_OUT(Type)                                                                      \
    namespace IRWorld {                                                                            \
    template <> struct SaveTrait<Type> {                                                           \
        static constexpr bool kExplicit = true;                                                    \
        static constexpr bool kSave = false;                                                       \
        static constexpr std::uint32_t kSaveVersion = 0;                                           \
        static constexpr const char *kSaveName = #Type;                                            \
    };                                                                                             \
    }

#endif /* SAVE_TRAIT_H */

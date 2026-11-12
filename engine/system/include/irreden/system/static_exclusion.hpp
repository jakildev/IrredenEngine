#ifndef STATIC_EXCLUSION_H
#define STATIC_EXCLUSION_H

#include <irreden/ir_entity.hpp>

#include <irreden/system/components/component_static.hpp>
#include <irreden/system/system_access.hpp>

#include <tuple>
#include <type_traits>

// The matching policy for static residents (docs/design/static-residents.md,
// "The matching policy"). Every registration and query lane calls
// `applyStaticExclusion`; it is the only implementation of the rule.

namespace IRSystem {

namespace detail {

constexpr IREntity::StaticGuard strongerGuard(IREntity::StaticGuard a, IREntity::StaticGuard b) {
    // INPUT outranks DERIVED outranks NONE.
    const auto rank = [](IREntity::StaticGuard guard) {
        return guard == IREntity::StaticGuard::INPUT
                   ? 2
                   : (guard == IREntity::StaticGuard::DERIVED ? 1 : 0);
    };
    return rank(a) >= rank(b) ? a : b;
}

template <typename... Extras>
constexpr IREntity::StaticGuard extrasWriteGuard(const std::tuple<Extras...> *) {
    IREntity::StaticGuard guard = IREntity::StaticGuard::NONE;
    ((guard = strongerGuard(guard, IREntity::kStaticGuard<std::remove_cvref_t<Extras>>)), ...);
    return guard;
}

template <typename T> constexpr IREntity::StaticGuard packEntryWriteGuard() {
    if constexpr (IsAlsoWrites<T>::value) {
        return extrasWriteGuard(static_cast<const typename IsAlsoWrites<T>::Types *>(nullptr));
    } else if constexpr (kIsTag<T> || std::is_const_v<T>) {
        return IREntity::StaticGuard::NONE;
    } else {
        return IREntity::kStaticGuard<std::remove_cvref_t<T>>;
    }
}

/// The highest `kStaticGuard` over the components a registration pack
/// declares a write to: its non-const component types plus
/// `AlsoWrites<...>`. Compile-time, so it resolves no component id.
template <typename... Pack> constexpr IREntity::StaticGuard writeGuardOf() {
    IREntity::StaticGuard guard = IREntity::StaticGuard::NONE;
    ((guard = strongerGuard(guard, packEntryWriteGuard<Pack>())), ...);
    return guard;
}

/// The same reduction for a lane that knows its write set only as ids.
inline IREntity::StaticGuard writeGuardOf(const IREntity::Archetype &writes) {
    const IREntity::EntityManager &entities = IREntity::getEntityManager();
    IREntity::StaticGuard guard = IREntity::StaticGuard::NONE;
    for (IREntity::ComponentId component : writes) {
        guard = strongerGuard(guard, entities.staticGuard(component));
    }
    return guard;
}

/// `C_Settled`'s id if any registration has named it, else
/// `kNullComponent`. Never registers the component.
inline IREntity::ComponentId settledComponentOrNull() {
    return IREntity::getEntityManager().findComponentType<IRComponents::C_Settled>();
}

/// A deriving system maintains state derived from a static resident's pose
/// inputs: its declared exclusions contain `C_Settled`, or it carries
/// `GatesSettled`.
inline bool isDeriving(const IREntity::Archetype &declaredExclude, bool gatesSettled) {
    if (gatesSettled) {
        return true;
    }
    const IREntity::ComponentId settled = settledComponentOrNull();
    return settled != IREntity::kNullComponent && declaredExclude.contains(settled);
}

/// A deriving system that writes a pose input would exclude itself from
/// the rows it must derive: a registration error every lane asserts on.
inline bool derivingWritesPoseInput(
    const IREntity::Archetype &declaredExclude, IREntity::StaticGuard writeGuard, bool gatesSettled
) {
    return writeGuard == IREntity::StaticGuard::INPUT && isDeriving(declaredExclude, gatesSettled);
}

/// The effective exclude archetype of a system or query: what it declared,
/// plus `C_Static` when it may not match a static archetype. That is when
/// it writes a pose input, or writes derived pose without being a deriving
/// system. A pure function of its arguments, so it is re-run whenever the
/// declared set changes.
inline IREntity::Archetype applyStaticExclusion(
    const IREntity::Archetype &declaredExclude, IREntity::StaticGuard writeGuard, bool gatesSettled
) {
    IREntity::Archetype effective = declaredExclude;
    if (writeGuard == IREntity::StaticGuard::INPUT ||
        (writeGuard == IREntity::StaticGuard::DERIVED &&
         !isDeriving(declaredExclude, gatesSettled))) {
        effective.insert(IREntity::getComponentType<IRComponents::C_Static>());
    }
    return effective;
}

} // namespace detail

} // namespace IRSystem

#endif /* STATIC_EXCLUSION_H */

#ifndef STATIC_EXCLUSION_H
#define STATIC_EXCLUSION_H

#include <irreden/ir_entity.hpp>

#include <irreden/system/components/component_static.hpp>
#include <irreden/system/system_access.hpp>

#include <string_view>
#include <tuple>
#include <type_traits>

// The matching policy for static residents (docs/design/static-residents.md,
// "The matching policy"). Every registration and query lane calls
// `applyStaticExclusion`; it is the only implementation of the rule.

namespace IRSystem {

namespace detail {

template <typename T> void appendGuardedWrite(IREntity::Archetype &out) {
    using Bare = std::remove_cvref_t<T>;
    if constexpr (IREntity::kStaticGuard<Bare> != IREntity::StaticGuard::NONE) {
        out.insert(IREntity::getComponentType<Bare>());
    }
}

template <typename... Extras>
void appendGuardedWrites(IREntity::Archetype &out, const std::tuple<Extras...> *) {
    (appendGuardedWrite<Extras>(out), ...);
}

template <typename T> void appendPackWrite(IREntity::Archetype &out) {
    if constexpr (IsAlsoWrites<T>::value) {
        appendGuardedWrites(out, static_cast<const typename IsAlsoWrites<T>::Types *>(nullptr));
    } else if constexpr (!kIsTag<T> && !std::is_const_v<T>) {
        appendGuardedWrite<T>(out);
    }
}

/// The guarded components a registration pack declares a write to: its
/// non-const component types plus `AlsoWrites<...>`, kept only where
/// `IREntity::kStaticGuard` is not `NONE`. The guard is read at compile
/// time, so an unguarded pack resolves no component id here.
template <typename... Pack> IREntity::Archetype guardedWrites() {
    IREntity::Archetype out;
    (appendPackWrite<Pack>(out), ...);
    return out;
}

/// `C_Settled`'s id if any registration has named it, else
/// `kNullComponent`. Never registers the component.
inline IREntity::ComponentId settledComponentOrNull() {
    return IREntity::getEntityManager().findComponentType<IRComponents::C_Settled>();
}

/// A deriving system maintains state derived from a static resident's pose
/// inputs: it excludes `C_Settled`, or it carries `GatesSettled`.
inline bool isDeriving(const IREntity::Archetype &exclude, bool gatesSettled) {
    if (gatesSettled) {
        return true;
    }
    const IREntity::ComponentId settled = settledComponentOrNull();
    return settled != IREntity::kNullComponent && exclude.contains(settled);
}

/// Adds `C_Static` to `exclude` when the system or query may not match a
/// static archetype: it writes a pose input, or it writes derived pose and
/// is not a deriving system. `writes` is the lane's declared write set;
/// only guarded components in it matter. Returns true when it added the
/// component. A deriving system that writes a pose input is a registration
/// error, because it would exclude itself from the rows it must derive.
inline bool applyStaticExclusion(
    const IREntity::Archetype &include,
    IREntity::Archetype &exclude,
    const IREntity::Archetype &writes,
    bool gatesSettled = false,
    std::string_view name = {}
) {
    IREntity::EntityManager &entities = IREntity::getEntityManager();
    IREntity::ComponentId inputWrite = IREntity::kNullComponent;
    bool writesDerived = false;
    for (IREntity::ComponentId component : writes) {
        const IREntity::StaticGuard guard = entities.staticGuard(component);
        if (guard == IREntity::StaticGuard::INPUT) {
            inputWrite = component;
        } else if (guard == IREntity::StaticGuard::DERIVED) {
            writesDerived = true;
        }
    }
    if (inputWrite == IREntity::kNullComponent && !writesDerived) {
        return false;
    }
    const bool deriving = isDeriving(exclude, gatesSettled);
    IR_ASSERT(
        !(deriving && inputWrite != IREntity::kNullComponent),
        "System '{}' is a deriving system (it excludes C_Settled or carries "
        "GatesSettled) and declares a write to pose input component {}. A pose "
        "writer never matches a static entity, so it could not derive the rows "
        "it claims. Declare the input const.",
        name,
        IREntity::makeComponentString(IREntity::Archetype{inputWrite})
    );
    if (deriving && inputWrite == IREntity::kNullComponent) {
        return false;
    }
    const IREntity::ComponentId staticTag = IREntity::getComponentType<IRComponents::C_Static>();
    IR_ASSERT(
        !include.contains(staticTag),
        "System '{}' includes C_Static and declares a write to a guarded pose "
        "component without being a deriving system, so it can never match. "
        "Declare the pose components const, or exclude C_Settled if the system "
        "derives from them.",
        name
    );
    return exclude.insert(staticTag).second;
}

} // namespace detail

} // namespace IRSystem

#endif /* STATIC_EXCLUSION_H */

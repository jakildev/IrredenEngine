#ifndef IR_SCRIPT_PREFAB_COMPONENT_FACTORY_H
#define IR_SCRIPT_PREFAB_COMPONENT_FACTORY_H

// Per-component "construct from Lua table" factory registry for the declarative
// `components = { C_Name = { field = value, ... } }` block in prefab files.
// See engine/script/CLAUDE.md "Prefab format → declarative components" for
// the v1 schema and acceptance contract.

#include <irreden/ir_entity.hpp>

#include <sol/sol.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace IRPrefab::Prefab {

// Construct a component from a Lua table of field overrides and attach it to
// `entity`. The factory owns both the construction and the `setComponent`
// call so callers don't need a type-erased setComponent surface.
using ComponentFactory = std::function<void(IREntity::EntityId, const sol::table &)>;

// Register `factory` under `componentName`. Re-registering an existing name
// overwrites the prior factory (consistent with `registerPrefab` semantics).
// The name must match the binding's `registerType<T>("C_Foo")` string so
// the prefab's `components = { C_Foo = ... }` key resolves.
void registerComponentFactory(std::string componentName, ComponentFactory factory);

// Returns nullptr if no factory is registered for `componentName`. Callers
// surface a diagnostic identifying the missing binding rather than silently
// dropping the entry.
const ComponentFactory *findComponentFactory(std::string_view componentName);

// Test helper. Clears every registered factory; production code never calls
// this (the registry is process-singleton, populated once at creation init).
void clearComponentFactories();

} // namespace IRPrefab::Prefab

namespace IRScript {

// Convenience template for `*_lua.hpp` bindings to opt in to declarative
// prefab spawning. Captures `setFields(C&, const sol::table&)` which copies
// any present overrides from the table into a default-constructed C; the
// factory then `setComponent`s the result onto the entity. Wire up by
// calling this from `bindLuaType<C>(LuaScript&)` after the usertype is
// registered — same lifetime as the rest of the binding.
template <typename C, typename SetterFn>
void registerComponentFactoryFor(std::string name, SetterFn setFields) {
    IRPrefab::Prefab::registerComponentFactory(
        std::move(name),
        [fn = std::move(setFields)](IREntity::EntityId entity, const sol::table &fields) {
            C component{};
            fn(component, fields);
            IREntity::setComponent(entity, std::move(component));
        }
    );
}

} // namespace IRScript

#endif /* IR_SCRIPT_PREFAB_COMPONENT_FACTORY_H */

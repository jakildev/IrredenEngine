#ifndef IR_SCRIPT_PREFAB_COMPONENT_FACTORY_H
#define IR_SCRIPT_PREFAB_COMPONENT_FACTORY_H

// Per-component "construct from Lua table" factory registry for the declarative
// `components = { C_Name = { field = value, ... } }` block a prefab root and
// each of its parts carry. See engine/script/CLAUDE.md "Prefab format" for the
// schema and acceptance contract.

#include <irreden/ir_entity.hpp>

#include <sol/sol.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace IRPrefab::Prefab {

// Construct a component from a Lua table of field overrides and attach it to
// `entity`. The factory owns both the construction and the `setComponent`
// call so callers don't need a type-erased setComponent surface.
using ComponentFactory = std::function<void(IREntity::EntityId, const sol::table &)>;

// Checks a field-override table before any factory runs; returns the problem
// (an unknown field, a value of the wrong type) or nullopt.
using ComponentFieldValidator = std::function<std::optional<std::string>(const sol::table &)>;

// Register `factory` under `componentName`. Re-registering an existing name
// overwrites the prior factory (consistent with `registerPrefab` semantics).
// The name must match the binding's `registerType<T>("C_Foo")` string so
// the prefab's `components = { C_Foo = ... }` key resolves. Without a
// `validator`, every table is accepted.
void registerComponentFactory(
    std::string componentName, ComponentFactory factory, ComponentFieldValidator validator = {}
);

// Drops the factory registered under `componentName`, if any. A registrant
// whose factory captures state that dies before process exit unregisters it
// there.
void unregisterComponentFactory(std::string_view componentName);

// Returns nullptr if no factory is registered for `componentName`. Callers
// surface a diagnostic identifying the missing binding rather than silently
// dropping the entry.
const ComponentFactory *findComponentFactory(std::string_view componentName);

// Runs the validator registered with `componentName`'s factory. Nullopt when
// the table is accepted or the factory registered no validator.
std::optional<std::string>
validateComponentFields(std::string_view componentName, const sol::table &fields);

// Every registered factory name, sorted.
std::vector<std::string> listComponentFactories();

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

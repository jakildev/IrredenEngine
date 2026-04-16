#pragma once

namespace IRScript {
class LuaScript;

/// Trait that gates whether a component type has a Lua binding.
/// Defaults to `false`.  A sibling `component_<name>_lua.hpp` file
/// specialises this to `true` and provides a `bindLuaType<T>` definition.
/// Without that file, `LuaScript::registerTypeFromTraits<T>()` will fail to
/// compile (static_assert) rather than fail at runtime.
template <typename T> inline constexpr bool kHasLuaBinding = false;

/// Registers the Lua usertype for @p T on @p script.
/// Must be specialised by `component_<name>_lua.hpp` alongside the
/// `kHasLuaBinding<T> = true` specialisation.
template <typename T> void bindLuaType(LuaScript &);
} // namespace IRScript

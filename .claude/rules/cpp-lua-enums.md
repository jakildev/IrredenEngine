---
paths:
  - "engine/script/**"
  - "engine/**/*_lua.hpp"
  - "creations/**/*.{hpp,cpp,h,cc}"
---

# Lua surface: enums and constants, never string-name lookups

Rule:

> **Never** check a Lua-side string name against a fixed set of values
> in C++ binding code (`if (s == "GRID") ... else if (s == "DETACHED")`,
> etc.). Expose the underlying C++ enum as a Lua table and accept the
> integer value at the binding boundary.

Why:

- **Single source of truth.** Renaming or extending the enum on the C++
  side only updates one place; Lua callers picked it up automatically.
  String-name checks drift — a new enum value gets bound in C++ but the
  Lua-side string list is forgotten, the schema silently rejects a
  legal value, and the bug surfaces months later.
- **Typo class moves up to load time.** `IRComponent.RotationMode.GRIB`
  fails at Lua-eval with a nil-access error. `rotation_mode = "GRIB"`
  fails at spawn time, deep inside an unrelated codepath, with a
  diagnostic the author has to map back to the typo.
- **Mirrors the existing pattern.** `IRSystem.SystemName.X` /
  `IRTime.X` / `IRCommand.CommandName.X` / `IRModifier.Transform.X` /
  `IRInput.{InputType,ButtonStatus,Key,Modifier}.X` all already work
  this way. New enum-typed Lua surfaces should match.

## What to do instead

1. Add a Lua table mirror in the relevant binding file
   (`engine/script/src/lua_*.cpp` or `engine/script/include/irreden/
   script/lua_*_bindings.hpp`). The canonical shape uses a one-line
   macro so the Lua key is derived from the C++ enum identifier and
   cannot drift:

   ```cpp
   sol::table rotationMode = lua.create_table();
   #define IR_BIND_ROTMODE(name) \
       rotationMode[#name] = static_cast<lua_Integer>(IRComponents::RotationMode::name)
       IR_BIND_ROTMODE(GRID);
       IR_BIND_ROTMODE(DETACHED);
   #undef IR_BIND_ROTMODE
   lua["IRComponent"]["RotationMode"] = rotationMode;
   ```

2. Read the value as `lua_Integer`, range-check it, cast to the enum:

   ```cpp
   sol::object obj = prefab["rotation_mode"];
   if (obj.valid() && obj.get_type() != sol::type::lua_nil) {
       if (obj.get_type() == sol::type::string) {
           return makeError(/* ... */ "string names are not accepted");
       }
       if (!obj.is<lua_Integer>()) {
           return makeError(/* ... */);
       }
       const lua_Integer raw = obj.as<lua_Integer>();
       if (raw < static_cast<lua_Integer>(MyEnum::kFirst) ||
           raw > static_cast<lua_Integer>(MyEnum::kLast)) {
           return makeError(/* ... */);
       }
       value = static_cast<MyEnum>(raw);
   }
   ```

3. Diagnose the legacy string path explicitly. A caller who passes
   `rotation_mode = 'GRID'` deserves a message that says "use
   `IRComponent.RotationMode.GRID` instead", not "type mismatch" or
   "unknown value".

4. Update every Lua-side caller, test fixture, and prefab `.lua` file
   to use the enum spelling. The cutover should land in the same PR
   as the C++ change — leaving even one string-typed caller behind
   defeats the rule.

## Allowlist (NOT covered by this rule)

- **Lua-defined component field names.** `arch.Comp:getField(i,
  "fieldName")` looks up a field by string at runtime; this is part
  of the Lua-defined ECS surface and is the documented hot-path
  alternative (`getField → getLuaField + cached index`) for callers
  that care about per-tick cost. See `engine/script/CLAUDE.md`
  "Two-tier accessor contract".
- **Modifier `fieldNameOrId`.** The modifier framework accepts
  either a string or a `FieldBindingId`. Strings round-trip through
  the registry — they're stable identifiers across the C++/Lua
  boundary, not a closed enum set. The same allowance applies to
  any registry-backed string id (component name → component id,
  prefab name → path).
- **User-facing string content.** Log messages, error diagnostics,
  prefab `id` strings, and prefab-file paths are strings by design.
  The rule is only about *enumerated values that have a C++ enum
  equivalent*.

## Audit hooks

Open-coded `if (s == "FOO") ... else if (s == "BAR") ...` chains in
`engine/script/src/**`, `engine/**/*_lua.hpp`, or creation Lua-binding
code are a smell. Replace them with the binding-table + enum-cast
pattern above when you touch the surrounding code.

---
paths:
  - "engine/script/**"
  - "**/*_lua.hpp"
  - "**/lua_*_bindings.hpp"
  - "creations/**/*.{hpp,cpp,h,cc}"
---

> Sweep with `fleet-rules-sweep`, never `rg`/`Grep` rooted at `creations/`
> — see [`README.md`](README.md). The `creations/**` injection scope is
> deliberately wider than the audit hook's globs; §"Audit hooks" says why.

# Lua surface: enums and constants, never string-name lookups

Rule:

> **Never** check a Lua-side string name against a fixed set of values in
> C++ binding code (`if (s == "GRID") ... else if (s == "DETACHED")`).
> Expose the underlying C++ enum as a Lua table and accept the integer value
> at the binding boundary.

One source of truth (the enum), a typo fails at Lua load time instead of
deep in a spawn path, and it matches the existing `IRSystem.SystemName.X` /
`IRTime.X` / `IRCommand.CommandName.X` / `IRModifier.Transform.X` /
`IRInput.{InputType,ButtonStatus,Key,Modifier}.X` tables.

## What to do instead

1. Mirror the enum as a Lua table in the binding file
   (`engine/script/src/lua_*.cpp` or
   `engine/script/include/irreden/script/lua_*_bindings.hpp`), deriving the
   key from the C++ identifier so it cannot drift:

   ```cpp
   sol::table rotationMode = lua.create_table();
   #define IR_BIND_ROTMODE(name) \
       rotationMode[#name] = static_cast<lua_Integer>(IRComponents::RotationMode::name)
       IR_BIND_ROTMODE(GRID);
       IR_BIND_ROTMODE(DETACHED);
   #undef IR_BIND_ROTMODE
   lua["IRComponent"]["RotationMode"] = rotationMode;
   ```

2. Read the value as `lua_Integer`, reject a string with a message that
   names the table (`use IRComponent.RotationMode.GRID`), range-check, cast.
   Most enums here have no `kFirst`/`kLast` sentinels, so range-check by
   switching over the enumerators and rejecting on fallthrough (`toSuite` in
   `lua_command_bindings.hpp`; `-Wswitch` then flags a new enumerator).
   Switch on the `lua_Integer` *before* the cast when the enum is unscoped
   with no fixed underlying type — converting an out-of-range value is
   unspecified.
3. Update every Lua caller, fixture, and prefab `.lua` in the same PR; one
   string-typed caller left behind defeats the rule.

## Allowlist (NOT covered by this rule)

- **Lua-defined component field names** — `arch.Comp:getField(i, "name")`
  is the Lua-defined ECS surface (`engine/script/CLAUDE.md` "Two-tier
  accessor contract").
- **Registry-backed string ids** — modifier `fieldNameOrId`, component name
  → id, prefab name → path: stable identifiers, not a closed enum set.
- **User-facing string content** — logs, diagnostics, prefab `id` strings,
  file paths, UI labels.
- **CLI argument values** — reach C++ from `argv`, never from Lua; they
  route through `IRArgs`' `.enumValue` (`engine/CLAUDE.md` §"CLI args go
  through `IRArgs`").
- **Binary / asset-format field tags** — `.vxs`, `.irkv`, …: the format
  owns its spelling and compatibility contract.
- **A single reserved-name or sentinel guard** — `if (name == "register")`
  is a validation check, not a value-set dispatch.

## Audit hooks

Open-coded `if (s == "FOO") ... else if (s == "BAR")` chains on the binding
surface are the smell; replace with the table + enum-cast pattern when
touching the code. The pattern is a bare string-compare, so the globs do all
the discrimination — scope them at the binding surface, not the tree
(sweeping `creations` wholesale returns only allowlisted CLI-argv /
asset-format / UI-label hits). A creation whose binding file is not named
`lua_*` adds its own `--glob`; a creation's `main*.cpp` is out of scope
(argv parses, owned by the `IRArgs` rule).

```
fleet-rules-sweep --glob 'engine/script/**' --glob '**/*_lua.hpp' \
  --glob '**/lua_*_bindings.hpp' --glob 'creations/**/lua_*.{hpp,cpp}' \
  --pattern '== *"'
```

A run reporting only the sites in §"Live deviations" is a clean pass.
Classify every other hit against the Allowlist before treating it as a
violation.

**`paths:` must cover every file the hook sweeps** — a rule enforced on a
file it was never injected into cannot reach the author who would widen the
glob. The frontmatter therefore keeps `creations/**/*.{hpp,cpp,h,cc}`
(creation binding files have no mandated name) and mirrors the hook's
tree-wide `**/*_lua.hpp` / `**/lua_*_bindings.hpp` spellings verbatim (a
prefix such as `engine/**/` would silently drop `test/` and `tools/`). Check
it whenever either glob set changes:

```
fleet-rules-sweep --files-only --pattern '.' --glob 'engine/script/**' \
  --glob '**/*_lua.hpp' --glob '**/lua_*_bindings.hpp' \
  --glob 'creations/**/lua_*.{hpp,cpp}' | sort > /tmp/detected
fleet-rules-sweep --files-only --pattern '.' --glob 'engine/script/**' \
  --glob '**/*_lua.hpp' --glob '**/lua_*_bindings.hpp' \
  --glob 'creations/**/*.{hpp,cpp,h,cc}' | sort > /tmp/injected
comm -23 /tmp/detected /tmp/injected      # must be empty
```

## Live deviations

Cite by symbol, not line. Two genuine deviations, grandfathered — both tags
are documented public schema (`engine/script/CLAUDE.md` §"Lua-defined
components", §"Per-system mode override") read by the build-time codegen
tool `cmake/lua_codegen/` as well as the runtime, so the swap is a
schema-breaking design call for the human, not a mechanical fix. New code on
this surface still follows the rule; don't migrate these in an unrelated PR.

- `engine/script/src/lua_script.cpp`, `parseExplicitTypeTag` — maps the
  component schema's `type = "int"` / `"float"` / `"vec3"` / … tags onto
  `LuaFieldType`. No `IRComponent.FieldType` table is exposed.
- `engine/script/src/lua_script.cpp`, the `mode` dispatch in
  `IRSystem.registerSystem` — `"codegen"` / `"eval"` onto `EcsMode`. No Lua
  table is exposed.

Not a violation (listed so a clean run is interpretable):
`engine/script/include/irreden/script/lua_enum_def.hpp`, `enumName ==
"register"` — a single reserved-name guard (`engine/script/CLAUDE.md`
§"Lua-defined enums").

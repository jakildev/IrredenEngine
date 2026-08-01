---
paths:
  - "engine/script/**"
  - "engine/**/*_lua.hpp"
  - "engine/**/lua_*_bindings.hpp"
  - "creations/**/*.{hpp,cpp,h,cc}"
---

> **Sweeping for violations?** `paths:` is an injection scope, not a search
> root. `rg`/`Grep` rooted at `creations/` reads a **false clean** (#2739) —
> run detectors through `fleet-rules-sweep`. See [`README.md`](README.md).
> The `creations/**` injection scope is deliberately wider than the audit
> hook's globs; §"Audit hooks" says why.

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
  prefab `id` strings, prefab-file paths, and UI label text are strings
  by design. The rule is only about *enumerated values that have a C++
  enum equivalent*.
- **CLI argument values.** `--subdivision-mode full` reaches C++ as a
  string from `argv`, never from Lua, so this rule does not reach it —
  even when the value maps onto a C++ enum. That surface has its own
  rule: route it through `IRArgs`' `.enumValue` declaration rather than
  a hand-rolled compare chain (see `engine/CLAUDE.md` §"CLI args go
  through `IRArgs`").
- **Binary / asset-format field tags.** A value read out of a `.vxs`
  sidecar, `.irkv` store, or other on-disk format is part of that
  format's schema. The format owns its spelling and its compatibility
  contract; the Lua enum table is not in the loop.
- **A single reserved-name or sentinel guard.** `if (name ==
  "register")` rejecting one reserved key is a validation check, not a
  value-set dispatch. The rule targets *chains* that enumerate a closed
  set — one comparison against one literal has no enum to mirror.

## Audit hooks

Open-coded `if (s == "FOO") ... else if (s == "BAR") ...` chains in
`engine/script/**`, `engine/**/*_lua.hpp`, or creation Lua-binding
code are a smell. Replace them with the binding-table + enum-cast
pattern above when you touch the surrounding code. (`engine/script/**`,
not just its `src/`: one of the deviations below lives under
`engine/script/include/`.)

Run the hook through `fleet-rules-sweep`, never `rg creations` — this is the
detector whose false clean surfaced #2739 (0 hits reported on a tree with 4
matching files):

```
fleet-rules-sweep --glob 'engine/script/**' --glob '**/*_lua.hpp' \
  --glob '**/lua_*_bindings.hpp' --glob 'creations/**/lua_*.{hpp,cpp}' \
  --pattern '== *"'
```

**Scope the globs at the binding surface, not at the tree.** The pattern is a
bare string-compare match — it cannot tell a Lua-sourced value from any other
`std::string`, so the glob is doing all the discrimination. Sweeping
`creations` wholesale instead returns 41 hits in 7 files of which **30 are
allowlisted classes above** (25 CLI-argv parses, 4 asset-format tags, 1 UI
label) — none of them reachable from Lua, and none of them this rule's to fix
(#2745). The globs above are the surface this rule's prose actually names: 81
files, reporting **12 hits in 2 files**, every one of them classified below.

The `creations/**/lua_*.{hpp,cpp}` glob is load-bearing. It covers the six real
creation-side binding files — `lua_bindings.{hpp,cpp}` + `lua_component_pack.hpp`
under `demos/default` and `demos/sprite_demo` — which are clean today. Drop it
and the detector narrows until it structurally cannot see creation binding code:
the same false-clean shape as #2739, spelled with a glob instead of a walker. A
creation whose binding file is named something else adds its own `--glob`.

`**/lua_*_bindings.hpp` is the **prefix** spelling the binding headers actually
use (`engine/script/include/irreden/script/lua_*_bindings.hpp`, as §"What to do
instead" names them). The suffix form `**/*_lua_bindings.hpp` matches nothing in
the tree — `fleet-rules-sweep` reports `swept 0 file(s)` for it alone. The
correction is coverage-neutral (those headers already sit inside
`engine/script/**`, so the totals stay 81/12/2); it exists so a binding header
added *outside* `engine/script/` is swept instead of silently skipped.

**`paths:` stays wider than these globs — deliberately.** The frontmatter keeps
`creations/**/*.{hpp,cpp,h,cc}` (the shape `cpp-ecs.md`, `cpp-ecs-smells.md`,
and `cpp-math.md` share) rather than tracking
`creations/**/lua_*.{hpp,cpp}`. Injection and detection answer different
questions: the glob narrows because a bare `== "` pattern cannot tell a
Lua-sourced value from any other string compare — a constraint an author
*reading* the rule does not share. And the escape hatch above (a
differently-named binding file adds its own `--glob`) only fires if the rule
reached that author in the first place. Narrowing injection to `lua_*` would
hide it from exactly the person who needs to widen the glob, sealing the
detector's blind spot shut. `cpp-systems.md` can mirror its own detector scope
because `system_<name>.hpp` is a mandated filename (`engine/prefabs/CLAUDE.md`
§"File pattern"); the creation-side binding surface has no such spelling to
lean on.

**The invariant that argument implies: `paths:` must cover every file the hook
sweeps.** Wherever detection reaches a file injection doesn't, the rule is
enforced on an author it was never shown to — and the escape hatch above,
which is the whole reason the wide creations scope is justified, cannot fire.
**It holds today** — the swept set is a strict subset of the injected set
(81 ⊆ 141), because every binding header currently lives under
`engine/script/include/irreden/script/`, which `engine/script/**` already
injects. The frontmatter's `engine/**/lua_*_bindings.hpp` is **forward-looking,
not remedial**: the hook's `**/lua_*_bindings.hpp` glob matches a binding header
*anywhere* in the tree, so the first one placed outside `engine/script/` would
be swept while `engine/script/**` + `engine/**/*_lua.hpp` failed to inject it.
The entry closes that hole before it opens; it changes no counts today. Not
widened to `engine/**/*.{hpp,cpp,h,cc}` (the sibling shape): that
would inject a Lua-binding rule into every engine translation unit to reach a
surface with a consistent, greppable spelling. The creations side needs the
blunt glob because creation binding files have no mandated name; the engine
side does not.

Check the invariant, don't assume it — the two glob sets are edited
independently:

```
# swept by the hook
fleet-rules-sweep --files-only --pattern '.' --glob 'engine/script/**' \
  --glob '**/*_lua.hpp' --glob '**/lua_*_bindings.hpp' \
  --glob 'creations/**/lua_*.{hpp,cpp}' | sort > /tmp/detected
# injected by paths:
fleet-rules-sweep --files-only --pattern '.' --glob 'engine/script/**' \
  --glob 'engine/**/*_lua.hpp' --glob 'engine/**/lua_*_bindings.hpp' \
  --glob 'creations/**/*.{hpp,cpp,h,cc}' | sort > /tmp/injected
comm -23 /tmp/detected /tmp/injected      # must be empty (81 vs 141 today)
```

A creation's `main*.cpp` is deliberately **not** in scope, `main_lua.cpp`
included — its string compares are CLI-argv parses (three in
`lua_perf_grid/main_lua.cpp`), allowlisted above and owned by the `IRArgs` rule.
Re-add it only together with a pattern that can tell argv from a Lua value.

Classify every surviving hit against the Allowlist before treating it as a
violation. A run that reports only the sites in §"Live deviations" is a clean
pass.

## Live deviations

The hook's 12 surviving hits, classified. Two are genuine deviations; one is a
known-allowlisted shape kept here so a clean run is interpretable without
re-deriving it.

- `engine/script/src/lua_script.cpp:93-115` — `parseExplicitTypeTag` maps the
  Lua component schema's `type = "int"` / `"float"` / `"vec3"` / `"quat"` … tags
  onto `LuaFieldType` (`lua_component_data.hpp:37`, 9 enumerators). **Genuine
  deviation, deferred.** No `IRComponent.FieldType` table is exposed.
- `engine/script/src/lua_script.cpp:880-891` — `IRSystem.registerSystem`'s
  `mode = "codegen"` / `"eval"` maps onto `EcsMode`
  (`ir_script_types.hpp:42`). **Genuine deviation, deferred.** No Lua table is
  exposed.
- `engine/script/include/irreden/script/lua_enum_def.hpp:67` — `enumName ==
  "register"` reserved-name guard. **Not a violation** — a single sentinel
  check per the Allowlist, documented in `engine/script/CLAUDE.md`
  §"Lua-defined enums".

**Why the two deviations are deferred rather than tracked as migrations.**
Both tags are documented public schema (`engine/script/CLAUDE.md` §"Lua-defined
components", §"Per-system mode override") and both are read by the *build-time*
codegen tool (`cmake/lua_codegen/`) out of the same `.lua` files as the runtime.
Swapping either for an integer table is a schema-breaking change across every
creation `.lua` **and** the codegen tool — a design call with a compatibility
story, not the mechanical binding-table fix §"What to do instead" describes. No
tracking issue exists yet because filing design-direction work is the human's
call, not a worker's; this register is the record until they make it.

New code on this surface still follows the rule — these two are grandfathered,
not a precedent. Don't migrate them in an unrelated PR.

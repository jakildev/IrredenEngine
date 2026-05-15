# Lua binding automation — codegen extension + shared default bindings

**Status:** Research note. No implementation tickets opened yet; the
recommendation below names three follow-ups that should be filed against
this document.

**Decision:**

- **(b) `registerStandardBindings(luaScript)`** — ship. Folds the math /
  enum / GUI-table boilerplate every demo re-lists today into one shared
  header. Uncontroversial; concrete savings; lands in one PR.
- **(a) codegen extension for the 39 existing `*_lua.hpp` files** — do
  NOT extend the codegen tool to emit these. Adopt a templated
  `bindSimple<C, ...>` helper for the trivial cases instead and keep the
  complex bindings hand-written. The codegen tool stays focused on the
  Lua-defined-component / Lua-defined-system DSL it was built for.

**Problem owner:** `engine/script/` + `cmake/lua_codegen/`. Touch points
listed under each option.

---

## What's actually there today

### 39 `*_lua.hpp` files, ~1569 lines total

```
$ find engine/prefabs/irreden -name '*_lua.hpp' -type f | wc -l
39
$ find engine/prefabs/irreden -name '*_lua.hpp' -exec wc -l {} +
... 1569 total
```

Average ~40 lines/file including the `#ifndef` guards. Three rough
shapes:

1. **Trivial bind-by-fields.** `engine/prefabs/irreden/render/components/component_light_source_lua.hpp`
   — registers `C_LightSource` with five overloaded constructors and
   seven `&C_LightSource::field_` member-pointer bindings. The shape is
   `registerType<C, Ctor1, Ctor2, ...>("C_X", "name", &C::field_, ...)`.
   The mechanical 80% case.
2. **Lambda-getter bindings for nested fields.**
   `engine/prefabs/irreden/common/components/component_position_3d_lua.hpp:15-21`
   binds `C_Position3D::pos_.x`, `.y`, `.z` via `[](C_Position3D &o)
   { return o.pos_.x; }` lambdas because the underlying `glm::vec3` lives
   one indirection deep. Same shape in
   `component_gui_position_lua.hpp:14-18`. Roughly a quarter of files.
3. **Companion enums + member functions + multi-ctor.**
   `component_light_source_lua.hpp:15-21` also binds the `LightType`
   enum alongside the component. `component_zoom_level_lua.hpp:17-21`
   binds `C_ZoomLevel::zoomIn` / `zoomOut` zero-arg member functions
   alongside the `zoom` field. A handful of files combine all of these.

### Per-demo Lua-bindings boilerplate

`creations/demos/default/lua_bindings.cpp:31-132` hand-registers, line for
line:

- `IRMath::Color` (4 fields)
- `IRMath::ivec3` (3 fields)
- `IRMath::vec3` (3 fields + `+` / `-` meta-functions)
- `IREasingFunctions` enum (30 values)
- `IRMidiNote` enum (~88 values)
- `TextAlignH` / `TextAlignV` tables (3 values each)

`creations/demos/sprite_demo/lua_bindings.cpp:22-40` re-registers
`Color`, `vec2`, `vec4` — same shape, slightly different subset. Any
new demo that exposes Lua adds another copy.

### What's already shared

- `engine/script/include/irreden/script/ir_script_utils.hpp` —
  `IRScript::vec3FromLua(sol::object)` covers the userdata-or-table
  parse direction. The opposite direction (registering `vec3` etc. with
  sol2) is not shared anywhere.
- `engine/prefabs/irreden/common/modifier_lua.hpp` — central
  `bindModifierNamespace(luaScript)` for the `ir.modifier.*` surface.
  Pattern matches what (b) needs for math + enums.

### What the codegen tool emits today

`cmake/lua_codegen/main.cpp:603-647` already emits the same shape
`registerType<C_Foo, ...>("Foo", "field", &C_Foo::field_, ...)` we
write by hand — but only for *Lua-defined* components whose fields
are int32/float/bool/string. That path is wired up: capture the schema
from a Lua-stub `IRComponent.register(...)` call, emit the C++ struct,
emit the binding. The Lua-defined-component path landed in T-106; the
Lua-defined-system path in T-107; system-mode override in #587.

The question this note answers is *do we extend the same machinery to
emit bindings for hand-written C++ components* — i.e. components whose
`struct` exists in `engine/prefabs/.../component_X.hpp` already.

---

## (b) — Shared default bindings: `lua_bindings_default.hpp`

**Recommendation: ship.**

### Shape

```cpp
// engine/script/include/irreden/script/lua_bindings_default.hpp
#pragma once
#include <irreden/script/lua_script.hpp>

namespace IRScript {
inline void registerStandardBindings(LuaScript &luaScript) {
    // Math types: Color, vec2, vec3, vec4, ivec3, ivec2 (+ vec3
    // arithmetic metafunctions).
    // Enums: IREasingFunctions (30), IRMidiNote (88), TextAlignH (3),
    // TextAlignV (3).
    // Helper tables already shared across demos.
}
} // namespace IRScript
```

A demo's `lua_bindings.cpp` collapses to one line where ~110 lines used
to live:

```cpp
IRScript::registerStandardBindings(luaScript);
// ... plus this creation's own components and IR* namespace exposures.
```

### Why this is unambiguously a win

- The duplication is **real and identical**. The MIDI-note table is the
  same 88-line block in every demo that touches MIDI. Math types are
  literally cut-and-paste.
- It's not a binding-shape question. These types are already in
  `engine/math/` and `engine/audio/` — the only thing missing is one
  shared header that registers them.
- Idempotency is a non-problem: the helper is gated on a `static bool
  isRegistered` guard (`creations/demos/default/lua_bindings.cpp:20`);
  double-registration never occurs in practice.
- Net deletion. Roughly **−100 to −150 lines** across the two
  demos that currently ship their own `lua_bindings.cpp`
  (`default` deletes ~110 lines of math/enum/text-align registration;
  `sprite_demo` deletes ~20 lines of Color/vec2/vec4). Both replaced
  by one `registerStandardBindings(luaScript)` call. Grows as new
  demos with bindings land.
- No new build-time dep, no new file format, no codegen change.

### What stays per-creation

- The IR* namespace tables (`IRText`, `IREntity`, `IRRender`,
  `IRAudio`, `IRSpriteNamespace`). Each demo wires only the calls it
  uses; routing them through a single header would silently drag every
  engine subsystem into every demo's link line.
- `registerCreateEntityBatchFunction<...>` calls — the specific
  archetypes vary per demo.
- The demo's own `lua_component_pack.hpp` listing — the existing
  carefully-scoped pattern.

### Open question

Should `vec3` keep the `+ / -` metafunctions from
`creations/demos/default/lua_bindings.cpp:60-63` in the shared header,
or leave them as an opt-in second helper
`registerStandardMathMetafunctions(luaScript)`? Argument for inclusion:
they're cheap, and a demo that registers `vec3` almost certainly wants
to add two of them in Lua. Argument against: sol2 metafunction
registration is order-sensitive — `registerType<vec3>` returns the
usertype that the `[sol::meta_function::addition]` assignment then
mutates, and a creation that doesn't want operator overloading might
prefer the smaller surface. Default to **include** in v1 and split if
a creation later objects.

---

## (a) — Codegen for the 39 existing `*_lua.hpp` files

**Recommendation: do not extend codegen. Adopt a templated
`bindSimple<C, ...>` helper for the trivial 80% and keep complex
bindings hand-written.**

### What "the codegen approach" would actually cost

The Lua-defined codegen path works because the schema is *already in
Lua*. The tool runs the schema as Lua, captures the
`IRComponent.register("Hp", { current = 100, ... })` call via a stub,
and emits the matching C++ struct + binding. There is no parsing of
existing C++ — the schema is the source of truth.

To codegen bindings for the existing C++ components, the tool needs
the inverse: it must enumerate the fields of an *already-written* C++
struct. The options for that, in increasing cost order:

#### Option 1 — Stay hand-written (do nothing)

The serious case for this:

- **40 files × ~20 useful lines/file = ~800 lines of bindings.** Most
  of those lines are field names; you cannot generate field names that
  don't exist somewhere first. Whether they live in a `.lua_bind` schema
  or directly in C++, the line count is the same.
- **Per-file authoring cost is 5 minutes** for the trivial cases,
  ~20 minutes for the complex ones (enum companions, nested-field
  lambda getters). The marginal cost of adding a 40th, 50th, 60th file
  is bounded — it does not compound the way an actual content pipeline
  would.
- **The binding layer is API.** A `*_lua.hpp` file documents *which*
  fields are exposed to Lua, which is a real choice. Some
  fields (private flags, GPU resource handles, internal voxel indices)
  should NOT be Lua-bindable. Codegen-from-C++ has to encode that
  exclusion list somewhere — annotation comments, sidecar exclusion
  lists, or just exposing everything — and that machinery is its own
  source of bugs.
- **Codegen tool churn has a cost.** The current codegen tool's parser
  for the system-body DSL (`cmake/lua_codegen/system_dsl.cpp`) is the
  most fragile part of the build. Adding a second machinery (libclang
  or regex) to drive a parallel emission path would roughly double the
  surface for codegen-time errors.
- **Lua-defined components already cover the green-field path.** When
  a creation needs a new component AND the binding is trivial, the
  right move is `IRComponent.register("Hp", {...})` in Lua and let the
  existing codegen handle it. The 39 hand-written `*_lua.hpp` files
  represent the pre-codegen-era components — they're a finite,
  bounded set. New components born after T-106 default to Lua-side
  declaration when they fit.

This option is the runner-up to the templated helper below, not a
strawman.

#### Option 2 — `bindSimple<C, Ctor, ...>` templated helper

A header-only sol2 wrapper that takes a parameter pack of `name, &C::field_` pairs and calls
`registerType<C, Ctors...>(name, ...)` for the caller. Compile-time only;
no codegen tool runs.

```cpp
// engine/script/include/irreden/script/lua_bindings_helpers.hpp (new)
// One shared macro + helper to collapse the trivial bind shape:
namespace IRScript {

template <typename C, typename... Ctors, typename... Members>
inline void bindSimple(LuaScript &script,
                       const char *name,
                       Members&&... members) {
    script.registerType<C, Ctors...>(name, std::forward<Members>(members)...);
}

} // namespace IRScript

// A trivial *_lua.hpp file now reads:
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_Velocity3D> = true;
template <> inline void bindLuaType<IRComponents::C_Velocity3D>(LuaScript &s) {
    using IRComponents::C_Velocity3D;
    IRScript::bindSimple<C_Velocity3D,
                         C_Velocity3D(float,float,float),
                         C_Velocity3D(IRMath::vec3),
                         C_Velocity3D()>(s, "C_Velocity3D");
}
```

For the field-listing case, the helper adds nothing over the existing
`registerType` shape — the savings are real only when the *boilerplate*
(the `#ifndef` guard, the `template<>` specialisations) is the bulk of
the file. Looking at the actual numbers: `component_velocity_3d_lua.hpp`
is 21 lines, of which only 5 lines are the binding body. The guard and
specialisation framing dominate.

The honest follow-up move is one of:

- **Header-only macro.** A single `IR_BIND_LUA_COMPONENT(C, ctors..., fields...)`
  that emits the `kHasLuaBinding` specialization, the `bindLuaType` template,
  and the `registerType` call. Trades one form of boilerplate (template
  specialisations) for another (macro invocations), but the file shrinks
  from ~20 lines to ~5.
- **Leave the framing alone.** The framing is mechanical to write and
  mechanical to read; a future grep for `kHasLuaBinding<C_Foo>` returns
  exactly the file that binds `C_Foo`. The discoverability matters.

**Pick "leave the framing alone"** unless a future PR adding ~5 trivial
components in one go reveals the macro form as obviously better. The
saving is too small per file to justify the indirection cost.

#### Option 3 — Sidecar `.lua_bind` schema files per component

Per component, ship a `component_x.lua_bind`:

```lua
return {
    name = "C_LightSource",
    ctors = {
        { "LightType", "Color", "float", "uint8_t", "vec3", "float", "float" },
        { },
    },
    fields = {
        type = "type_",
        emitColor = "emitColor_",
        intensity = "intensity_",
        radius = "radius_",
        direction = "direction_",
        coneAngleDeg = "coneAngleDeg_",
        ambient = "ambient_",
    },
    companion_enums = { "LightType" },
}
```

The codegen tool parses these, looks up `IRComponents::C_LightSource`
in `#include`'d headers, and emits the `bindLuaType` specialization.

Problems:

- The schema duplicates the C++ field name (`emitColor_` appears in
  both the C++ struct and the schema). Any rename has to be
  synchronized across two files. The current `*_lua.hpp` shape has the
  same duplication, so this is breakeven on maintenance — but the
  *cognitive* cost is higher because there are now THREE places
  (struct definition, schema, generated binding) instead of two
  (struct definition, hand-written binding).
- Companion enums need their own schema syntax. Member functions need
  another. Lambda getters for nested fields need another. The schema
  grows to handle every shape the C++ side already handles for free.
- The codegen tool stops being self-contained Lua → C++ and starts
  being "schema language inspired by Lua syntax."

Not recommended.

#### Option 4 — libclang at codegen time

The maximalist option. Add a libclang dep to the codegen tool; parse
the C++ component header; enumerate fields; emit the binding. Likely
the only path that can produce *zero-cost* bindings — write the
component, the binding is automatic.

Problems:

- **Hard dep on libclang at build time.** Currently the codegen tool
  builds in seconds from sol2 + LuaJIT. Adding libclang brings a ~50 MB
  dep, links against the system's libclang.so on Linux, and pulls in
  the entire Clang preprocessor stack. Build-time cost is real.
- **Cross-platform fragility.** WSL2 (the fleet env) has libclang
  packaged; Windows MSYS2 has a working libclang; macOS has
  Apple-clang's libclang which is API-compatible but version-skewed.
  Three platforms × two backends (OpenGL, Metal) means we already
  spend a lot of fleet time on platform parity. Adding libclang
  multiplies that surface.
- **Component-field exposure decisions need annotations.** A C++
  component has fields the binding should expose and fields it
  shouldn't (GPU handles, internal state). libclang can read the
  fields but cannot read the *intent*. Either: (i) bind everything and
  break encapsulation, (ii) require annotation comments
  (`// LUA: bind name=x`) that the codegen parses — at which point
  we've reinvented the schema-sidecar approach with extra steps.
- **Lambda-getter cases (nested fields) need code emission, not just
  field enumeration.** `C_Position3D::pos_.x` is a member of a member;
  libclang can see it, but generating `[](C_Position3D &o) { return
  o.pos_.x; }` requires synthesizing C++ code, not just reading.
- **Diagnostic quality.** When the parse fails, error messages from
  libclang are "the file fails to parse" with line number; mapping
  those back to "your component's binding will be missing" is its own
  UX problem.

Not recommended for v1. Revisit when component count crosses ~150 and
the per-file authoring cost actually starts to ache.

#### Option 5 — Regex parsing

A `cmake/lua_codegen` script reads each `component_x.hpp` with regex
and tries to identify fields. Quick to prototype, brittle in production.
Templates, macros, conditional `#ifdef`s, and inheritance break it.
Not recommended in any form.

#### Option 6 — C++26 `std::reflect`

The proposal (P2996) that would make this trivially correct. Not
yet shipped in any production compiler; the standardization timeline
makes it a multi-year horizon at best. Doesn't help today.

---

## What about the codegen tool's emission shape for new Lua-defined components?

Orthogonal to (a) and (b), one detail in
`cmake/lua_codegen/main.cpp:615-646` is worth flagging for a future
follow-up. The tool emits sol2 bindings with field names matching the
Lua schema verbatim (the trailing-`_` member-name convention is
preserved on the C++ side only):

```cpp
template <> inline void bindLuaType<IRComponents::C_Hp>(LuaScript &luaScript) {
    using IRComponents::C_Hp;
    luaScript.registerType<C_Hp, C_Hp(std::int32_t)>(
        "Hp",                              // sol2 user-type name
        "current", &C_Hp::current_         // Lua name → C++ member
    );
}
```

This means a CODEGEN-generated component's Lua-visible name is the
**unprefixed** name (`Hp`), not `C_Hp`. Lua-defined components are
spelled `Hp.new(...)` in Lua, but C++ components are spelled
`C_Foo.new(...)` — a small but real inconsistency
this is not yet documented as a deliberate decision — task 2 in the
implementation plan below captures exactly that; but it's worth a
second pass once both surfaces have production users).

Not a blocker for any of the work above; flagging for completeness.

---

## Implementation plan: three follow-up tasks

1. **`script: shared default bindings header — registerStandardBindings(luaScript)`**
   *Model: sonnet. Area: engine/script.*
   - Add `engine/script/include/irreden/script/lua_bindings_default.hpp`
     with `IRScript::registerStandardBindings(LuaScript &)`.
   - Register: `Color`, `vec2`, `vec3` (+ `+`/`-` metafunctions), `vec4`,
     `ivec2`, `ivec3`, `IREasingFunctions`, `IRMidiNote`, `TextAlignH`,
     `TextAlignV`.
   - Update `creations/demos/default/lua_bindings.cpp` and
     `creations/demos/sprite_demo/lua_bindings.cpp` to call the helper
     and delete the duplicated registrations. Other demos that grow
     Lua bindings later pick it up automatically.
   - Acceptance: every existing demo still builds and runs unchanged;
     net ~`-100..-150` lines today, growing as new Lua demos land.

2. **`script: codegen — match registered Lua name to C++ struct prefix`**
   *Model: sonnet. Area: cmake/lua_codegen, engine/script.*
   - Decide whether codegen'd-as-C++ Lua-defined components should be
     spelled `Hp` or `C_Hp` in Lua. Document the decision in
     `engine/script/CLAUDE.md`.
   - Pure decision + doc PR; code change only if the decision flips
     today's behavior.

3. **(Optional, only if a future PR motivates it)
   `script: IR_BIND_LUA_COMPONENT macro for trivial _lua.hpp files`**
   *Model: sonnet. Area: engine/script.*
   - File only if a PR adds ≥3 new trivial `*_lua.hpp` files in one
     sitting and the boilerplate becomes the obvious annoyance.
   - Adds a single `IR_BIND_LUA_COMPONENT(C, "C_X", Ctor1, Ctor2,
     "field", &C_X::field_, ...)` macro that emits `kHasLuaBinding`,
     `bindLuaType`, and the `registerType` call.
   - Migrate ~20 trivial files; leave the complex ones (enum
     companions, lambda getters, member functions) hand-written.

Tasks 1 and 2 land regardless of how the codegen-for-existing-components
question resolves. Task 3 is a "we'll know it when we see it" follow-up.

**Not filed:** any task that extends the codegen tool to emit
bindings for the 39 existing `*_lua.hpp` files. The recommendation is
that work doesn't happen.

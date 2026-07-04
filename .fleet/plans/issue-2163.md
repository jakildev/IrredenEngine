# Plan: lua-ecs — add VEC4/QUAT LuaFieldType so EVAL systems can write quaternion fields

- **Issue:** #2163
- **Model:** opus
- **Date:** 2026-07-01
- **Blocked by:** (none)

### Scope

Add a native `VEC4` column type to `LuaFieldType` so a **Lua-defined**
component can declare a `vec4`/quaternion field that EVAL systems read and
write with no C++ shim. Quaternions are stored as `IRMath::vec4(qx,qy,qz,qw)`
engine-wide (there is no distinct `IRMath::quat` type — `IRMath::vec4 =
glm::vec4`, and `C_LocalTransform.rotation_` is itself an `IRMath::vec4`), so
`quat`/`quaternion` become **tag aliases for the same VEC4 column** with an
identity default, not a second storage type.

### Premise correction (authoritative reading, per architect sign-off)

The issue's motivating example — writing `C_LocalTransform.rotation_` from a
Lua EVAL tick — is served by a **different mechanism** than this task and is
**not fixed by adding a LuaFieldType**:

- `C_LocalTransform` is a **C++-defined** component. EVAL systems reach C++
  components through `LuaCppColumnView` (`at`/`setAt`), built by `registerType<T>`
  (`recordComponentLuaName` in `lua_script.hpp`). The `LuaFieldType` machinery
  in `lua_component_data.hpp` only covers **Lua-defined** components
  (`IComponentDataLuaTyped`).

So this task delivers the literal title ask (a `vec4`/`quat` field type usable
by **Lua-defined** components) and is independently valuable. It does **not**,
by itself, make `C_LocalTransform.rotation_` writable from a Lua EVAL tick —
that C++-component field is reached through a different binding path, so any
downstream C++ facing→rotation shim is unaffected. Exposing
`C_LocalTransform.rotation_` on the C++-component Lua binding is a **separate
follow-up** (a `registerType`/`LuaCppColumnView` ergonomics question), filed
unlabeled per TASK-FILING.md as part of the plan triage.

### Approach (single, committed)

**One `LuaFieldType::VEC4` column** storing `IRMath::vec4`. Accept the explicit
tags `vec4`, `quat`, and `quaternion`, all resolving to `VEC4`. Use the
existing `quatFromLua()` (identity `(0,0,0,1)` default for nil/partial input)
as the uniform column converter — the primary consumer is rotation, so identity
is the correct default.

**Why not a distinct `QUAT` type/variant:** a quaternion and a `vec4` are the
*same* C++ storage (`IRMath::vec4`). A second `std::visit` variant of an
identical type can't be disambiguated by the `if constexpr
(std::is_same_v<Elem, IRMath::vec4>)` dispatch the column machinery uses; the
only behavioral difference (nil-default = identity vs zero) is resolvable at the
shared column by choosing the quat-identity default. The `quat`/`quaternion`
semantics live in the **tag** (register-time), not a redundant storage type.

Steps, in order:

1. `engine/script/include/irreden/script/lua_component_data.hpp` (7 sites)
   - Add `VEC4` to the `LuaFieldType` enum (after `IVEC3`).
   - `toString` case (`"vec4"`).
   - Append `std::vector<IRMath::vec4>` as a new `LuaFieldColumn` variant
     alternative (keep the 1:1 enum↔variant ordering).
   - `detail::makeEmptyColumn`: `VEC4 → std::vector<IRMath::vec4>{}`.
   - `detail::columnAppendDefault`: `if constexpr (std::is_same_v<Elem,
     IRMath::vec4>)` → `v.push_back(quatFromLua(defaultValue))`.
   - `writeFieldAt`: vec4 branch accepting an `{x,y,z,w}` table or
     `IRMath::vec4` userdata via `quatFromLua`.
   - `readFieldAt`: distinct vec4 branch surfacing `{ x, y, z, w }` (mirror the
     vec3 `{x,y,z}` shape, add `w`).
   - The generic move/copy/swap/size helpers are type-generic — no per-type case.
2. `engine/script/src/lua_script.cpp`
   - `parseExplicitTypeTag`: map `"vec4"`, `"quat"`, `"quaternion"` → `VEC4`.
   - Update the unknown-tag error string to list `vec4`.
   - `inferTypeFromDefault`: `if (value.is<IRMath::vec4>()) return VEC4;`
     (symmetric with the vec3/ivec3 userdata branches; a bare `{x,y,z,w}` table
     still requires the explicit tag).
   - **Do not** add `VEC4` to `isModifierTargetable` — it stays
     `INT32|FLOAT|BOOL`; `VEC4` falls through to `kInvalidFieldId` like vec3.
3. `test/script/lua_component_register_test.cpp`
   - Retarget the existing unknown-tag test: `type = 'quaternion'` is now valid,
     so change the tag to a still-unknown one (`'mat4'`) and its assertion.
   - Add a `Vec4AndQuatFieldsStoreAsNativeColumnAndRoundTrips` test mirroring
     `Vec3AndIvec3FieldsStoreAsNativeColumnsAndRoundTrip`: `quat`/`vec4`/omitted
     fields materialize as `std::vector<IRMath::vec4>`, round-trip through
     write/read as `{x,y,z,w}`, and an omitted default resolves to identity
     `(0,0,0,1)`. Add a not-modifier-targetable assertion for `quat`.
4. `engine/script/CLAUDE.md` — document the `vec4`/`quat`/`quaternion` EVAL
   field type; note it is EVAL-only (CODEGEN still errors on the tag, same
   status as `vec2`).

### Acceptance criteria

- A Lua-defined component with `type = 'vec4'` / `'quat'` / `'quaternion'`
  fields stores a native `std::vector<IRMath::vec4>` column (no `sol::table`
  fallback).
- Reads surface as `{x,y,z,w}` tables; writes accept an `{x,y,z,w}` /
  `{1,2,3,4}` table or an `IRMath::vec4` userdata.
- Omitted/partial default resolves to identity `(0,0,0,1)`.
- Archetype move/copy/remove preserve vec4 column values (generic helpers).
- The script test target builds; the new + retargeted tests pass. No change to
  `isModifierTargetable`.

### Gotchas

- The `'quaternion'` unknown-tag test WILL silently false-pass if not
  retargeted — it would assert an error that no longer occurs.
- `columnAppendDefault` and `writeFieldAt` have `if constexpr` chains with no
  final else: an unmatched `IRMath::vec4` element silently no-ops. The vec4
  branch is mandatory in both.
- Keep the enum↔variant ordering 1:1; append `VEC4` at the end of both.
- `readFieldAt` needs a *distinct* vec4 branch (not folded into the vec3/ivec3
  branch) — vec4 surfaces a 4-key `{x,y,z,w}` table.
- Engine-public artifact: keep terminology engine-only (no game feature names).

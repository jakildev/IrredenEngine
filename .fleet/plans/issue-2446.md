<!--
Plan file for #2446 (per #1932 — committed as the first commit of the
implementer's PR). This is the `## Plan` comment posted on issue #2446 on
2026-07-20; the architect granted approach sign-off on 2026-07-28 and cleared
`human:review-plan`. Reproduced verbatim below; the issue thread is the
canonical source.
-->

## Plan: Lua attach path for codegen'd components + Lua-typed accessor guards

- **Issue:** #2446
- **Model:** opus
- **Date:** 2026-07-20

### Scope

Make `IREntity.addLuaComponent` / `IREntity.deferredCreate` attach codegen'd (C++-typed) components with overrides honored; convert every blind `IComponentDataLuaTyped` cast in the `IREntity.*Lua*` accessors into a routed check with an actionable Lua error; correct the misleading `deferredCreate` comment; document the accessor asymmetry in `engine/script/CLAUDE.md`.

### Verified current state (source-verified 2026-07-20, worktree @ 5c53d2e5)

- `IComponentData::appendDefaultRow()` defaults to `false` (`engine/entity/include/irreden/entity/i_component_data.hpp:44`) and `IComponentDataImpl<T>` does **not** override it — the only override is `IComponentDataLuaTyped` (`lua_component_data.hpp:254`). So `EntityManager::addComponentByIdImpl` (`entity_manager.cpp:621`) asserts for every C++-typed impl, exactly as reported. This default is deliberate: some C++ components have deleted default ctors (the comment cites `C_CanvasAOTexture`), so a blanket default-row at the entity layer was ruled out by design.
- Both `addLuaComponent` (`lua_script.cpp:542`) and `deferredCreate` (`lua_script.cpp:589`; attach runs at flush inside `stageStructuralChange`) route through the shared `attachDynamicComponent` helper (`lua_script.cpp:175`), which then `static_cast`s to `IComponentDataLuaTyped` for the overrides write. **`deferredCreate`'s componentList hits the same wall** — confirmed by code reading, and worse: the assert fires at `flushStructuralChanges`, far from the Lua call site.
- Negative claim exhaustively verified: the complete `IREntity` Lua surface is `addLuaComponent` / `getLuaComponent` / `removeLuaComponent` / `hasLuaComponent` / `deferredCreate` / `deferredDestroy` / `bindPoint` / `singleton` / `getLuaField` / `setLuaField` (grep of `m_lua["IREntity"][` across `lua_script.cpp`). **No typed setComponent binding exists**; the two attach paths above are the only ones.
- **Additional latent UB, pulled into scope:** `getLuaComponent` (`:551`), `getLuaField` (`:674`), `setLuaField` (`:691`) blind-`static_cast` any `IComponentData*` to `IComponentDataLuaTyped*` with no assert at all. A codegen'd component attached from C++ (spawn factory, `setComponent<T>`) then read via `getLuaComponent` is undefined behavior **today**, before any fix. Fixing attach makes this path much easier to reach, so the guards land in the same PR.
- No routing discriminator exists yet: `LuaScript::m_componentByLuaName` records only C++-bound types (`recordComponentLuaName<T>` from `registerType`); the `IRComponent.register` path records the Lua-typed id nowhere.
- Infrastructure to reuse: the prefab declarative-components factory (`prefab_component_factory.hpp`) proves the "default-construct + setFields(table) + typed `setComponent`" shape; `IRScript::vec3FromLua` / `ivec3FromLua` already exist in `ir_script_utils.hpp`; the `registerCodegenComponents` emission (`cmake/lua_codegen/main.cpp:842`) already has both `em` and `luaScript` in scope per component.
- Sibling/in-flight reconciliation: no open PR touches `engine/script/`; the other needs-plan issues (#2360 Metal render, #2449 toolchain, #2462 fleet infra) do not overlap this surface.

### Approach (single approach, committed)

Route attach through a **per-`LuaScript`, ComponentId-keyed attach-factory registry populated by codegen emission** — NOT by generalizing `appendDefaultRow` to C++ types. Rationale: (a) preserves the entity core's "C++ types attach with an explicit value" invariant (deleted-default-ctor and side-effectful default ctors stay unrepresentable in the dynamic path); (b) overrides need per-field typed writes, which for a C++ struct only the codegen (which knows the fields) can supply; (c) mirrors the proven prefab-factory shape instead of inventing a second mechanism.

**Phase 0 (repro confirm, cheap).** On a current-master-buildable host, before building the fix: add one `IREntity.addLuaComponent(e, <codegen comp>, {})` call against the existing `lua_component_codegen_fixtures` setup and confirm the `appendDefaultRow` assertion fires as documented. Bail path: if it does NOT fire, stop — comment the observed behavior on this issue and flag for re-plan; do not build the dependent phases on a refuted premise.

1. **LuaScript state (`lua_script.hpp`).**
   - `std::unordered_set<IREntity::ComponentId> m_luaTypedComponentIds` — inserted in the `IRComponent.register` lambda immediately after `registerComponentDynamic` succeeds. NOT on the coexistence carve-out early-return (that path returns the existing C++ handle).
   - `using ComponentAttachFn = std::function<void(IREntity::EntityId, const sol::table &)>;` + `std::unordered_map<IREntity::ComponentId, ComponentAttachFn> m_componentAttachFactories` + public `registerComponentAttachFactory(ComponentId, ComponentAttachFn)`. Per-`LuaScript` members, NOT a process-singleton: ComponentIds are per-World runtime allocations, and a static registry would go stale across World re-creation (the prefab registry tolerates process lifetime only because it keys by stable *name*).
   - Error-path helper: reverse scan of `m_componentByLuaName` for id → Lua name (diagnostics only; falls back to the numeric id).
2. **Routing in `attachDynamicComponent` (`lua_script.cpp`).** Make the helper LuaScript-aware (member function or explicit map params). Order: (i) attach factory registered → invoke with the overrides table (or an empty table when nil); the factory body is `C c{}; setFields(c, t); IREntity::setComponent(entity, std::move(c));` (ii) id in `m_luaTypedComponentIds` → existing dynamic path, unchanged; (iii) neither → `throw sol::error` naming the component and the supported paths (Lua-registered components attach directly; codegen'd components require `registerCodegenComponents()` to have run; other C++-bound components attach from C++ via typed `setComponent`). The engine-side assertion becomes unreachable from Lua but stays as safety.
3. **`deferredCreate` call-time validation.** Validate each marshaled componentId's eligibility (factory present ∨ Lua-typed) in the marshal loop and raise at the call site with the entry index. An ineligible entry must never reach the flush-time drain — a throw there has no Lua context. Keep the flush lambda throw-free.
4. **Guard the read/write accessors.** `getLuaComponent` / `getLuaField` / `setLuaField`: check `m_luaTypedComponentIds` before the cast; a non-Lua-typed id raises a `sol::error` naming the supported reads for C++-typed components (archetype column views in ticks / typed C++ access). `removeLuaComponent` / `hasLuaComponent` are id-generic (no cast) — verify removal of a codegen'd component in the test rather than guarding.
5. **Codegen emission (`cmake/lua_codegen/main.cpp`).** In the emitted `registerCodegenComponents` body, per component: capture `const IREntity::ComponentId id = em.getComponentType<IRComponents::C_X>();`, keep the `registerTypeFromTraits` call, then emit `luaScript.registerComponentAttachFactory(id, [](IREntity::EntityId entity, const sol::table &fields) { ... });` with per-field guarded reads: `int32` / `float` / `bool` / `string` via `fields.get<sol::optional<T>>("name")`; `vec3` / `ivec3` via a non-nil check + `IRScript::vec3FromLua` / `ivec3FromLua` (matching the packed-field write convention). Unknown keys are silently ignored — the same contract `writeRowFromTable` documents. Add `irreden/script/ir_script_utils.hpp` to the emitted include block.
6. **Docs.** Correct the `deferredCreate` comment (`lua_script.cpp` ~585) to the routed semantics. In `engine/script/CLAUDE.md`: update the attach paragraphs (including the "a native C++-only component still uses the templated C++ createEntity" sentence) and add the acceptance's "which view do I get, and what can I call on it?" table — three rows (codegen'd C++ struct / `IRComponent.register` Lua-typed / hand-written `*_lua.hpp` C++ component) × (tick column view + methods, `IREntity.*Lua*` accessor support, attach path, modifier `bindingId` support). While in that file, fix the prefab-schema example that calls `IREntity.setComponent` inside `setup` — no such engine binding exists (verify intended spelling; likely doc drift or a per-creation helper).
7. **Tests** — see acceptance.

One task, one PR — the codegen emission and the runtime routing are one contract (the emitted header calls the new LuaScript API) and must land together. No stack split.

### Affected files
- `engine/script/include/irreden/script/lua_script.hpp` — attach registry, Lua-typed id set, `registerComponentAttachFactory`
- `engine/script/src/lua_script.cpp` — routing, accessor guards, call-time validation, comment fix
- `cmake/lua_codegen/main.cpp` — attach-factory emission + `ir_script_utils.hpp` include
- `test/script/lua_component_codegen_test.cpp`, `test/script/lua_component_codegen_fixtures.lua` — coverage
- `engine/script/CLAUDE.md` — accessor table + attach-path docs

### Acceptance criteria (positive-fire)
1. `addLuaComponent(e, <codegen comp>, { <field> = X })` attaches; C++-side `getComponent<C_X>` asserts the override landed AND untouched fields hold schema defaults. Include one `vec3`-field override case.
2. `deferredCreate({ { <codegen comp>, { <field> = X } } })` + `flushStructuralChanges` materializes the entity with X asserted.
3. Negative with positive control in the same test: the same attach call on a C++-bound component with **no** factory raises a Lua error whose message is asserted to name the supported path (not the raw `appendDefaultRow` assertion text), while the codegen-component control attach succeeds.
4. `getLuaComponent` / `getLuaField` / `setLuaField` on a codegen'd component raise the diagnostic (message asserted) — the UB cast is unreachable.
5. `removeLuaComponent` on an attached codegen'd component detaches it (`hasLuaComponent` flips true → false).
6. Existing EVAL-path suites (`lua_component_register_test`, coexistence tests) stay green — the Lua-typed path is behavior-identical.

### Gotchas
- The attach map MUST live on `LuaScript` (World lifetime) — never a static/process registry (per-World ComponentIds; multi-World test fixtures would key stale ids).
- Coexistence: `IRComponent.register`'s carve-out early-return must not insert into `m_luaTypedComponentIds`.
- sol2 config is `SOL_ALL_SAFETIES_ON` + `SOL_EXCEPTIONS_ALWAYS_UNSAFE` — raise via `throw sol::error{...}` like sibling bindings.
- Wiring order stays `bindLuaDrivenEcs()` → `registerCodegenComponents()` (already documented); the codegen handle at `IRComponent.<Name>` exists only after both.
- Modifier `bindingId` for codegen'd components remains unsupported (documented coexistence limitation) — carry that row in the new table; do not attempt to fix it here.
- Emitted struct field order is alphabetical (codegen sort); overrides are name-keyed so order is irrelevant — don't rely on positional table entries in tests.

### Out of scope (explicit)
- Unifying the column-view surfaces (`getField`/`setField` on `LuaCppColumnView`) — acceptance permits doc-only, and generic sol2-property dispatch has a silent-failure class for getter-lambda-style hand bindings. If EVAL-dispatch parity for codegen'd systems becomes load-bearing (the dual-mode tuning loop), file it as its own designed follow-up.
- Registering codegen components into the prefab declarative-components (string-keyed) registry — a natural extension of the same emission, not required by acceptance.
- Hand-written `*_lua.hpp` components opting into Lua attach via `registerComponentAttachFactory` — the mechanism supports it; per-component adoption is separate work.


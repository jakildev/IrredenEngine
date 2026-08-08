## Plan: cmake/lua_codegen: registry symbols collide across codegen runs (ODR; correctness rests on inlining)

- **Issue:** #2609
- **Model:** opus
- **Date:** 2026-08-07

### Scope

Give every `irreden_lua_codegen()` run its own linkage identity so N runs
linked into one binary can never merge, keep every existing call site
compiling unchanged, and make the one collision class namespacing cannot fix
(same component *name* declared by two runs) a loud link error instead of a
silent attach-factory swap. One PR.

### Verified current state (re-measured 2026-08-07, this session)

- **4 in-tree runs:** 3 into `IrredenEngineTest` (`test/CMakeLists.txt:155,181,194`)
  + 1 into `IRLuaPerfGrid` (`creations/demos/lua_perf_grid/CMakeLists.txt:14`).
- `nm build/IrredenEngineTest | grep -c registerCodegenComponents` → **0** on
  the current tree — every copy still inlines; the issue's correctness-by-
  inlining condition is live, unchanged.
- **The colliding surface** (all in `cmake/lua_codegen/main.cpp:887-977`):
  `registerCodegenComponents` (:911), `struct CodegenSystemIds` (:940, members
  differ per run → type ODR), `registerCodegenSystems` (:946),
  `kDefaultEcsMode` (:960), `kEvalSystemNames` (:969) — **plus one the issue
  body doesn't list:** the per-system `createSystem_<NAME>()` free functions
  (flushed at :887-889). Those collide whenever two runs declare the same
  *system* name — the same conditional-uniqueness class as `bindLuaType<C_X>`
  (the 2026-07-29 comment's precondition). The two `inline constexpr`
  variables are themselves ODR violations when runs differ in mode/content,
  not just the functions.
- **No consumer passes `CodegenSystemIds` across a TU boundary** — all 5 call
  sites bind `auto ids` and consume locally
  (`test/script/lua_system_codegen_test.cpp:193`,
  `lua_system_coexistence_test.cpp:77,109,136`,
  `creations/demos/lua_perf_grid/main_lua.cpp:356`). A per-run distinct type
  is therefore safe.
- **Nothing in engine code declares `namespace IRScript::CodegenRegistry`** —
  the only non-generated mentions are comments
  (`engine/script/include/irreden/script/lua_script.hpp:73,317`) and docs. The
  namespace exists solely in generated headers, so the re-export mechanism
  below cannot collide with a real engine declaration.
- **Each generated header is included by exactly one TU today** (the 3 test
  .cpps + `main_lua.cpp`) — the include contract Phase 1 step 4 formalizes is
  already the in-tree reality.
- **Fixture component names are distinct across the 3 test runs** (prefixed
  `Codegen*` / `Sys*` / `Coexist*`), re-confirming the thread comment's
  precondition check.
- **Sibling/in-flight:** no open PR touches `cmake/lua_codegen/` or the
  registry surface (checked open-PR list 2026-08-07). The #2608 workaround
  (attach factory in `bindLuaType<C_X>`) is merged and stays where it is —
  this plan retires the *constraint*, not that placement.

### Approach

**Per-run namespace + using-directive re-export + link-time claim symbols.**

**Phase 0 — probe the compat mechanism's language premise.** The zero-call-
site-churn claim rests on qualified lookup following using-directives. Compile
a 6-line standalone probe on the host toolchain before touching the emitter:

```cpp
namespace A { namespace R {
    namespace run1 { inline void f() {} }
    using namespace run1;
} }
int main() { A::R::f(); }
```

Expected: compiles and links ([namespace.qual] — qualified lookup unions
using-directed namespaces when the namespace has no direct declaration).
Bail path if refuted on either fleet toolchain (gcc-13 / AppleClang): stop,
comment the measurement on this issue, swap back to `fleet:needs-plan` — the
fallback shape (TU-local `namespace CodegenRegistry = ...` alias) changes the
emitted surface enough that the plan should be re-cut, not improvised.

**Phase 1 — emitter + CMake.**

1. `cmake/lua_codegen/main.cpp`: derive a run identifier from the `--out`
   stem (basename minus extension, sanitized to `[A-Za-z0-9_]`, `run_`
   prefixed if it starts with a digit — never the full path, which is
   host-specific). Add an optional `--registry-namespace=<id>` CLI override.
2. Emit **both** `IRScript::CodegenRegistry` blocks (the `systemsBuf` flush
   at :887-889 and the registry block at :910-977) as:

   ```cpp
   namespace IRScript::CodegenRegistry {
   namespace <id> {
       ... createSystem_<NAME>, registerCodegenComponents,
           CodegenSystemIds, registerCodegenSystems,
           kDefaultEcsMode, kEvalSystemNames ...
   } // namespace <id>
   using namespace <id>;
   } // namespace IRScript::CodegenRegistry
   ```

   Every symbol gets per-run linkage (distinct mangled names — the linker
   keeps all runs), while the using-directive keeps every existing
   `IRScript::CodegenRegistry::X` spelling resolving unchanged in any TU that
   includes one run's header. A TU that includes **two** runs' headers turns
   the old spelling into a compile-time ambiguity naming both candidates —
   the caller disambiguates with the per-run qualified name. That is the
   correct degradation: loud, at compile time, with the fix in the error.
   Both blocks must share one `<id>` — `registerCodegenSystems()` calls
   `createSystem_<NAME>()` unqualified.
3. `cmake/ir_functions.cmake` (`irreden_lua_codegen`): optional
   `REGISTRY_NAMESPACE` one-value param threaded to `--registry-namespace`;
   omitted → the tool's stem derivation (all 4 in-tree runs stay zero-config).
   Track used ids per target in a target property and `FATAL_ERROR` at
   configure when two runs on one target resolve to the same id (two
   same-stem `codegen.hpp` in different dirs would otherwise silently
   re-create the collision).
4. **Claim symbols for the residue namespacing cannot fix** (component
   structs, `bindLuaType<C_X>` specializations, attach factories — all keyed
   on the user-authored component *name*): per component, emit at namespace
   scope a **non-inline** external definition whose name is the diagnosis,
   e.g.

   ```cpp
   namespace IRScript::CodegenClaims {
   char C_<Name>_declared_by_more_than_one_codegen_run_in_this_binary;
   }
   ```

   Two runs declaring the same component name now fail the **link** with a
   duplicate-symbol error that names the component, instead of silently
   swapping attach factories (the post-#2446 worst case from the thread).
   Consequence to document: a generated header becomes single-TU-per-target
   by contract (multi-TU inclusion was legal-but-unused; the claim chars make
   it a duplicate-symbol error too, which enforces the same contract). System
   names need no claim — `createSystem_<NAME>` is inside the per-run
   namespace, so cross-run system-name reuse becomes benign.
5. Rewrite the now-obsolete emitter comments: the "Keep this body minimal /
   correctness rests on inlining / #2609 owns the fix" block (:898-909) and
   the `bindLuaType` placement note (:846) — the inline-size constraint is
   retired; `bindLuaType<C_X>` remains the home for per-component work as an
   organizational choice, not an ODR survival strategy. Do **not** re-inline
   the #2608 attach factory (no churn on a merged workaround).

**Phase 2 — verification (see Acceptance criteria for the gates).**

**Phase 3 — docs.** `engine/script/CLAUDE.md`: the "Coexistence wiring" call
sequence stays valid (compat spelling) — add the per-run namespace scheme,
the stem/`REGISTRY_NAMESPACE` derivation, the single-TU-per-target include
contract, and what a `CodegenClaims` duplicate-symbol link error means. The
`kEvalSystemNames` future-hook note gains "per-run" wording.

### Affected files

- `cmake/lua_codegen/main.cpp` — per-run namespace emission (both blocks),
  claim symbols, `--registry-namespace` flag, comment rewrite
- `cmake/ir_functions.cmake` — `REGISTRY_NAMESPACE` param + per-target
  duplicate-id `FATAL_ERROR`
- `engine/script/CLAUDE.md` — scheme, include contract, claim-symbol
  diagnostic
- **Unchanged by design:** `test/script/lua_*_test.cpp`,
  `creations/demos/lua_perf_grid/main_lua.cpp` — zero call-site churn is the
  acceptance-criterion-3 "compatibility spelling"

### Acceptance criteria

1. **Criterion-2 regression probe (mutate-run-revert, uncommitted):** patch
   `main.cpp` to re-inline the attach-factory registration loop into
   `registerCodegenComponents` (the exact #2608 trigger), rebuild, run
   `fleet-run --timeout 0 IrredenEngineTest`: the pre-fix failure trio
   (`LuaSystemCoexistenceTest.BothModesRegisterAndTickInOnePipeline`,
   `.EvalSystemBodyIsHotReloadable`,
   `.ComponentRegisterIsIdempotentInCoexistenceMode`) stays **green**, and
   `nm build/IrredenEngineTest | grep registerCodegenComponents` shows ≥ 2
   distinct out-of-line symbols (one per run namespace) — the positive-fire
   observation that per-run linkage, not inlining luck, now carries
   correctness. Revert the probe before commit; paste both readings into the
   PR body.
2. **Claim-symbol positive control (also mutate-run-revert):** add a
   same-named component to two test fixtures → the link fails with a
   duplicate `IRScript::CodegenClaims::C_<Name>_…` symbol. Revert. (A guard
   that has never fired is a test-coverage illusion.)
3. `fleet-build --target IrredenEngineTest` and `fleet-build --target
   IRLuaPerfGrid` green **with zero call-site edits** anywhere.
4. `fleet-run --timeout 0 IrredenEngineTest` green — 1510/1511 with the sole
   failure `SaveTrait.InventoryIsComplete` (#2834's known master-red, matched
   by name).

### Gotchas

- The CMake 3.31 `CODEGEN`-token hazard (:184-186 of `ir_functions.cmake`)
  applies to any new `add_custom_command` argument — keep the new flag's
  value lowercase-safe the same way.
- `kDefaultEcsMode` is consumed in `if constexpr`
  (`main_lua.cpp:355`) — a per-run `inline constexpr` in a nested namespace
  is still a constant expression; no consumer change.
- Derive the id from the **stem only**, never the resolved absolute
  `OUTPUT_HPP` path (host-specific, and `IRLC_OUTPUT_HPP` is absolutized
  before the tool sees it).
- The generated headers live under `build/` — the header-checks executor
  excludes them, so the new namespace shape needs no baseline entry; don't
  add one.
- Plans are engine-public: nothing game-side touches this surface.

### Plan review — cleared 2026-08-07

`fleet-plan-lint 2609`: PASS, 0 warnings. Two binding constraints for the
implementer, neither blocking:

1. **Acceptance criterion 4's `1510/1511` is #2898's branch total, not
   master's.** Master today is **1502/1503**, and the absolute count drifts
   with every merged PR that adds tests — **gate on the name, not the
   number**: green except `SaveTrait.InventoryIsComplete` (#2834's known
   master-red).
2. **`engine/script/CLAUDE.md` (Phase 3) is also edited by open PR #2902**
   (`fleet:approved`, likely lands first). Textual conflict risk only —
   #2902 touches the command-bindings section, not the coexistence wiring
   this plan amends. Rebase onto master immediately before the Phase 3 edit.

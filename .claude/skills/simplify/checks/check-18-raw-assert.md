# Check 18 — raw `assert()` instead of the engine convention

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** the diff adds a raw `assert(` call.

Runtime invariants go through `IR_ASSERT` (routes to the engine log sink,
stripped under `IR_RELEASE`); compile-time invariants use raw
`static_assert` with a message (no `IR_` wrapper exists or is needed);
raw `<cassert>` `assert()` never — silently absent in release and
invisible to the log sink. Full convention:
[`docs/agents/CLAUDE-BASELINE.md`](../../../../docs/agents/CLAUDE-BASELINE.md).

```
Grep tool with:
  pattern: '\bassert\s*\('
  glob:    '{engine,creations,test}/**/*.{hpp,cpp,h,cc,tpp}'
  output_mode: 'content'
  -n: true
```

(`\b` after `_` does not fire, so `static_assert(` never matches.)
Cross-reference against added (`+`) lines. Skip Lua `assert(...)` inside
string literals handed to the script engine (`runOk("assert(...)")` in
script tests) — that is Lua's own assert, not `<cassert>`. Allowlist:
standalone `tools/**` binaries that don't link the engine. Live deviations
(don't re-flag):

- `engine/asset/include/irreden/asset/chunk_header.hpp:69` — **migrating**
  via #2674; the site disappears when that lands.
- `engine/ir_args.cpp:16`
  (`#define IR_ASSERT(cond, msg) assert((cond) && (msg))`) — **permanent**,
  not migrating. It is a dependency-free macro so the standalone tools
  (`img_diff`, `jitter_probe`, `lua_codegen`) can compile that translation
  unit without linking the engine profiler, and it lives outside `tools/**`
  so the allowlist above does not reach it. Don't try to "finish" this one —
  there is nothing to migrate.

Auto-fix: `IR_ASSERT` for runtime conditions. (#2440)

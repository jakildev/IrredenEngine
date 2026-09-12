# Check 18 — raw `assert()` instead of the engine convention

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** the diff adds a raw
`assert(` call.

Runtime invariants go through `IR_ASSERT` (engine log sink, stripped under
`IR_RELEASE`); compile-time invariants use `static_assert` with a message;
raw `<cassert>` `assert()` never — silently absent in release and
invisible to the log sink
([`docs/agents/CLAUDE-BASELINE.md`](../../../../docs/agents/CLAUDE-BASELINE.md)).

```
Grep tool with:
  pattern: '\bassert\s*\('
  glob:    '{engine,creations,test}/**/*.{hpp,cpp,h,cc,tpp}'
  output_mode: 'content'
  -n: true
```

(`\b` after `_` does not fire, so `static_assert(` never matches.) Keep
only `+` lines. Skip Lua `assert(...)` inside string literals handed to
the script engine (`runOk("assert(...)")` in script tests). Allowlist:
standalone `tools/**` binaries that don't link the engine. Live
deviations (don't re-flag):

- `engine/ir_args.cpp:16` (`#define IR_ASSERT(cond, msg) assert((cond) &&
  (msg))`) — permanent: a dependency-free macro so the standalone tools
  (`img_diff`, `jitter_probe`, `lua_codegen`) compile that translation
  unit without the engine profiler; it lives outside `tools/**`, so the
  allowlist does not reach it.

Auto-fix: `IR_ASSERT` for runtime conditions.

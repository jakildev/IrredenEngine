# Check 11 — mutable namespace-scope variables in headers

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** the diff touches `.hpp`/`.h` files.

A new `inline` / `extern` variable at namespace scope in a header is state
with no owner — never cleared at World teardown, invisible to scene reset
and save/load. Full rule, sanctioned-pattern table, and rationale:
[`.claude/rules/cpp-globals.md`](../../rules/cpp-globals.md).

```
Grep tool with:
  pattern: '^\s*(inline|extern)\s+(?!(constexpr|const|void)\b)[^(]*[;={]'
  glob:    '**/*.{hpp,h}'
  output_mode: 'content'
  -n: true
```

The `[^(]*` guard drops function declarations; classify surviving hits by
hand and cross-reference against added (`+`) diff lines — only flag newly
introduced variables. Allowlisted paths (the sanctioned manager-global and
engine-context patterns): `engine/*/include/irreden/ir_*.hpp` and
`engine/include/irreden/ir_engine.hpp`. For everything else, flag with the
migration target from the cpp-globals.md table: world-scoped mutate-once
state → singleton component; system wiring → the `SystemManager` registry
(`IRSystem::findSystem`); module-internal state → anonymous namespace in
the `.cpp`. Live deviations (don't re-flag): `g_jointMatrixSystem` /
`g_allocatorSystem` (#2526), `g_defaultTheme` (#2527). Report, don't
auto-fix — the right owner is a design call.

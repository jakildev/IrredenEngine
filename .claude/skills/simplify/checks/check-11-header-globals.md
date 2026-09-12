# Check 11 — mutable namespace-scope variables in headers

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** the diff touches
`.hpp`/`.h` files.

A new `inline` / `extern` variable at namespace scope in a header is state
with no owner. Rule, sanctioned patterns, and rationale:
[`.claude/rules/cpp-globals.md`](../../../rules/cpp-globals.md).

The check is executed, never hand-grepped:

```bash
cmake --build <build-dir> --target header-checks
```

Without a configured build dir: `cmake -DPROJECT_ROOT=<repo-root> -P
cmake/run_header_checks_standalone.cmake`. The executor owns the
declaration-head scoping of the `constexpr` / `const` exemption, the
both-ends rule for pointers, continuation-line joining, the allowlist, and
the `header_global_baseline` ratchet.

The scan is tree-wide, so keep only failures on `+` diff lines;
pre-existing violations belong to their own issue. Flag with the migration
target from the cpp-globals.md table — world-scoped mutate-once state →
singleton component; system wiring → the `SystemManager` registry
(`IRSystem::findSystem`); module-internal state → anonymous namespace in
the `.cpp`. Report, don't auto-fix — the right owner is a design call.

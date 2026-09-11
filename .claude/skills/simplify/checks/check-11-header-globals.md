# Check 11 — mutable namespace-scope variables in headers

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** the diff touches `.hpp`/`.h` files.

A new `inline` / `extern` variable at namespace scope in a header is state
with no owner — never cleared at World teardown, invisible to scene reset
and save/load. Full rule, sanctioned-pattern table, and rationale:
[`.claude/rules/cpp-globals.md`](../../../rules/cpp-globals.md).

This check is **executed** — don't hand-grep it (#2727):

```bash
cmake --build <build-dir> --target header-checks
```

Without a configured build dir, the entry point the `header-checks` CI
workflow drives runs the same scan: `cmake -DPROJECT_ROOT=<repo-root> -P
cmake/run_header_checks_standalone.cmake`. It scans every header tree-wide
and owns the parts a restatement here kept getting wrong — declaration-head
scoping of the `constexpr` / `const` exemption, the both-ends rule for
pointers, continuation-line joining, the allowlist, the
`header_global_baseline` ratchet, and the live-deviation register.

Cross-reference failures against added (`+`) diff lines: a tree-wide scan
also reports pre-existing violations, which belong to their own issue. Flag
a hit with the migration target from the cpp-globals.md table —
world-scoped mutate-once state → singleton component; system wiring → the
`SystemManager` registry (`IRSystem::findSystem`); module-internal state →
anonymous namespace in the `.cpp`. Report, don't auto-fix — the right owner
is a design call. (#2738 replaced a hand-grep here that still passed `inline
const T *p` and still named #2526 / #2527 symbols long gone from the tree.)

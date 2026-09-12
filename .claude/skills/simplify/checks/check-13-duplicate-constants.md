# Check 13 — a named constant added in the diff that already exists elsewhere

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** the diff adds a
`constexpr` / `const` named constant.

The §1b subagents target callables and control flow, so a duplicated
named constant reaches review unseen. For each `k<Name>` in a `constexpr`
/ `const` definition on a `+` line, grep the tree for the identifier; two
or more definition sites → flag with the hoist target: one shared constant
beside the type both consumers share. Report, don't auto-fix — the home is
a design call.

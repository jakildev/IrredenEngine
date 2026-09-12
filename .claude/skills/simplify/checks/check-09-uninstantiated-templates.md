# Check 9 — template functions added with no instantiation

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** the diff adds a
`template <...>` function or member.

An uninstantiated template body is parsed, never semantically checked, so
a wrong member access or stale API call ships on a green build. For each
added `template <...>` function or member, grep `engine/`, `creations/`,
and `test/` for a call site (`<name><`, `<name>(`) outside the definition.
No hit → flag "uninstantiated template body — not type-checked; add a
call site or headless test in this PR." Report, don't auto-fix.

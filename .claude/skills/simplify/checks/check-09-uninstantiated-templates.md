# Check 9 — template functions added with no instantiation

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** the diff adds a `template <...>` function or member.

C++ only type-checks a template body at instantiation — an uninstantiated
template member/free function is *parsed*, never semantically checked, so a
wrong member access or stale API call in its body ships on a green build
(PR #2170's `carve()`). For each `template <...>` function or member
**added** in the diff, grep `engine/`, `creations/`, and `test/` for a call
site (`<name><`, `<name>(`) outside the definition itself. No hit → flag:
"uninstantiated template body — not type-checked; add a call site or
headless test in this PR." Report, don't auto-fix — where the instantiation
belongs is a design call.

# Check 13 — a named constant added in the diff that already exists elsewhere

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** the diff adds a `constexpr` / `const` named constant.

The §1b reuse fan-out's five subagents are all aimed at callables and
control flow, so a duplicated *named constant* reaches review by
construction (PR #2585 defined `kBackgroundNormDepthThreshold = 0.99f`
twice — same name, same literal, same semantic role — in the same PR that
made both files consumers of one shared type). For each `k<Name>` in a
`constexpr` / `const` definition on an added (`+`) line, grep the tree for
the same identifier; two-plus definition sites → flag with the hoist
target: one shared constant beside the type both consumers share (there,
`ir_render_types.hpp` next to `CompositeDepthSample`). Report, don't
auto-fix — which header is the right home is a design call. (#2651)

# Check 15 — retirement sweep — the old value, not just the old symbol

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** the diff introduces a named sentinel/constant that replaces a prior value, or migrates sites onto one.

When the diff introduces or migrates sites onto a named sentinel/constant
that replaces a prior value (`kNullSystemId` replacing the
`kNullEntity`/`0` collision), a completeness grep keyed on the retired
*symbol* is structurally blind to sites spelled as the bare *literal*: a
fabricated `SystemId{0}` on an error path survived three independent
`kNullEntity`-keyed sweeps and silently ticked the wrong system
(#2596 → #2599). Grep the relevant type context for value-equivalent
bare-literal constructions (`T{<old>}`, `T x = <old>`, `return <old>;`
from a `T`-returning function) and report survivors. The doc-prose mirror
of the same blind spot — retired entities surviving as paraphrases — is
§9a's retired-entity sweep. (#2600)

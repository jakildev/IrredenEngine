# Check 15 — retirement sweep — the old value, not just the old symbol

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** the diff introduces a
named sentinel/constant that replaces a prior value, or migrates sites
onto one.

A completeness grep keyed on the retired *symbol* is blind to sites
spelled as the bare *literal* (a `SystemId{0}` on an error path survives
every `kNullEntity`-keyed sweep). Grep the relevant type context for
value-equivalent bare-literal constructions — `T{<old>}`, `T x = <old>`,
`return <old>;` from a `T`-returning function — and report survivors. The
doc-prose mirror (retired entities surviving as paraphrases) is §9a's
retired-entity sweep.

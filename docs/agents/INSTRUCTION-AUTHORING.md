# INSTRUCTION-AUTHORING — rules for agent-facing instruction files

Part of the agent baseline ([`CLAUDE-BASELINE.md`](CLAUDE-BASELINE.md)), read
on demand: read this file before editing any instruction file — a `CLAUDE.md`,
`AGENTS.md`, `SKILL.md`, role or subagent file, `.claude/rules/` file, or a
`docs/agents/` protocol. The section keeps the heading of its stub in the
baseline; a creation that opts out of that heading opts out of it here.

---

## What belongs in agent-facing docs

Applies to every doc loaded into an agent's context: `CLAUDE.md`,
`SKILL.md`, role files, the `docs/agents/` protocols.

**Point, don't dump.** State each rule, table, procedure, or template once
in its canonical home (the baseline's `## Canonical-home map`) and link to it
everywhere else. If nothing owns the topic, file an issue for a home rather
than starting a third copy.

Do **not** include: file trees, layout blocks, "Key components / systems"
sections (agents `Glob`); catalogs of type, component, system, or function
names or signatures (agents `Grep`; the signature lives in the header);
rules owned by another doc (ECS footgun, naming, IRMath, Bash rules,
cross-repo isolation, build commands, fleet workflow, the `## Hard rules`
list — role files keep only role-specific exceptions); incident narration
(state the rule; an issue number survives only as the tracking pointer of a
live deviation the reader must not re-flag); verification or "double-check"
instructions aimed at the model (name the validator in
[`VALIDATION.md`](VALIDATION.md) instead). In a `SKILL.md`, also not:
sections paraphrasing the front-matter `description:`, anti-pattern lists
restating flow steps, decorative emoji bullets, procedures a sibling skill
owns (compose by reference), heavy worked examples (move to
`<skill>/procedures/<topic>.md`). In a role file, also not: protocols a
fleet doc owns — reference by anchor.

**Do** include: design decisions and their rationale; constraints and
invariants not obvious from the code; gotchas that have bitten; conceptual
relationships spanning directories; ordering constraints that affect
correctness; a code example that demonstrates a *pattern*; trigger
conditions, owner-role boundaries, and behavioral contracts no other doc
owns.

Every `[text](path#anchor)` and `§Heading` citation resolves (anchors are
lowercase-kebab of the heading) and every cited name exists; a section ID
implying a series (`M-2`) has its predecessors in the doc or explains the
numbering at the heading. Before stating a rule absolutely ("never X"),
grep the tree for deliberate counter-examples: narrow the rule, or keep it
absolute and record the exceptions in a `## Live deviations` register with
tracking issues. Cite the **shape** of what you measured, not a raw tally
("no `_HPP` guard exists; the only non-`_H` tokens are three vendored
headers"), or name the scope the number was measured over. Inclusion test:
would this section survive a rename refactor? Names are fine inside a
pattern example or an actionable gotcha, clutter when they list what exists.

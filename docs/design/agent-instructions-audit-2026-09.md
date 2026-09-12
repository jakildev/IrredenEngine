# Agent instructions audit — September 2026

Status: in progress. This document is the audit's source of truth: the
principles it measures against, the measured baseline, the per-surface
rubric, and the execution plan. Each executing PR or epic child cites it
rather than restating it.

## Why

The instruction corpus grew by accretion: every incident added a paragraph,
every paragraph cited its incident, and nothing ever came out. The model
generation the fleet now runs on (Claude Opus 5 / Fable 5.1, GPT-5.x under
Codex) follows instructions more literally, verifies its own work, and reads
long files worse than short ones, so the accretion is now a cost with no
offsetting benefit. Three vendor guides frame the target:

- Anthropic, Claude Code memory: a `CLAUDE.md` should target under 200 lines;
  keep build commands, conventions, pitfalls, and rules that differ from
  defaults; cut what the model can derive from the codebase (layouts,
  dependency lists, architecture overviews); move multi-step procedures to
  skills and path-specific content to `.claude/rules/`; remove contradictions
  because the model picks one arbitrarily.
- Anthropic, skill authoring: "the context window is a public good"; assume
  Claude is already smart and cut every explanation it does not need;
  `SKILL.md` body under 500 lines with one-level-deep references; a
  description that says what the skill does and when to use it; one default
  instead of a menu of options; no time-sensitive information.
- Anthropic, prompting Claude Opus 5: remove explicit verification and
  double-check instructions (the model already does both, and the
  instructions compound into waste); constrain scope in one sentence; a
  short conciseness instruction beats emphasis.
- OpenAI, GPT-5 prompting: instructions are followed with surgical precision,
  so a contradictory pair costs reasoning on every task; drop maximize-style
  language; keep tool instructions explicit and the rest short.
- OpenAI, `AGENTS.md`: lead with commands and durable working agreements;
  exclude what the code already shows; the instruction chain is truncated at
  32 KiB, so a long root file loses its tail.
- Comment practice (Ousterhout; the "no ticket numbers in comments" argument):
  comments explain what code cannot; a ticket id in a comment is a pointer
  into memory the source does not own.

## Measured baseline (2026-09-11, master at c44361144)

| Surface | Files | Size | Lines | Note |
|---|---|---|---|---|
| `docs/agents/*.md` | 29 | 690 KB | 14,342 | `FLEET.md` 68 KB, `fleet-labels-reference.md` 48 KB, `CLAUDE-BASELINE.md` 42 KB |
| `docs/agents/skills/*.md` | 8 | 125 KB | 2,832 | shared skill flows |
| `.claude/commands/*.md` (roles) | 8 | 146 KB | 2,703 | `role-worker.md` 61 KB, `role-merger.md` 34 KB |
| `.claude/agents/*.md` (subagents) | 18 | 130 KB | 2,195 | |
| `.claude/rules/*.md` | 7 | 70 KB | 1,193 | |
| `.claude/skills/*/SKILL.md` | 23 | 231 KB | 5,201 | `simplify` 40 KB (over the 500-line cap), `platform-catchup` 28 KB |
| skill support files | 40 | 117 KB | 2,296 | |
| `CLAUDE.md` (all) | 36 | 586 KB | 10,834 | `engine/render` 100 KB, `engine/script` 99 KB, `engine/prefabs/irreden/render` 59 KB |
| `AGENTS.md` | 1 | 1 KB | 22 | pointer file, fine |
| `.fleet/plans/*.md` | 189 | 2.0 MB | | committed task plans |
| Issue/PR numbers inside code comments | 559 | | 4,000 lines | `scripts/` 1,306, `engine/render` 1,169, `engine/prefabs` 700, `creations/` 411 |

The dominant pattern across every surface is the same: the rule is stated,
then the incident that produced it is narrated with its issue number, then a
second incident is appended when the rule failed to fire. The narration is
what the vendor guidance says to cut and what the model does not need.

## Rubric

Applies to every surface unless a row below narrows it.

1. State the rule; do not narrate the incident. An issue number may appear
   only as the tracking pointer for a live deviation the reader must not
   re-flag. History moves to `docs/design/` or is dropped.
2. Cut what the model derives: file trees, symbol catalogs, restated
   baseline rules, explanations of well-known concepts.
3. One canonical home per rule; everywhere else a link. Two copies drift.
4. No verification or double-check instructions aimed at the model; the
   current generation performs both unprompted. Keep validators, drop the
   nagging.
5. No contradictions, including across files. When two docs disagree, the
   audit picks one and deletes the other statement.
6. Describe outcomes and constraints, not choreography. A definition of done
   plus named validators beats a step list.
7. Length caps: `CLAUDE.md` 200 lines; `SKILL.md` body 500 lines with
   references one level deep; a subagent description one or two sentences;
   a role file the assignment contract plus pointers.
8. Code comments: `.claude/rules/comments.md`. No issue numbers, no history,
   no narration; the ratchet `scripts/lint_comment_refs.py` gates CI.

## Target state by surface

- **Code comments.** The comment rule lands with its ratchet (this audit's
  first PR). The 4,000 recorded references are swept to zero by
  module-scoped tasks; each task lowers its module's baseline entries to
  zero and the ratchet keeps them there.
- **Task plans.** The `## Plan` issue comment is the only plan artifact.
  No plan file is committed or staged; departures from an approach sketch
  go in the PR body. The epic steward keeps its ledger as a comment on the
  umbrella issue that it edits in place, and opens no PRs. `.fleet/plans/`
  is deleted; the tooling that read it (ingest, scout, epic-status,
  rebase's plan-only merge lane, the docs-only classifiers, the acceptance
  grader) reads the comment or is retired.
- **Definition of done.** An issue's `**Acceptance criteria**` are its
  definition of done; a plan may tighten them; the PR's `## Acceptance
  evidence` proves each one; the acceptance grader compares the two.
  `docs/agents/VALIDATION.md` indexes every validator (build, ctest, fleet
  tests, header checks, ruff, comment refs, render-verify, gui-verify,
  cull-verify, perf gate, positive control, plan lint) with what each
  proves and how it runs, so criteria name validators instead of describing
  them.
- **`CLAUDE.md` files.** Each under 200 lines: commands, conventions,
  pitfalls, and the module's contracts. Design rationale moves to
  `docs/design/<topic>.md` when it is still true and is dropped when it is
  history. Executed checks are named, not re-explained.
- **`docs/agents/` protocols.** Single-sourced: the label catalog owns
  label semantics, the flow docs own steps, the baseline owns cross-cutting
  rules. Incident narration is cut to the rule. Target is roughly a third
  of today's size.
- **Roles and skills.** Role files carry the assignment contract and
  pointers. `SKILL.md` bodies under 500 lines; descriptions rewritten as
  what-plus-when in the third person; procedures one level deep; explicit
  verification instructions removed.
- **Subagents.** One-sentence descriptions; system prompts state role,
  constraints, output shape, and decision rules only.
- **`AGENTS.md`.** Stays a pointer; gains the validation index link.

## Execution

1. PR: comment rule, ratchet, baseline, CI workflow, this document.
2. PR: plan-file retirement, steward ledger on the umbrella issue,
   `VALIDATION.md`, definition-of-done wording, deletion of `.fleet/plans/`.
3. Epic: comment sweep, one child per module group, each lowering its
   baseline entries to zero.
4. Epic: `CLAUDE.md` trims, one child per file over the cap, each graded by
   line count, the rubric, and the comment-refs ratchet on the file's docs.
5. Architect-owned: `docs/agents/` consolidation and the gated `.claude/`
   surfaces (roles, skills, subagents), in batches per surface.

Each executing change cites this document as its rubric and reports the
before/after size of the surface it touched.

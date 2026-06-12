# script-first for repeatable perf work

The `optimize` skill body is short by design. Every step it instructs
the agent to run is either:

1. A **script** the agent invokes verbatim (`scripts/perf/perf_grid_matrix.sh`,
   `compare_perf_runs.py`, `perf_summary.py`, future microbench scripts).
2. A **decision** the agent has to make from data (which fix to try
   next, whether the matrix delta justifies the change).

Anything that's neither — multi-step shell sequences in prose, copy-
pasted command snippets, "do these N things in order" recipes — is a
bug in the skill. Extract it to a script.

## Why

- **Scripts are testable.** A shell or python script can be smoke-tested
  on master and on a worktree. A prose recipe can't.
- **Scripts are diff-able.** When the recipe changes, the diff shows
  exactly what changed. Prose drifts silently as code evolves around it.
- **Scripts compound.** Every PR that wants a perf comparison gets the
  same matrix output without re-deriving the flags. The next session
  inherits the work.
- **Scripts don't lie.** A prose paragraph that says "this takes about 5
  minutes" stays true forever, even after a 10× regression slows it down.
  A script run produces the actual number.
- **Cache hit rate.** Skill bodies are loaded into context on every
  invocation. A 150-line skill + 800 lines of reference (only loaded
  when relevant) is cheaper than a 1000-line skill body.

## What lives where

| Lives in… | Examples |
|-----------|----------|
| `scripts/perf/` | Matrix run, before/after diff, summary, microbench harness, regression gate, baseline rotator |
| `.claude/skills/optimize/reference/` | The bottleneck catalog, big-win case studies, profiling-macro reference, partner-skill cross-refs, this principle |
| `.claude/skills/optimize/SKILL.md` | Triage → baseline → scan → profile → fix → measure → report → self-improve. ~150 lines. |

## When to make a new script

The first time you write a multi-step shell sequence in a PR body or
in `SKILL.md`, it's prose. The second time you'd write something
similar, **stop and extract a script**. The two-strike rule is the
trigger.

When in doubt: would two agents independently invoking this skill
benefit from the same script? If yes, extract.

## When NOT to make a script

- Single-use commands tied to a specific PR (e.g. "reproduce issue
  #1020 by running X with these args") — these live in the PR body, not
  in `scripts/perf/`.
- Decisions that require judgment (which optimization to pick, whether
  the trade-off is worth it) — these stay in the skill body as
  guidance, not as runnable code.
- One-off measurement queries during a specific investigation —
  scratch shell commands are fine. Promote to a script only when the
  same query has run twice.

## Cross-reference

This principle is engine-wide, not optimize-specific. The engine-wide
version of this rule will live in
[`docs/agents/CLAUDE-BASELINE.md`](../../../../docs/agents/CLAUDE-BASELINE.md)
§"Script-first for repeatable work" (forthcoming — added by the
follow-up PR that cross-references this file from CLAUDE-BASELINE and
FLEET.md).

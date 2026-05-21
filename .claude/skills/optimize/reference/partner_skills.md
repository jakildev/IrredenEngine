# partner skills — what to invoke around optimize

The optimize skill doesn't do regression checking, visual verification,
or backend parity itself. It produces perf numbers; other skills do the
adjacent work. Cross-reference these so the right one fires at the
right time.

## simplify (runs after optimize)

Polishes the dirty tree before commit — catches ECS smells (per-entity
getComponent in tick, function-local static, math primitive
violations), naming convention slips, dead code, debug logs.

**Order**: optimize first, then simplify. Optimize may add
`IR_PROFILE_*` blocks and perf-rationale comments that simplify
preserves (they're not the kind of "extraneous" simplify removes —
profiling macros are engine surface, perf-rationale comments explain
non-obvious why).

**Cross-ref**: `.claude/skills/simplify/SKILL.md`.

## render-verify (visual regression check)

Automated pass/fail render-regression harness — builds a demo, runs
with `--auto-screenshot`, compares each shot against a committed
reference image. Run **after** any optimize change that touches the
render pipeline, to catch visual regressions in pursuit of perf.

A 20% frame-time win that breaks the shape silhouette is not a win.
render-verify is the gate that catches this.

**Cross-ref**: `.claude/skills/render-verify/SKILL.md`.

## render-debug-loop (interactive iteration)

Build → run → screenshot → diagnose loop for visual / render bugs.
Different from render-verify (which is pass/fail) — used during active
iteration when you're trying to understand a render bug or align two
shapes pixel-for-pixel.

Useful **before** optimize on render PRs where the visual semantics
might be unclear — get the visual right, then optimize.

**Cross-ref**: `.claude/skills/render-debug-loop/SKILL.md`.

## attach-screenshots (PR-body evidence)

Captures before/after screenshots for a render PR and commits them
under `docs/pr-screenshots/<branch>/`. Runs against origin/master and
against the dirty tree, pairs by shot label, produces markdown.

Run **alongside** optimize when the perf PR touches render — reviewers
need both the perf diff (from `compare_perf_runs.py`) and the visual
diff (from attach-screenshots) to evaluate the trade-off.

**Cross-ref**: `.claude/skills/attach-screenshots/SKILL.md`.

## polish-checkpoint (mid-session verify)

Runs the pre-commit phase of `commit-and-push` (simplify + build) but
stops before any git operations. Useful when you want confidence that
the working tree is clean and the build is green without committing
yet.

Run between iteration cycles in a long optimization session.

**Cross-ref**: `.claude/skills/polish-checkpoint/SKILL.md`.

## backend-parity (cross-platform sync)

Keeps the OpenGL (Linux/Windows) and Metal (macOS) backends in
functional sync. Render PRs almost always author against only one
backend; the other lags until a fleet agent on that host catches up.

If your optimize PR touches a shader (`*.glsl` or `*.metal`), run
backend-parity **after** the perf measurement on the authoring backend
to port the change to the other side. Both backends should show the
matrix improvement; if Metal regresses where OpenGL improved (or vice
versa), the port has a bug.

**Cross-ref**: `.claude/skills/backend-parity/SKILL.md`.

## commit-and-push (open the PR)

When the optimization is done, `commit-and-push` packages the diff,
runs simplify automatically, opens the PR. The PR body should include
the `compare_perf_runs.py` markdown output.

**Cross-ref**: `.claude/skills/commit-and-push/SKILL.md`.

## start-next-task (stack the next optimization)

If the optimization session uncovered a follow-up opportunity,
`start-next-task` stacks the next branch on the current PR's head.
Useful when an optimization is bounded but exposes a deeper issue —
land what you have, stack the deeper fix.

**Cross-ref**: `.claude/skills/start-next-task/SKILL.md`.

---

## Workflow shape

```
        ┌─────────────────────────────────────────────┐
        │ start-next-task → branch off master or stack│
        └────────────────────┬────────────────────────┘
                             ▼
        ┌─────────────────────────────────────────────┐
        │ optimize → matrix baseline → fix → matrix → │
        │           compare → step 7 self-improve     │
        └────────────────────┬────────────────────────┘
                             ▼
                   ┌─────────────────────┐
       render PR? ─▶ render-verify       │
                   │ attach-screenshots  │
                   │ backend-parity      │
                   └──────────┬──────────┘
                              ▼
        ┌─────────────────────────────────────────────┐
        │ commit-and-push → simplify auto-runs → PR   │
        └─────────────────────────────────────────────┘
```

If a step in the diagram repeated more than once, it's a skill — not
a manual instruction.

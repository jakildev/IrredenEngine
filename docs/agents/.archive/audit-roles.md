# audit-roles — shared protocols + point-don't-dump

**Scope.** All 7 `.claude/commands/role-*.md` files (4,506 lines total).
**Baselines compared against.** `docs/agents/FLEET.md` (645 lines),
`docs/agents/FLEET-CACHE.md` (110), `docs/agents/CLAUDE-BASELINE.md`
(370), `docs/agents/BUILD.md` (229). The expectation is that any rule
or procedure defined in those four docs is referenced by name from a
role file, not restated. **Sister audits.** `docs/agents/audit-skills.md`
(T-222, merged via PR #804) and `docs/agents/audit-claude-md.md` (T-223,
in flight) cover the rest of the docs surface; this note completes the
trio.

**Method.** Two subagents read 3 + 4 role files in full alongside the
four baseline docs, extracting duplicated blocks, dump violations,
stale anchors, and one-PR-sized follow-up tasks. Targeted greps
confirmed pattern frequencies and validated the highest-impact stale
findings (the missing `fleet:awaiting-base` label, the broken
`see CRITICAL section above` anchor in every role's `## Hard rules`,
the inverted `Engine API removal rule` citation between
CLAUDE-BASELINE.md and the worker/architect role files). File:line
citations anchor to the working-tree state on branch
`claude/T-221-roles-audit` at the time this note was written.

**Headline.** The role files weigh in at ~4,500 lines, with the two
authoring roles (`role-opus-worker.md`, `role-sonnet-author.md`)
making up ~2,150 lines on their own — and most of those are
near-verbatim duplicates of each other. The single largest dup is the
feedback-label handling block (~210 lines × 2). The reviewer pair
duplicates the stack-awareness procedure (~44 lines × 2) plus the
verdict label-swap and nits-vs-needs-fix blocks. Three role files
restate the "Engine API removal rule" that CLAUDE-BASELINE.md
already owns, with the citation direction inverted (baseline points
to the roles). Six role files share an identical broken anchor in
their `## Hard rules` epilogue. Estimated total reduction after the
proposed cleanups: ~1,400 lines deleted from role files, ~800 lines
of new shared content under `docs/agents/`, for a net ~600-line
reduction with two new shared docs absorbing the operational
protocols.

---

## 1. Duplicated >=5-line blocks across role files

The blocks below appear in two or more role files at >=5 lines of
near-verbatim or paraphrased content. Each row names the proposed
canonical shared home and is enumerated as a cleanup PR in §5.

### 1.1 Feedback-label handling (AMEND vs ESCALATE, step b TOCTOU, label cycle)

| File | Range | Lines |
|---|---|---|
| `role-opus-worker.md` | `228-436` | ~210 |
| `role-sonnet-author.md` | `143-371` | ~230 |

Both worker roles carry the full procedure: priority order across
`human:needs-fix` / `fleet:needs-fix` / `fleet:has-nits` /
`fleet:design-unblocked`, AMEND-default vs ESCALATE decision rule,
step-b branch-claim TOCTOU + idempotent label removal +
`fleet:human-amending` + `fleet-claim reserve` mechanic, response-
label conventions, downstream rebase propagation, and reservation
release. Differences between the two are cosmetic except for one
real divergence (see §3.1).

**Right home.** New `docs/agents/FLEET-FEEDBACK-HANDLING.md`. Both
authoring roles need the full procedure; the architect role
(`role-opus-architect.md:167-194`) carries a leaner version that
should also point at the new doc once T-CLEANUP-9 catches it up.

### 1.2 Stack-awareness gate (reviewer side)

| File | Range | Lines |
|---|---|---|
| `role-opus-reviewer.md` | `197-243` | ~44 |
| `role-sonnet-reviewer.md` | `198-251` | ~50 |

Same procedure: detect non-master base, look up upstream PR, gate on
upstream's `fleet:approved` label, set `fleet:awaiting-upstream-review`
when ungated, otherwise review child diff only. Wording differs only
in the verdict-body "Stack context" template.

**Right home.** New `docs/agents/REVIEWER-PROTOCOL.md`. Both
reviewers need the full text.

### 1.3 Stacked PR per-task branching + creation sequence (author side)

| File | Range | Lines |
|---|---|---|
| `role-opus-worker.md` | `867-927` | ~60 |
| `role-sonnet-author.md` | `575-635` | ~60 |

Stack-base lookup → branching off `origin/$base` → `--base $base
--label fleet:wip --label fleet:stacked` PR creation → stacked PR
title/body template → "when an earlier PR merges" rebase guidance.
`FLEET.md:303-385` § "Stacked PRs" describes the scheduler-level
mechanics but not the per-task command sequence. The roles' inline
restatements are operational and identical.

**Right home.** Move the per-task command sequence into the
`commit-and-push` SKILL.md "Stack-aware mode" section that the role
files already point at (`role-opus-worker.md:916-918`,
`role-sonnet-author.md:624-626`) — eliminate the inline duplication
in favor of the existing pointer.

### 1.4 Molecule resume / advance / complete protocol

| File | Range | Lines |
|---|---|---|
| `role-opus-worker.md` | `679-746` | ~68 |
| `role-sonnet-author.md` | `448-489` | ~42 |

`fleet-claim molecule resume` stdout discriminator, resume-vs-
restart judgment, `molecule advance` reminder, `molecule complete`
on empty-but-archivable stdout. The opus-worker version has a
`--repo game` cross-repo extension; the rest is byte-equivalent.

**Right home.** New `docs/agents/FLEET-MOLECULES.md` (or extend
`FLEET.md` § "Stacked PRs" with a "Molecule resume protocol"
subsection). Both roles need the full procedure.

### 1.5 Cross-host smoke validation step (step 1b)

| File | Range | Lines |
|---|---|---|
| `role-opus-worker.md` | `438-502` | ~65 |
| `role-sonnet-author.md` | `373-446` | ~74 |
| `role-opus-reviewer.md` | `297-319` | ~22 (tagging only) |
| `role-sonnet-reviewer.md` | `342-364` | ~22 (tagging only) |

Two views of the same protocol: the reviewers _tag_ approved render
PRs with `fleet:needs-<other-host>-smoke`; the authors _claim_ those
labels, build + run the smoke target, and clear or escalate. The
two authoring copies are near-verbatim except for one
escalation-wording delta (see §3.2). The two reviewer copies are
verbatim except for which-host pronoun.

**Right home.** New `docs/agents/FLEET-CROSS-HOST-SMOKE.md` (or a
single `FLEET.md` § "Cross-host smoke validation" subsection
covering both halves: tagging by reviewers, claiming by authors).
All four roles point at the shared doc.

### 1.6 Stackable-blocked fallback tier paragraph

| File | Range | Lines |
|---|---|---|
| `role-opus-worker.md` | `786-798` | ~13 |
| `role-sonnet-author.md` | `516-529` | ~14 |

Both restate the scout's `stackable_blocker_pr` projection field,
the same multi-blocker exclusion, and the `--stackable-on` claim
command. `FLEET.md:319-385` § "Cross-author stacking (scheduler)"
already walks this scenario with PR numbers.

**Right home.** FLEET.md § "Cross-author stacking" — roles
collapse to a one-line "fall through to FLEET.md's stackable-on
tier when no fully-unblocked tasks remain" pointer.

### 1.7 Verdict label-swap commands (reviewer side)

| File | Range | Lines |
|---|---|---|
| `role-sonnet-reviewer.md` | `305-319` | ~14 |
| `role-opus-reviewer.md` | `277` | 1 (implicit) |

Sonnet has the explicit 4-command block (approve / approve+nits /
needs-fix / blocker); Opus does not, just inline references.

**Right home.** REVIEWER-PROTOCOL.md § "Verdict label-swap commands"
— both reviewers point at the same block so they cannot drift apart.

### 1.8 Nits vs needs-fix — the bright line

| File | Range | Lines |
|---|---|---|
| `role-opus-reviewer.md` | `321-341` | ~20 |
| `role-sonnet-reviewer.md` | `366-385` | ~20 |

Same 4-bullet structure: approve-with-nits criteria, the forbidden
contradiction (`fleet:approved` + `fleet:needs-fix` together), and
the in-doubt heuristic.

**Right home.** REVIEWER-PROTOCOL.md § "Nits vs needs-fix".

### 1.9 `fleet-claim review-claim` acquire + release

| File | Range | Lines |
|---|---|---|
| `role-opus-reviewer.md` | `180-192` + `285-296` | ~25 |
| `role-sonnet-reviewer.md` | `176-189` + `321-330` | ~24 |

GitHub-label atomic lock acquisition, exit-code semantics,
"acquire FIRST, release after verdict" rule. The same protocol
also surfaces lightly in the author roles (`role-opus-worker.md:465`
+ `493`, `role-sonnet-author.md:400` + `427`) where the wrappers
are referenced once each.

**Right home.** REVIEWER-PROTOCOL.md § "Acquiring / releasing the
review claim".

### 1.10 Heartbeat instructions (step 0)

| File | Range | Lines |
|---|---|---|
| `role-opus-worker.md` | `194-206` | ~13 |
| `role-sonnet-author.md` | `112-121` | ~10 |
| `role-merger.md` | `117-130` | ~14 |
| `role-opus-reviewer.md` | `168-172` | ~5 |
| `role-sonnet-reviewer.md` | `149-153` | ~5 |

`fleet-heartbeat <worktree-basename>` invocation + wrapper-vs-touch
rationale (avoid the path-scope prompt on raw `touch ~/...`) + the
re-touch reminder before long-running ops. The two authoring roles
have the longest copies; the reviewers' are shorter but still
duplicated.

**Right home.** New `docs/agents/FLEET-RUNTIME.md` (heartbeat +
reservation + exit + per-iteration shutdown — see §1.11–1.13).

### 1.11 Reservation check (step 0.5)

| File | Range | Lines |
|---|---|---|
| `role-opus-worker.md` | `208-226` | ~19 |
| `role-sonnet-author.md` | `123-141` | ~19 |

`fleet-claim reservation-of`, Empty/Non-empty discriminator, JSON
read for the `branch` field, `git checkout`, jump-into-step-5
behavior. Byte-equivalent except the step number the agent jumps
to (worker step 5 vs author step 4).

**Right home.** FLEET-RUNTIME.md.

### 1.12 Exit protocol (transient roles)

| File | Range | Lines |
|---|---|---|
| `role-opus-worker.md` | `48-67` | ~20 |
| `role-sonnet-author.md` | `37-56` | ~20 |
| `role-merger.md` | `50-59` | ~10 |
| `role-opus-reviewer.md` | `52-61` | ~10 |
| `role-sonnet-reviewer.md` | `46-55` | ~10 |

All five transient roles carry near-identical exit-protocol text:
stop emitting tool calls, no `kill -TERM $PPID`, the auto-mode
classifier rationale. The two authoring roles' copies are the
longest; the three review/merge roles have a more compressed form.

**Right home.** FLEET-RUNTIME.md § "Exit protocol".
`role-opus-architect.md` (not transient — interactive) does not
need it.

### 1.13 Reset + per-iteration summary block (final step)

| File | Range | Lines |
|---|---|---|
| `role-opus-worker.md` | `1134-1156` | ~23 |
| `role-sonnet-author.md` | `792-815` | ~24 |
| `role-merger.md` | `741-754` | ~14 |
| `role-opus-reviewer.md` | `342-360` | ~19 |
| `role-sonnet-reviewer.md` | `386-403` | ~18 |

`fleet-iteration-summary` invocation, the "do NOT use backticks in
the summary text" warning (cite of past breakage), `release-
worktree` before `start-next-task` (cite of issue #521), and the
exit banner.

**Right home.** FLEET-RUNTIME.md § "Per-iteration shutdown" — all
five roles collapse to a 3-line pointer plus the role-specific
summary placeholder.

### 1.14 Hard rules block

| File | Range | Lines |
|---|---|---|
| `role-opus-worker.md` | `1213-1260` | ~48 |
| `role-sonnet-author.md` | `872-911` | ~40 |
| `role-opus-architect.md` | `359-376` | ~18 |
| `role-merger.md` | `780-812` | ~33 |
| `role-opus-reviewer.md` | `390-426` | ~37 |
| `role-sonnet-reviewer.md` | `435-467` | ~33 |
| `role-queue-manager.md` | `191-209` | ~19 |

Every role file ends with a `## Hard rules` epilogue listing the
same baseline prohibitions: never push master, never `--force`,
never `gh pr merge`, never `cmake --preset`, never `sudo`/`brew`/
`apt`, never touch the `.claude/worktrees/` layout, plus an
identical broken cross-reference at the end of each list (see
§3.4).

**Right home.** Add `## Hard rules for autonomous fleet roles` to
`CLAUDE-BASELINE.md`. Every role file collapses to a single-line
pointer plus any role-specific addition (e.g., the merger's
"never write `merge` commits" addition).

### 1.15 Verify-visual-output (render verification gate)

| File | Range | Lines |
|---|---|---|
| `role-opus-worker.md` | `1057-1098` | ~42 |
| `role-sonnet-author.md` | `721-767` | ~47 |

Trigger file set (`engine/render/`, `engine/prefabs/irreden/render/`,
`*.glsl`/`*.metal`, `creations/demos/*/src/**`), `attach-screenshots`
+ `render-debug-loop` requirement, exceptions list pointer. Both
copies already point at `engine/render/CLAUDE.md` "Verifying
render changes" — they are pure re-explanations of that pointer.

**Right home.** Each role keeps a 5-line pointer to
`engine/render/CLAUDE.md` "Verifying render changes" and to the
two skill names. Strong candidate for shrinkage.

### 1.16 Build-and-run (per-target run patterns)

| File | Range | Lines |
|---|---|---|
| `role-opus-worker.md` | `983-1000` | ~18 |
| `role-sonnet-author.md` | `685-702` | ~18 |

`fleet-build --target <name>`, the prefabs-systems "also build
`IrredenEngineTest`" wrinkle, `fleet-run` patterns for demos
(`--auto-screenshot`), GUI executables (`--timeout 15`), test
executables, and the "never `cd <dir> && ./<exe>`" warning.

**Right home.** Extend `BUILD.md` (it already covers `fleet-build`
+ `fleet-run` at `BUILD.md:66-103, 178-199`). The prefabs-systems
wrinkle and the demo-vs-GUI run pattern table are net-new and
belong there.

### 1.17 Optimize-before-commit

| File | Range | Lines |
|---|---|---|
| `role-opus-worker.md` | `1099-1114` | ~16 |
| `role-sonnet-author.md` | `769-783` | ~15 |
| `role-opus-architect.md` | `140-153` | ~14 |

"Run `optimize` for perf-critical paths", "commit-and-push runs
simplify so do not invoke separately", "re-run optimize when
addressing review feedback that touches hot paths".

**Right home.** The skill description itself (`optimize/SKILL.md`)
plus one cross-cutting sentence in `FLEET.md` § quality skills.
Role files reduce to one-sentence pointers.

### 1.18 Bash tool rules pointer + duplicated bullet summary

| File | Range | Lines |
|---|---|---|
| `role-opus-worker.md` | `23-29` | ~7 |
| `role-sonnet-author.md` | `13-19` | ~7 |
| `role-opus-architect.md` | `13-19` | ~7 |
| `role-merger.md` | `17-31` | ~15 |
| `role-opus-reviewer.md` | `14-28` | ~15 |
| `role-sonnet-reviewer.md` | `13-27` | ~15 |
| `role-queue-manager.md` | `12-16` | ~5 |

All seven role files have a `## Bash tool rules` section that
points at `CLAUDE-BASELINE.md § Bash tool rules` (good) then
restates 4-6 bullets of the rule (bad). The three review/merge
roles also restate the same "role-specific Write-body-file"
warning where only the filename changes (`.review-body.md` vs
`.merger-body.md`).

**Right home.** CLAUDE-BASELINE.md gains a one-sentence "When
writing PR review/merger body files, keep them inside the
worktree" addendum; each role keeps only its filename inline.
The 4-6 bullet restatement disappears.

### 1.19 Shared-fleet-state-cache header

| File | Range | Lines |
|---|---|---|
| `role-opus-worker.md` | `31-46` | ~16 |
| `role-sonnet-author.md` | `21-35` | ~15 |
| `role-opus-architect.md` | `21-35` | ~15 |
| `role-merger.md` | `33-48` | ~16 |
| `role-opus-reviewer.md` | `30-50` | ~21 |
| `role-sonnet-reviewer.md` | `29-44` | ~16 |
| `role-queue-manager.md` | `18-28` | ~11 |

Every role restates the slice-vs-full-state comparison (~5 KB vs
~32 KB) and the closing pointer to FLEET-CACHE.md. Each one is
correct on its own, but every clarification (e.g., a new size
metric) currently has to land in seven places.

**Right home.** FLEET-CACHE.md is the canonical doc. Each role
collapses to a 2-line "Read your slice at `<path>` (see
FLEET-CACHE.md for the protocol)."

### 1.20 Repo-slug discovery in startup actions

| File | Range | Lines |
|---|---|---|
| `role-merger.md` | `97-103` | ~7 (engine-only) |
| `role-opus-reviewer.md` | `105-113` | ~9 (both repos) |
| `role-sonnet-reviewer.md` | `74-82` | ~9 (both repos) |
| `role-queue-manager.md` | `67-69` | ~3 (both repos) |

Same procedure: read `~/.fleet/state/repos.json`, fall back to
`gh repo view --jq .nameWithOwner`, treat absent game as "skip
game-side ops". `FLEET-CACHE.md` § "Repo slug discovery" at lines
86-95 already owns this.

**Right home.** Each role replaces with a 1-line pointer.

### 1.21 Engine API removal rule

| File | Range | Lines |
|---|---|---|
| `role-opus-worker.md` | `86-99` | ~14 |
| `role-opus-architect.md` | `53-66` | ~14 |
| `CLAUDE-BASELINE.md` | `282-290` | ~9 (shorter version) |

`CLAUDE-BASELINE.md` is the baseline doc — but its version of the
rule ends "See the 'Engine API removal rule' section of
`role-opus-architect.md` and `role-opus-worker.md` for the full
guidance." The citation direction is **inverted**: the baseline
should be canonical, not point downstream to two role files that
repeat the same guidance.

**Right home.** CLAUDE-BASELINE.md, expanded to full canonical
form. Both role files collapse to a 2-line pointer. Counted again
as a dump violation in §2.

### 1.22 Fleet feedback channel restatement

| File | Range | Lines |
|---|---|---|
| `role-opus-worker.md` | `1202-1211` | ~10 |
| `role-sonnet-author.md` | `861-870` | ~10 |
| `role-opus-architect.md` | `350-357` | ~8 |
| (plus shorter forms in merger/reviewer roles) | | |

Each role restates the `~/.fleet/feedback/<basename>.md` append
path and the "see FLEET.md 'Fleet feedback channel' for format
and bar" pointer.

**Right home.** Roles keep a 2-line pointer; the path can stay
inline since it depends on the role's basename.

---

## 2. Dump violations (point-don't-dump)

The role files mostly do their citations well — most violations
below are restatements alongside an otherwise-correct pointer
rather than outright dumps.

### 2.1 Engine API removal rule — inverted citation direction

Already enumerated in §1.21. `CLAUDE-BASELINE.md:282-290` ends
with "See the 'Engine API removal rule' section of
`role-opus-architect.md` and `role-opus-worker.md` for the full
guidance" — pointing downstream into role files that simply
repeat the rule. The baseline doc should be the long-form home;
the role files should point at the baseline.

### 2.2 Never-push-master / never-force / never-merge — restated verbatim in every Hard rules block

Each role's `## Hard rules` (see §1.14) restates `FLEET.md:11-22`
verbatim. `FLEET.md` already owns the workflow rules and
top-level `CLAUDE.md` § "Workflow at a glance" reiterates them.

### 2.3 Bash tool rules — pointer present but bullets duplicated

Already enumerated in §1.18. All seven roles point at
`CLAUDE-BASELINE.md § Bash tool rules` (lines 180-225) and then
restate 4-6 bullets of the same rule.

### 2.4 Label state machine restated in reviewer/merger skip lists

- `role-merger.md:355-376` lists every fleet/human label with skip
  semantics.
- `role-merger.md:483-488` describes derived-state convenience
  labels (`fleet:stacked`, `fleet:awaiting-base`, `fleet:stacked-
  rebase`, `fleet:needs-base-update`) without pointing at FLEET.md.
- `role-merger.md:814-845` `## How the cooldown label works` is
  the merger's full description of the label's durable-vs-cooldown
  semantics — this belongs in FLEET.md's label dictionary, not in
  the role doc.
- `role-opus-reviewer.md:147-160` and `role-sonnet-reviewer.md:122-141`,
  `:163-172` carry skip lists that restate label ownership.

`FLEET.md:446-589` § "Issue/PR labeling discipline" is the
canonical source.

### 2.5 Cache structure restated in role intros

Already enumerated in §1.19. FLEET-CACHE.md is the canonical
home; each role's "Shared fleet state cache" section restates
the size comparison and structure.

### 2.6 Cross-repo information isolation — undercited

The reviewer and merger roles never reference
`CLAUDE-BASELINE.md § Cross-repo information isolation`
(`CLAUDE-BASELINE.md:229-292`) even though they routinely write
public engine PR comments. `role-queue-manager.md:96-103`
restates a fragment ("engine repo is public, no game context
may appear") at step 1 but does not point at the canonical
section. The rule applies more broadly than the queue-manager
alone; it should be cited from every author/reviewer/merger role
exactly once.

### 2.7 Design-escalation flow — deliberate split, but worker role re-narrates the cycle

`FLEET.md:192-219` describes the architect-escalation flow
abstractly, then explicitly says "the full per-role procedure is
in `role-opus-worker.md` (escalate + resume) and
`role-opus-architect.md`." This is intentional. But
`role-opus-worker.md:1014-1018` re-narrates the cycle inline
instead of pointing at FLEET.md — a minor restatement on top of
an otherwise-clean split.

### 2.8 Per-iteration shutdown / iteration summary — restated in five role files

Already enumerated in §1.13. The "do NOT use backticks in the
summary text" warning is repeated five times with the same
rationale; the same applies to the `release-worktree` ordering
citation of #521.

---

## 3. Cross-role drift

Real divergences in the procedures across role files where the
two copies do *not* agree.

### 3.1 `fleet-pr-clear-feedback-labels` wrapper — adopted by opus-worker only

- `role-opus-worker.md:360-368` uses the idempotent wrapper:
  ```
  fleet-pr-clear-feedback-labels <N>
  ```
  Wraps the live-query + per-label `gh pr edit --remove-label`
  chain. Comments cite past breakage on PR #637 (2026-05-11,
  2026-05-12) as the motivating bug.
- `role-sonnet-author.md:261` still uses the raw chained form:
  ```
  gh pr edit <N> --remove-label "human:needs-fix" --remove-label "human:blocker" --remove-label "fleet:needs-fix" --remove-label "fleet:has-nits" --remove-label "fleet:human-deferred"
  ```
  This is the exact failure mode the wrapper exists to prevent
  (`gh pr edit --remove-label X --remove-label Y` exits non-zero
  on the first absent label, leaving any earlier labels removed
  but the later ones intact).
- `role-opus-architect.md:185-186` uses the same raw chained
  form when the architect handles a feedback PR.

**Verify:** the wrapper exists at `scripts/fleet/fleet-pr-clear-
feedback-labels`. Only `role-opus-worker.md` has adopted it.

### 3.2 `fleet:awaiting-base` vs `fleet:awaiting-upstream-review` — possible label drift

- `role-merger.md:362, 437, 447, 478, 819-825` uses
  `fleet:awaiting-base` as a durable handoff label set by the
  merger when a stacked PR's base is OPEN at rebase time.
- `role-opus-reviewer.md:233` and `role-sonnet-reviewer.md:241`
  use `fleet:awaiting-upstream-review` when a stacked PR's
  upstream lacks approval at review time.
- `FLEET.md:514-578` documents `fleet:awaiting-upstream-review`
  in the label dictionary but does **not** document
  `fleet:awaiting-base`.

These may describe two genuinely distinct PR states (base merge
vs upstream review), or they may be unintentional label drift.
Either way, FLEET.md's label dictionary is incomplete.

### 3.3 Architect's `fleet:approved` handling is stale on `human:needs-fix`

`role-opus-architect.md:189-193` handles only `fleet:has-nits` /
`fleet:needs-fix` and says `fleet:approved` "stays valid". It
never describes the AMEND-path `human:needs-fix` flow where
`fleet:approved` MUST be cleared so the human knows to hold the
merge (per `role-opus-worker.md:374-376` and
`role-sonnet-author.md:267`). Architect role docs is stale on
this point.

### 3.4 `(see CRITICAL section above)` — broken anchor in every Hard rules block

Verified: every role's `## Hard rules` ends with the line
`Single-command Bash only (see CRITICAL section above)` but no
`## CRITICAL` header exists in any of these files. The actual
section is named `## Bash tool rules`.

Affected:
- `role-merger.md:812`
- `role-opus-architect.md:376`
- `role-opus-reviewer.md:426`
- `role-opus-worker.md:1255`
- `role-sonnet-author.md:906`
- `role-sonnet-reviewer.md:467`

`role-queue-manager.md:209` says "see CLAUDE-BASELINE.md above"
— the file is correct but "above" is misleading (the rule lives
in CLAUDE-BASELINE.md, which is a separate doc).

### 3.5 Architect references a non-existent 20-minute loop

`role-opus-architect.md:230-231`: "The **opus worker**
autonomously handles `fleet:needs-plan` issues on its 20-minute
loop". But `role-opus-worker.md:48-67` explicitly describes the
opus-worker as a **transient one-shot** that re-fires when the
scout sees actionable state. There is no 20-minute loop. The
architect doc is stale.

### 3.6 Cross-host smoke escalation wording diverges

`role-sonnet-author.md:440-446` adds a Sonnet-specific
escalation note about not inspecting screenshots and deferring
visual judgment to Opus. `role-opus-worker.md` (the analogous
step) does not have the analogous "Opus *does* inspect" caveat —
the role split is implicit. The proposed FLEET-CROSS-HOST-SMOKE.md
should document the split explicitly.

### 3.7 Redundant `--repo <engine-repo>` flags in merger

The merger is engine-only (v1 scope per `role-merger.md:67-70`)
and runs from the engine clone (`role-merger.md:96-97` confirms
`pwd` is engine). `gh pr edit <N>` infers the right repo from
cwd, so `--repo <engine-repo>` is only needed when overriding —
which the merger never does. The flag appears 30+ times in the
file (lines 140, 185, 246, 306-307, 343, 416, 436-449, 477-480,
535-537, 661, 710, 716-720, etc.) as redundant.

---

## 4. Stale content

Items that should be removed or rewritten, beyond the drifts in §3.

### 4.1 `git checkout origin/master -- ...` warning in worker role

`role-opus-worker.md:167-169` warns "Do NOT use `git checkout
origin/master -- ...` — it stages the files and breaks later
`git checkout -b`". This is plan-file-reading-specific advice
that is isolated in the worker role. Move to FLEET-RUNTIME.md
or to BUILD.md alongside the other git-safety rules.

### 4.2 `kill -TERM $PPID` deprecation paragraph in every transient role

`role-opus-worker.md:61-67` and `role-sonnet-author.md:50-56`
each keep a paragraph explaining why the OLD approach (`kill
-TERM $PPID`) is no longer used. The merger / reviewer roles
keep a shorter version of the same. This was useful during the
transition; after a quarter it becomes archeology. Move into a
one-line "auto-mode classifier blocks `kill -TERM $PPID`" note
in FLEET-RUNTIME.md and drop the long-form paragraphs.

### 4.3 Bogus re-stamp PR examples in reviewer roles

- `role-opus-reviewer.md:421-422`: "Observed bogus re-stamps:
  PRs #347, #348, #394".
- `role-sonnet-reviewer.md:463-464`: same, plus `#402`.

PR-number citations accumulate cruft over time. The failure-
mode description belongs in the role; the specific PRs belong
in a postmortem/lessons-learned doc (or a comment with a date,
so future readers know to drop them).

### 4.4 Merger references `merger.log` (tail-rotated) with no visible rotation config

`role-merger.md:579-580, 803, 851-853` distinguishes
`~/.fleet/logs/merger-audit.log` (append-only) from `merger.log`
(tail-rotated). The fleet scripts directory does not contain a
rotation config for `merger.log` visible from `scripts/fleet/`
or `~/.fleet/`. Either the rotation lives somewhere off-tree
(dispatcher behavior) or the doc is stale. Verify before T-CLEANUP-3
edits land near this line.

### 4.5 Architect step 1c label-removal call non-atomic

`role-opus-architect.md:185-186` uses the same raw chained
`gh pr edit --remove-label A --remove-label B --remove-label C`
form that PR #637 exposed as non-atomic. Should adopt
`fleet-pr-clear-feedback-labels` once the wrapper is generalized
(or use the wrapper now since it already exists). Same drift as
§3.1.

### 4.6 Architect doc has no `--repo game` namespace guidance

`role-opus-architect.md` never mentions game-side operations or
`--repo game` namespacing. The architect is interactive and may
not autonomously claim game tasks, but if the human directs the
architect to plan a game-side issue (which the role doc does
permit at lines 228-269), there's no procedural guidance. Minor
freshness gap.

### 4.7 Queue-manager has no `## End-of-iteration feedback` section

Every transient role has a `## End-of-iteration feedback`
section pointing at `~/.fleet/feedback/<basename>.md`. The
queue-manager has no such section (verified via grep on
`feedback` in `role-queue-manager.md`). Either the queue-manager
explicitly does not produce feedback (and the role doc should
say so) or this is a real omission.

### 4.8 Sonnet-author's `fleet:human-deferred` removal in AMEND path

`role-sonnet-author.md:261` removes `fleet:human-deferred` in
the AMEND-path label-cleanup chain. But:
- The AMEND path only triggers for `human:needs-fix` /
  `human:blocker`, NOT `fleet:human-deferred` (which is the
  ESCALATE-path outcome).
- Removing `fleet:human-deferred` here unconditionally could
  clobber a deliberately-set human-deferred state. The opus-
  worker wrapper handles this correctly via "remove only if
  present"; the sonnet-author raw chain does not.

This is the same drift as §3.1 from a different angle.

---

## 5. Proposed follow-up cleanup tasks

Sized for one PR each. Filed as GitHub issues for the queue-manager
to ingest.

### T-CLEANUP-A: Hoist feedback-label handling into `docs/agents/FLEET-FEEDBACK-HANDLING.md`

- **New file:** `docs/agents/FLEET-FEEDBACK-HANDLING.md`
  covering priority order, AMEND-vs-ESCALATE decision,
  AMEND-path step b (TOCTOU + label-clear-wrapper + reservation),
  ESCALATE-path body fields + atomic swap, response-label
  conventions, downstream rebase propagation, reservation
  release.
- **Edit:** `role-opus-worker.md:228-436` and
  `role-sonnet-author.md:143-371` → ~30-line pointer + the
  role-specific reservation rule.
- **Also edit:** `role-opus-architect.md:167-194` → catch up to
  the same protocol (this fixes §3.3 staleness).
- **Resolves:** §1.1, §3.1, §3.3, §3.6 (partial), §4.5, §4.8.
- **Diff size:** +~250 lines new file, -~550 lines across three
  roles. Net -300.

### T-CLEANUP-B: Create `docs/agents/REVIEWER-PROTOCOL.md`

- **New file:** `docs/agents/REVIEWER-PROTOCOL.md` covering
  acquire/release claim, stack awareness, verdict label-swap
  commands (the explicit 4-command block), cross-host smoke
  tagging, nits-vs-needs-fix bright line.
- **Edit:** `role-opus-reviewer.md` — replace lines 180-192,
  197-243, 277, 285-296, 297-319, 321-341 with pointers.
- **Edit:** `role-sonnet-reviewer.md` — replace lines 176-189,
  198-251, 286-319, 321-330, 342-364, 366-385 with pointers.
- **Resolves:** §1.2, §1.7, §1.8, §1.9.
- **Diff size:** +~250 lines new file, -~150 lines across two
  reviewers. Net +100.

### T-CLEANUP-C: Hoist molecule / stacked-PR per-task command sequence into FLEET.md (or new FLEET-MOLECULES.md)

- **Edit:** `FLEET.md` § "Stacked PRs" — add subsections "Molecule
  resume protocol" and "Per-task stacked PR command sequence".
  Cover stack-base / stack-set-pr / claim-base / `--stackable-on`.
- **Edit:** `role-opus-worker.md:679-960` and
  `role-sonnet-author.md:448-664` → reduce each to a ~40-line
  pointer.
- **Resolves:** §1.3, §1.4, §1.6.
- **Diff size:** +~250 lines FLEET.md, -~500 across two roles.
  Net -250.

### T-CLEANUP-D: Create `docs/agents/FLEET-RUNTIME.md`

- **New file:** `docs/agents/FLEET-RUNTIME.md` covering heartbeat,
  reservation-of check, exit protocol, per-iteration shutdown
  (reset, summary, no-backticks warning, release-worktree
  ordering, completion banner), the `kill -TERM $PPID`
  deprecation note in compressed one-line form.
- **Edit:** All five transient roles reduce intro sections + final
  shutdown sections to pointers.
- **Resolves:** §1.10, §1.11, §1.12, §1.13, §4.1 (move git-
  checkout warning here), §4.2.
- **Diff size:** +~120 lines new file, -~250 across five roles.
  Net -130.

### T-CLEANUP-E: Create `docs/agents/FLEET-CROSS-HOST-SMOKE.md`

- **New file:** `docs/agents/FLEET-CROSS-HOST-SMOKE.md` covering
  the full protocol — reviewer tagging (set the label on
  approved render PRs), author claiming (build + smoke + verdict
  + label clear or escalation), and the explicit Sonnet-vs-Opus
  split (`§3.6`: Sonnet runs the smoke but defers visual judgment;
  Opus inspects screenshots).
- **Edit:** `role-opus-worker.md:438-502` and
  `role-sonnet-author.md:373-446` → 5-line pointer.
- **Edit:** `role-opus-reviewer.md:297-319` and
  `role-sonnet-reviewer.md:342-364` → 5-line pointer.
- **Resolves:** §1.5, §3.6.
- **Diff size:** +~150 lines new file, -~170 lines across four
  roles. Net -20 lines.

### T-CLEANUP-F: Hoist Hard rules into CLAUDE-BASELINE.md

- **Edit:** Add `## Hard rules for autonomous fleet roles` to
  `CLAUDE-BASELINE.md` covering never push master, never `--force`,
  never `gh pr merge`, never `cmake --preset`, never `sudo`/
  `brew`/`apt`, never touch worktree layout, never leave dirty
  edits, Edit/Write paths inside worktree, the `.claude/commands/`
  Edit gate workaround.
- **Edit:** All seven roles → 3-line pointer plus role-specific
  exceptions.
- **Fix:** the broken `(see CRITICAL section above)` anchor in
  all six affected role files (§3.4).
- **Resolves:** §1.14, §2.2, §3.4.
- **Diff size:** +~60 lines baseline, -~210 lines across seven
  roles. Net -150.

### T-CLEANUP-G: Invert Engine API removal rule citation

- **Edit:** `CLAUDE-BASELINE.md:282-290` → expand to the full
  rule (the version currently in the role docs) and drop the
  "See ... role docs ... for the full guidance" sentence.
- **Edit:** `role-opus-worker.md:86-99` and
  `role-opus-architect.md:53-66` → 2-line pointer to
  CLAUDE-BASELINE.md.
- **Resolves:** §1.21, §2.1.
- **Diff size:** +~15 lines baseline, -~28 lines roles. Net
  -13 lines.

### T-CLEANUP-H: Shrink intro boilerplate (Bash rules, cache, repo-slug discovery)

- **Edit:** All seven role files — reduce `## Bash tool rules`,
  `## Shared fleet state cache`, and the startup-step repo-slug
  discovery procedure to single-line pointers.
- **Edit:** `CLAUDE-BASELINE.md` gains a one-sentence "When
  writing PR review/merger body files, keep them inside the
  worktree" addendum so the `.review-body.md` / `.merger-body.md`
  warning has a canonical home.
- **Resolves:** §1.18, §1.19, §1.20, §2.3, §2.5.
- **Diff size:** -~100 lines across seven roles, +~10 lines
  baseline. Net -90.

### T-CLEANUP-I: Resolve `fleet:awaiting-base` vs `fleet:awaiting-upstream-review` label drift

- **Action:** Decide whether the two labels describe distinct
  states (base merged vs upstream reviewed) or are unintentional
  drift.
- **If distinct:** add `fleet:awaiting-base` to FLEET.md's label
  dictionary (`FLEET.md:446-589`) as a merger-owned label,
  documenting the state and owner.
- **If drift:** rename merger usage (`role-merger.md:362, 437,
  447, 478, 819-825`) to `fleet:awaiting-upstream-review`.
- **Resolves:** §3.2.
- **Diff size:** ~6-12 line edits depending on direction.

### T-CLEANUP-J: Adopt `fleet-pr-clear-feedback-labels` wrapper across roles

- **Edit:** `role-sonnet-author.md:261` and
  `role-opus-architect.md:185-186` — swap the raw chained
  `gh pr edit --remove-label A --remove-label B ...` form for
  `fleet-pr-clear-feedback-labels <N>`.
- **Edit:** Remove the unconditional `fleet:human-deferred`
  removal in the sonnet-author AMEND path.
- **Resolves:** §3.1, §4.5, §4.8.
- **Diff size:** ~10 line edits.

### T-CLEANUP-K: Catch up architect doc on transient-loop, AMEND, and game-repo wrinkle

- **Edit:** `role-opus-architect.md:230-231` — replace
  "20-minute loop" language with "transient, scout-triggered".
- **Edit:** `role-opus-architect.md:189-193` — adopt the
  `fleet:approved`-clearing-on-`human:needs-fix` flow.
- **Edit:** `role-opus-architect.md` — add a brief
  `--repo game` namespace guidance paragraph (or explicitly say
  the architect does not autonomously handle game tasks).
- **Resolves:** §3.3, §3.5, §4.6.
- **Diff size:** ~30 line edits.

### T-CLEANUP-L: Collapse redundant `--repo <engine-repo>` flags in merger

- **Edit:** `role-merger.md` — remove the inferable `--repo
  <engine-repo>` flag from `gh pr edit` / `gh pr comment` /
  `gh pr list` invocations throughout (~30 sites). Keep the
  value alive only where it's actually needed (repos.json Read
  step).
- **Resolves:** §3.7.
- **Diff size:** ~30 line edits, no semantic change.

### T-CLEANUP-M: Move PR-number examples out of role docs into a postmortem doc

- **Edit:** `role-opus-reviewer.md:421-422` and
  `role-sonnet-reviewer.md:463-464` — drop the specific PR
  numbers from the doc body. Either move them to a new
  `docs/agents/lessons-learned.md` (with dates) or drop them
  entirely now that the failure-mode prose stands alone.
- **Resolves:** §4.3.
- **Diff size:** ~6 line edits.

### T-CLEANUP-N: Verify and document `merger.log` rotation

- **Action:** Verify whether `~/.fleet/logs/merger.log` is
  rotated by the dispatcher (off-tree) or whether the role doc
  is stale on this point.
- **Edit:** `role-merger.md:579-580, 803, 851-853` — either
  update wording to cite the rotation source, or drop the
  "tail-rotated" claim if no rotation exists.
- **Resolves:** §4.4.
- **Diff size:** ~5 line edits + investigation.

### T-CLEANUP-O: Decide whether queue-manager produces feedback; document either way

- **Action:** Decide whether the queue-manager appends to
  `~/.fleet/feedback/queue-manager.md` like the other transient
  roles.
- **Edit:** `role-queue-manager.md` — either add a
  `## End-of-iteration feedback` section or add a one-line
  explicit statement that the role does not produce feedback.
- **Resolves:** §4.7.
- **Diff size:** ~10 lines.

---

## Summary

**Total identified duplications:** 22 blocks (§1.1–§1.22), ranging
from 5 to ~230 lines each.
**Dump violations:** 8 (§2.1–§2.8). Two are restatement-alongside-
pointer cases (§2.3, §2.5), one is an inverted citation (§2.1), the
others are baseline-text-without-pointer.
**Cross-role drift:** 7 items (§3.1–§3.7). The highest-priority
correctness issue is §3.1 (`fleet-pr-clear-feedback-labels` adopted
only by opus-worker, with documented past breakage on PR #637).
**Stale content:** 8 items (§4.1–§4.8).

The largest wins are T-CLEANUP-A, T-CLEANUP-C, and T-CLEANUP-D,
which together remove ~1,100 lines from the two authoring roles by
hoisting their three biggest operational protocols (feedback
handling, molecule/stacking, runtime ceremonies) into new shared
docs. The highest-priority single fix is T-CLEANUP-J (adopt the
existing wrapper across the remaining roles — known bug).

---

## Follow-up issues filed

| Task | Issue | Title |
|---|---|---|
| T-CLEANUP-A | #861 | hoist feedback-label handling into FLEET-FEEDBACK-HANDLING.md |
| T-CLEANUP-B | #862 | create REVIEWER-PROTOCOL.md and dedupe reviewers |
| T-CLEANUP-C | #863 | hoist molecule + stacked-PR per-task sequence into FLEET.md |
| T-CLEANUP-D | #864 | create FLEET-RUNTIME.md |
| T-CLEANUP-E | #865 | create FLEET-CROSS-HOST-SMOKE.md |
| T-CLEANUP-F | #866 | hoist Hard rules into CLAUDE-BASELINE.md + fix broken CRITICAL anchor |
| T-CLEANUP-G | #867 | invert Engine API removal rule citation |
| T-CLEANUP-H | #868 | shrink intro boilerplate (Bash rules, cache, repo-slug discovery) |
| T-CLEANUP-I | #869 | resolve `fleet:awaiting-base` vs `fleet:awaiting-upstream-review` label drift |
| T-CLEANUP-J | #870 | adopt `fleet-pr-clear-feedback-labels` wrapper in sonnet-author + architect |
| T-CLEANUP-K | #871 | catch up architect doc on transient-loop, AMEND, game-repo wrinkle |
| T-CLEANUP-L | #872 | collapse redundant `--repo` flags in role-merger.md |
| T-CLEANUP-M | #873 | move PR-number examples out of reviewer role docs |
| T-CLEANUP-N | #874 | verify and document `merger.log` rotation |
| T-CLEANUP-O | #875 | decide whether queue-manager produces feedback; document either way |

---

## Method appendix

Two general-purpose subagents read the role files in two groups:

- Author + worker + architect (`role-opus-worker.md` 1,260,
  `role-sonnet-author.md` 911, `role-opus-architect.md` 376).
- Reviewer + merger + queue (`role-merger.md` 857,
  `role-opus-reviewer.md` 426, `role-sonnet-reviewer.md` 467,
  `role-queue-manager.md` 209).

Each subagent also skimmed `FLEET.md`, `FLEET-CACHE.md`,
`CLAUDE-BASELINE.md`, `BUILD.md` to flag dump violations. Targeted
greps verified the highest-impact stale findings (`fleet:awaiting-
base` absence in FLEET.md, the `CRITICAL section above` anchor in
six files, the `fleet-pr-clear-feedback-labels` wrapper existing
in `scripts/fleet/` but adopted in only one role file, the inverted
Engine API removal rule citation in `CLAUDE-BASELINE.md`). Subagent
findings were cross-checked against the working-tree state on
branch `claude/T-221-roles-audit` before being incorporated.

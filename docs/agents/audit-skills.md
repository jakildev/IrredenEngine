# audit-skills — shared protocols + point-don't-dump

**Scope.** All 16 `.claude/skills/*/SKILL.md` files (5,219 lines total),
plus 6 `procedures/` subfiles under `commit-and-push/` and `review-pr/`.
**Baselines compared against.** `docs/agents/CLAUDE-BASELINE.md` (371
lines) and `docs/agents/FLEET.md`. The expectation is that any rule
defined in those two docs is referenced by name from a SKILL.md, not
restated. **Sister audit.** A parallel audit of `.claude/commands/role-*.md`
is queued as T-221 (issue #800); when it lands, it will live next to
this file under `docs/agents/`.

**Method.** Three subagents read 4 / 5 / 7 files each in full,
extracting baseline restatements, slop, internal dedup candidates, and
cross-skill drift. Cross-cutting greps confirmed pattern frequencies.
File:line citations are anchored to the working-tree state on branch
`claude/T-222-skills-audit` at the time this note was written; symbol-
plus-line citations (per `CLAUDE-BASELINE.md` §"Citing source in filed
artifacts") would be more durable but each finding spans a contiguous
block, so line ranges are adequate here.

**Headline.** The 16 SKILL.md files restate baseline rules far more
than they reference them: only one file (`simplify/SKILL.md`) cites
`CLAUDE-BASELINE.md` by name. The ECS-footgun rule, the naming table,
the IRMath substitution policy, and the cross-repo isolation procedure
each appear in 3-5 separate SKILL.md files in full or near-full form
— each copy is a future drift point. There are two cross-skill drifts
that are already visible in the diff (the `--auto-screenshot` symbol
names; the model-version stamps), plus one outright cross-repo leak
(`midi-scene-creator` and `create-creation` both name the private game
repo as the canonical reference home).

---

## 1. Duplicated >=5-line blocks

The blocks below appear in two or more SKILL.md files at >=5 lines of
near-verbatim or paraphrased content. Each row names the canonical
shared home and the proposed cleanup PR is enumerated in §5.

### 1.1 ECS-invariants checklist (per-entity getComponent + structural-change + allocation in tick)

| File | Range | Form |
|------|-------|------|
| `simplify/SKILL.md` | `62-105` | Full list, paragraph + sub-bullets |
| `review-pr/SKILL.md` | `200-222` | Full list, same order |
| `optimize/SKILL.md` | `194-200` | Subset — getComponent rule + alternatives |
| `polish-checkpoint/SKILL.md` | `88-94` | Subset — getComponent + naming-slip blurb |
| `ecs-prefab-creator/SKILL.md` | `131-133` | getComponent rule + alternatives |

The canonical source is `CLAUDE-BASELINE.md:16-31`, with the long-form
tick-signature story in `engine/system/CLAUDE.md`. The
component-method-tier-c rule (different surface, same baseline) lives
in `engine/prefabs/CLAUDE.md` and is also restated in
`simplify/SKILL.md:93-96` and `review-pr/SKILL.md`.

**Recommendation.** Each skill above should keep one line — "applies
the ECS-invariants checklist from `CLAUDE-BASELINE.md` §ECS and
`engine/system/CLAUDE.md`" — and drop the rest. If a skill needs a
machine-readable check pattern (`simplify`, `review-pr`), move that to
a new `.claude/rules/cpp-ecs-smells.md` and let both skills load it
from a single source. The `.claude/rules/cpp-systems.md` referenced
from `simplify/SKILL.md:119-122` is the precedent.

### 1.2 Naming-convention tables

| File | Range | Form |
|------|-------|------|
| `simplify/SKILL.md` | `308-321` | Full table, links to baseline AND restates it |
| `ecs-prefab-creator/SKILL.md` | `22-23`, `200-206` | Bullet form + checklist |
| `render-trixel-pipeline/SKILL.md` | `106-114` | Shader-prefix subset, full table form |
| `backend-parity/SKILL.md` | `121-132` | GLSL/MSL pair + prefix convention |
| `review-pr/SKILL.md` | `283-289` | Bullet form |

Canonical home: `CLAUDE-BASELINE.md:36-47`. Only `simplify` links to
it (and still restates the table). **Recommendation.** Single
sentence per skill — "Follow the naming table in `CLAUDE-BASELINE.md`
§Naming" — and delete the tables. The simplify skill's regex-style
fix patterns (`m_ on public`, `MinimapDetail` instead of plain
`detail`) are the actionable part; keep those and drop the table.

### 1.3 IRMath substitution rule

| File | Range | Form |
|------|-------|------|
| `simplify/SKILL.md` | `124-160` | Full substitution table (glm::*, std::* → IRMath::*) |
| `backend-parity/SKILL.md` | `218-222` | Pointer + paraphrase |
| `optimize/SKILL.md` | `211-213` | Subset (compute dispatch) |
| `review-pr/SKILL.md` | `268-273` | Iso-coord-mixing bullets |
| `lua-creation-setup/SKILL.md` | `294` | One-line callback (correct form) |

Canonical home: `CLAUDE-BASELINE.md:92-107`. **Recommendation.** Move
the IRMath substitution table from `simplify/SKILL.md:144-154` into
`.claude/rules/cpp-math.md` (already referenced as the math-rule home
at `simplify/SKILL.md:119`). Every skill should be a one-liner +
pointer.

### 1.4 Cross-repo information-isolation procedure

| File | Range | Form |
|------|-------|------|
| `commit-and-push/SKILL.md` | `118-156` | Full check, with code (~38 lines) |
| `commit-and-push/SKILL.md` | `462-464` | Anti-pattern restatement |
| `CLAUDE-BASELINE.md` | `230-290` | Canonical home |

The baseline at line 273 says "`commit-and-push` checks for this" —
but the skill restates the whole policy rather than checking against
the baseline list. **Recommendation.** Keep the check step short ("scan
the diff for the tokens listed in `CLAUDE-BASELINE.md` §Cross-repo
information isolation; warn if any matched") and let the baseline own
the token list. This also lets the policy evolve without editing
`commit-and-push/SKILL.md` separately.

### 1.5 Cursor-stack-base mechanic

| File | Range | Form |
|------|-------|------|
| `start-next-task/SKILL.md` | `34-39`, `159-186`, `270-291` | Three sub-sections |
| `FLEET.md` | `140-186` | Canonical home |
| `commit-and-push/procedures/cursor-stack.md` | (whole file) | Procedure-level detail |

Four locations, one mechanism. **Recommendation.** `FLEET.md` is the
canonical home; `start-next-task/SKILL.md` should hold flow only and
defer to `FLEET.md` for the concept. The
`commit-and-push/procedures/cursor-stack.md` subfile is correctly
scoped — it documents the PR-creation deltas, not the mechanism.

### 1.6 `fleet-build` / `fleet-run` invocation snippets

| File | Range | Variant |
|------|-------|---------|
| `simplify/SKILL.md` | `573-576` | `fleet-build --target format-changed`; `fleet-build --target <touched-target>` |
| `polish-checkpoint/SKILL.md` | `107`, `122` | `fleet-build --target format`; `fleet-build --target <touched-target>` |
| `attach-screenshots/SKILL.md` | `183-185`, `230-231` | `fleet-build --target <demo>`; `fleet-run --timeout 30 <demo> --auto-screenshot 10` |
| `optimize/SKILL.md` | `120-122` | `fleet-build --target <executable>`; `fleet-run --timeout 15 <executable>` |
| `render-debug-loop/SKILL.md` | `66`, `89` | `fleet-build --target <TARGET>`; `fleet-run <EXE> --auto-screenshot 10` |
| `render-verify/SKILL.md` | `102-104` | `fleet-build --target IRShapeDebug`; `fleet-run --timeout 60 IRShapeDebug --auto-screenshot 10` |
| `backend-parity/SKILL.md` | `253-269` | `cmake --build build --target IRShapeDebug -j$(nproc)` (raw, violates wrapper rule) |

Six skills issue the same wrapper with five different timeouts (none,
15, 30, 60) and one of them uses raw `cmake --build … -j$(nproc)` ([
`backend-parity/SKILL.md:254`, `260`]) which the engine-root
`CLAUDE.md:54-56` explicitly forbids. The `$(nproc)` substitution also
trips the Bash-tool gate documented in `CLAUDE-BASELINE.md:180-219`.
**Recommendation.** One shared snippet in `docs/agents/BUILD.md` ("use
`fleet-build --target <X>`; pick a timeout that matches the demo's
init time"). Per-skill text states only the timeout choice and why.

### 1.7 Host / preset table

| File | Range |
|------|-------|
| `render-verify/SKILL.md` | `42-46` |
| `render-debug-loop/SKILL.md` | `26-29` |
| `backend-parity/SKILL.md` | `253-269` (variant) |

Canonical home is `docs/agents/BUILD.md`, already pointed at from the
top-level `CLAUDE.md`. **Recommendation.** Two-line reference rather
than the table.

### 1.8 INPUT → UPDATE → RENDER pipeline ordering

| File | Range |
|------|-------|
| `ecs-prefab-creator/SKILL.md` | `189-196` |
| `create-creation/SKILL.md` | `172-181` |
| `midi-scene-creator/SKILL.md` | `80-91` (implicit in pipeline-register snippet) |

Canonical home: `engine/system/CLAUDE.md` (or a one-paragraph block in
`engine/CLAUDE.md`). **Recommendation.** Single line + link.

### 1.9 PR-body HEREDOC templates

`commit-and-push/SKILL.md` contains three near-identical PR-body
HEREDOC templates at `264-285` (single-PR), `307-335` (stacked,
fleet), and `349-374` (stacked, cursor). Each is ~25 lines and they
differ only in 1-2 fields. **Recommendation.** One canonical template
+ a per-mode delta section. Total reduction ~50 lines.

### 1.10 Trigger-discipline ("When to invoke" + "Do not auto-invoke")

Identical pattern in `polish-checkpoint/SKILL.md:40-49` and
`request-re-review/SKILL.md:17-29`: bullet list of trigger phrases
followed by "Do not auto-invoke." Also present in essentially every
skill's front-matter `description:`. **Recommendation.** Pull a small
"skill-author guide" sub-section into `docs/agents/` describing the
trigger-discipline convention; remove the "When to invoke" body
sections that just re-list what the front-matter already includes.

### 1.11 `fleet:authored-on-*` host-stamp logic

`commit-and-push/SKILL.md:386-423` carries the author-side stamping
logic (`uname -s` → `fleet:authored-on-{linux,macos}`) in 37 lines of
shell. The reviewer side at `review-pr/SKILL.md:478-484` defers to
`procedures/cross-host-smoke.md`. **Recommendation.** Lift
`commit-and-push`'s 37-line block into a new
`commit-and-push/procedures/host-label.md` (matching the existing
`procedures/` pattern). Saves ~30 lines from the main file and shares
ownership of the convention with the reviewer side.

---

## 2. Baseline-restatement violations (point-don't-dump)

The same finding as §1, framed by which baseline section each restated
block belongs to. Every row below restates a `CLAUDE-BASELINE.md` or
`FLEET.md` section in a SKILL.md instead of pointing.

| Baseline § | Restated in | Verdict |
|---|---|---|
| `CLAUDE-BASELINE.md:16-31` (ECS footgun) | simplify, review-pr, optimize, polish-checkpoint, ecs-prefab-creator | 5 copies |
| `CLAUDE-BASELINE.md:36-47` (Naming table) | simplify, ecs-prefab-creator, render-trixel-pipeline, backend-parity, review-pr | 5 copies (1 with link, still restates) |
| `CLAUDE-BASELINE.md:58-79` (Style — early return, unique_ptr, no validation of impossible states) | simplify (`427-444`), review-pr (`224-233`) | 2 copies |
| `CLAUDE-BASELINE.md:72-79` (Component-method tier-c) | simplify (`93-96`), review-pr (component-method tier-c) | 2 paraphrases |
| `CLAUDE-BASELINE.md:92-107` (IRMath rule) | simplify, backend-parity, optimize, review-pr | 4 copies; only lua-creation-setup uses the right one-line form |
| `CLAUDE-BASELINE.md:113-137` (What belongs in CLAUDE.md — "no inventories") | render-trixel-pipeline (`128-211`, multiple tables), render-debug-loop (`175-189`), backend-parity (`99-132`), optimize (`142-158`) | 4 inventory-table violations; rule applies to SKILL.md by the same logic |
| `CLAUDE-BASELINE.md:141-176` (Cite by symbol, not by line) | simplify (`189-200` lists `engine/prefabs/.../system_*.hpp:NN-NN`), render-debug-loop (`:122-125` forward-refs PR #433) | 2 violations |
| `CLAUDE-BASELINE.md:180-219` (Bash tool rules) | polish-checkpoint (`74` uses `&&`), backend-parity (`158-160` uses `<(…)` process substitution and `260` uses `$(nproc)`) | 3 violations in skill-prescribed snippets |
| `CLAUDE-BASELINE.md:228-292` (Cross-repo info isolation) | commit-and-push (`118-156`), and active **leak** in midi-scene-creator (`164-169`) and create-creation (`186`) | 1 restatement + 2 leaks |
| `FLEET.md` workflow rules (no master push, no force, label state machine, model split) | commit-and-push (`32-40`, `449-464`), backend-parity (`52-59`, `401-416`), start-next-task (`262-268`, `281-291`), request-re-review (`60-65`, `117-119`) | 4 multi-place restatements |

The two **cross-repo leaks** at `midi-scene-creator/SKILL.md:164-169`
and `create-creation/SKILL.md:186` are the most urgent items in this
table — both name the private game repo by its public-repo-relative
role ("the canonical full-Lua MIDI example now lives in the game
repo"), violating `CLAUDE-BASELINE.md:228-292`. The fix is to drop
the cross-repo reference and point only at `creations/demos/default/`,
or to remove the section entirely.

---

## 3. Cross-skill drift

### 3.1 `--auto-screenshot` contract symbol names

`render-debug-loop/SKILL.md:44-49` and `render-verify/SKILL.md:62-64`
describe the same `--auto-screenshot` contract with **different
symbol names**.

- `render-debug-loop` lists `IRVideo::parseAutoScreenshotArgv`,
  `IRVideo::AutoScreenshotShot`, `IRVideo::createAutoScreenshotSystem`.
- `render-verify` lists `ShotConfig`, `g_shots[]`, the
  `AutoScreenshot` system.

These are not aliases — one set is the engine-side helper and the other
is the demo's local config struct. One of the two docs is stale; based
on which symbols actually resolve in `engine/video/`, the
`render-verify` form looks older (likely pre-extraction). **Action.**
Verify against `engine/video/` and update whichever doc is wrong. Move
the contract description to a single source under
`engine/video/CLAUDE.md` (or wherever the helper lives) and have both
skills link.

### 3.2 `backend-parity` → `start-next-task` chaining

`backend-parity/SKILL.md:331-335` instructs the agent to "chain to
`start-next-task` and pick the next gap" after finishing a parity PR.
`start-next-task/SKILL.md:46-70` explicitly says "Do **not** invoke
proactively." Either backend-parity needs to drop the chaining
instruction or start-next-task's contract needs a documented exemption.

### 3.3 `simplify` runs format; `commit-and-push` doesn't re-run it

`simplify/SKILL.md:573-587` says the skill runs the formatter via
`fleet-build --target format-changed`. `commit-and-push/SKILL.md`'s
pre-commit phase (`94-185`) invokes simplify but doesn't mention a
subsequent format step, while `polish-checkpoint/SKILL.md:107` runs
`fleet-build --target format` after simplify. The three skills
disagree on whether the formatter is simplify's responsibility or the
caller's.

### 3.4 Model-version drift in stamped artifacts

- `review-pr/SKILL.md:393` hard-codes `🤖 Reviewed by Claude Opus 4.6
  (review-pr skill)`.
- `commit-and-push/SKILL.md:199` uses bare
  `Co-Authored-By: Claude <noreply@anthropic.com>` (no model version).
- The harness system prompt visible to agents uses
  `Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>` (and today
  is 2026-05-17 — Opus 4.6 is no longer the canonical model).

Three artifacts, three different conventions. **Action.** Either drop
the model-version stamp entirely from skill-emitted artifacts (let the
harness add it) or define one canonical signature constant in a
shared doc.

### 3.5 `request-re-review` partially reimplements `commit-and-push` and `start-next-task`

`request-re-review/SKILL.md:53-69` re-implements the staging-discipline
subset of `commit-and-push`; `request-re-review/SKILL.md:87-99`
re-implements the "release the branch" subset of `start-next-task`.
**Action.** Compose those skills rather than restating. The
trigger-discipline section is unique to request-re-review and should
be the only thing the file owns.

### 3.6 `attach-screenshots` / `render-debug-loop` overlap

Both skills capture screenshots from `--auto-screenshot`-capable
demos but neither cross-references the other in the body.
`attach-screenshots/SKILL.md` produces the PR-body record;
`render-debug-loop/SKILL.md` produces a diagnostic loop. The capture
mechanism is the same; the surface area should be a single shared
helper that both compose with.

---

## 4. Slop / dead content

### 4.1 Description restated as opening paragraph

In >=9 of 16 files, the body opens with a paragraph that paraphrases
the front-matter `description:`:

- `simplify/SKILL.md:18-23`, `25-38`
- `review-pr/SKILL.md:23-31`
- `commit-and-push/SKILL.md:14-19`, `22-28`
- `polish-checkpoint/SKILL.md:18-23`, `26-35`, `50-55`
- `optimize/SKILL.md:19-37`, `42-68`
- `backend-parity/SKILL.md:15-22`, `37-50`
- `start-next-task/SKILL.md:17-23`, `46-70`
- `attach-screenshots/SKILL.md:15-23`, `25-39`
- `render-debug-loop/SKILL.md:13-17`
- `render-verify/SKILL.md:13-18`
- `request-re-review/SKILL.md:11-13`, `17-29`
- `lua-creation-setup/SKILL.md:13-25`

The front-matter `description:` already carries the trigger phrases
and motivation. Every "When to invoke" body section that repeats
those phrases is cut-on-re-read filler. **Recommendation.** Standard
on a one-sentence body intro followed by the Flow section.

### 4.2 Hardcoded version-or-environment references that will rot

- `review-pr/SKILL.md:393` — `Claude Opus 4.6` (stale model version).
- `review-pr/SKILL.md:295-297` — `cmake --build build --target
  format-check` (the rest of the repo uses `fleet-build --target
  format-changed`).
- `review-pr/SKILL.md:36` — `docs/AGENT_FLEET_SETUP.md` (likely dead;
  canonical is `docs/agents/FLEET.md` — grep confirms no such file
  exists in the tree).
- `backend-parity/SKILL.md:265-269` — Windows-native PATH-fix command
  containing a personal username path (`C:/Users/evinj/...`).
- `simplify/SKILL.md:155-160` — hedge "IRMath::kPi / kHalfPi / kTwoPi
  in particular may not be merged yet — verify before suggesting."
  Either they exist now or the constant names need updating.
- `simplify/SKILL.md:189-200` — hardcoded "Live deviations already on
  the list" with absolute file paths and line ranges; per
  `CLAUDE-BASELINE.md:141-176`, line citations rot fast.
- `lua-creation-setup/SKILL.md:255-257` — `T-106 invariant` citation;
  task IDs go stale.
- `render-debug-loop/SKILL.md:122-125` — forward-ref to PR #433 for
  the capture procedure (rotted-cite candidate).

### 4.3 Dead placeholders and removed-thing references

- `midi-scene-creator/SKILL.md:166` — "The engine-side `midi_polyrhythm`
  demo has been removed from this repo" — concurrent cross-repo leak
  (§2). Drop the reference.
- `simplify/SKILL.md:202-207` — "Already covered by section 6, left
  here only as a cross-reference." Dead placeholder; drop the
  sub-section.
- `lua-creation-setup/SKILL.md:96-102` — `// ... more entries ...`
  ellipsis inside a code example (placeholder slop).
- `request-re-review/SKILL.md` — file is missing the `name:` field in
  its YAML front-matter (15 of 16 SKILL.md files have it; this one
  doesn't). Verified via grep.

### 4.4 Anti-patterns sections that restate flow steps

`backend-parity/SKILL.md` Anti-patterns, `start-next-task/SKILL.md`
Anti-patterns (`352-387`, 9 bullets), `attach-screenshots/SKILL.md`
Anti-patterns, `polish-checkpoint/SKILL.md` Anti-patterns + What This
Does NOT Do (overlap), `commit-and-push/SKILL.md` Anti-patterns
(`448-464`), `review-pr/SKILL.md` Anti-patterns (`495-505`). In each,
2-3 entries duplicate flow-step requirements documented earlier in
the file. The remaining 1-2 entries per skill carry real value
(non-obvious "don't do this"); the rest are obvious-on-re-read filler.

### 4.5 Heavy worked-examples that should move to `procedures/` subfiles

- `commit-and-push/SKILL.md:264-374` — three PR-body HEREDOC templates
  (~110 lines).
- `simplify/SKILL.md:209-307` — 98-line serialization version-bump
  rule with detection heuristics, false-positive guards, output
  templates, and a sub-extension. Belongs in
  `engine/asset/CLAUDE.md` or `.claude/rules/cpp-asset-versioning.md`.
- `simplify/SKILL.md:620-645` — 25-line hypothetical session example.
- `lua-creation-setup/SKILL.md:78-167`, `350-367` — 70+ lines of full
  C++ entry-point and Lua binding examples; better as a reference
  creation cited from the skill.
- `create-creation/SKILL.md:78-147` — 70-line C++ entry-point template
  duplicating `creations/template/main.cpp`.
- `attach-screenshots/SKILL.md:259-283` — markdown snippet template
  with `<details>` blocks; better as a procedures/ template file.

The `commit-and-push/procedures/{fleet-stack,cursor-stack,rebase-guard}.md`
and `review-pr/procedures/{cross-host-smoke,re-review,stacked-pr-review}.md`
subfile pattern is already established — extend it to the heavy-payload
skills above.

### 4.6 Decorative emoji bullets

Every Anti-patterns / What This Does NOT Do section uses `❌` bullets.
The codebase convention (engine `CLAUDE.md`, baseline) avoids emoji.
This is decorative slop; switch to bare list bullets.

---

## 5. Proposed follow-up cleanup tasks

Each item below is sized for one PR. The "Touches" column names the
files in scope so a downstream task picker can de-conflict against
other PRs touching the same files. Most are one-skill or one-block
edits; the larger refactors (§5.1-5.3) need their shared home created
first.

| # | Task | Touches | Size |
|---|------|---------|------|
| 5.1 | Extract ECS-invariants checklist into `.claude/rules/cpp-ecs-smells.md`; replace inlined copies with a one-line link in simplify, review-pr, optimize, polish-checkpoint, ecs-prefab-creator | 6 files, ~80 lines net cut | M |
| 5.2 | Move the IRMath substitution table from `simplify/SKILL.md:144-154` into `.claude/rules/cpp-math.md` (referenced as the math-rule home at `simplify/SKILL.md:119`); update review-pr, backend-parity, optimize, lua-creation-setup to one-line refs | 5 files | S |
| 5.3 | Replace naming-table copies in simplify, ecs-prefab-creator, render-trixel-pipeline, backend-parity, review-pr with one-line refs to `CLAUDE-BASELINE.md` §Naming | 5 files | S |
| 5.4 | Cross-repo info-isolation: shrink `commit-and-push/SKILL.md:118-156` to a check step that loads the leakage-token list from `CLAUDE-BASELINE.md` | 1 file | S |
| 5.5 | Fix two **active cross-repo leaks**: `midi-scene-creator/SKILL.md:164-169` and `create-creation/SKILL.md:186`. Drop the "game repo" reference; point only at `creations/demos/default/` (or remove the section) | 2 files | XS |
| 5.6 | `fleet-build` / `fleet-run` snippet consolidation: replace six skill-local copies (simplify, polish-checkpoint, attach-screenshots, optimize, render-debug-loop, render-verify) with refs to one canonical block in `docs/agents/BUILD.md` documenting timeout choice. Replace `backend-parity/SKILL.md:253-269` raw `cmake --build` with `fleet-build` (also fixes `$(nproc)` Bash-rule violation) | 7 files | M |
| 5.7 | Host/preset table consolidation: remove duplicate tables in `render-verify/SKILL.md:42-46` and `render-debug-loop/SKILL.md:26-29`; link to `docs/agents/BUILD.md` | 2 files | XS |
| 5.8 | Cursor-stack mechanic: shrink the 3 sections in `start-next-task/SKILL.md` (`34-39`, `159-186`, `270-291`) to flow-only; defer mechanism to `FLEET.md:140-186` | 1 file | S |
| 5.9 | **Drift fix:** unify `--auto-screenshot` contract symbol names between `render-debug-loop/SKILL.md:44-49` and `render-verify/SKILL.md:62-64`. Verify against `engine/video/` and move the canonical contract to `engine/video/CLAUDE.md`; both skills link | 2 files + 1 new doc section | M |
| 5.10 | **Drift fix:** model-version stamps in skill-emitted artifacts. Either drop entirely (let the harness add) or define one canonical signature constant. `review-pr/SKILL.md:393` and `commit-and-push/SKILL.md:199` are the two stamp sites | 2 files | S |
| 5.11 | **Drift fix:** decide who runs the formatter — `simplify`, `commit-and-push`, or `polish-checkpoint`. Pick one and remove the others' format step. Touches `simplify/SKILL.md:573-587`, `polish-checkpoint/SKILL.md:107`, `commit-and-push/SKILL.md` pre-commit | 3 files | S |
| 5.12 | **Drift fix:** `backend-parity/SKILL.md:331-335` instructs proactive `start-next-task` chaining; `start-next-task/SKILL.md:46-70` forbids proactive invocation. Pick one and align | 2 files | XS |
| 5.13 | Lift heavy payload from `commit-and-push/SKILL.md` PR-body HEREDOC templates (`264-374`, ~110 lines) into a single canonical template + per-mode delta in `commit-and-push/procedures/pr-body.md` | 1 file + 1 new procedure file | M |
| 5.14 | Lift the `commit-and-push/SKILL.md:386-423` host-stamp logic (37 lines) into `commit-and-push/procedures/host-label.md` | 1 file + 1 new procedure file | S |
| 5.15 | Lift the `simplify/SKILL.md:209-307` serialization version-bump rule (98 lines) into `engine/asset/CLAUDE.md` or `.claude/rules/cpp-asset-versioning.md`. simplify retains a one-line "applies the version-bump check from …" | 1 file + 1 new doc | M |
| 5.16 | **Rewrite** `render-trixel-pipeline/SKILL.md`: it's the structural outlier (no When-to-invoke / Anti-patterns / Recovery sections, heavy inventory tables of struct fields and binding numbers and enum values). Reduce to 30-50 lines of "concepts and gotchas" per `CLAUDE-BASELINE.md:113-137`; inventories move to `engine/render/CLAUDE.md` or stay in the code for Grep discovery | 1 file (big rewrite) | L |
| 5.17 | Trim inventory-table content from `render-debug-loop/SKILL.md:175-189` (Key Files), `backend-parity/SKILL.md:99-118` (backend C++ file pairs), `backend-parity/SKILL.md:121-132` (shader-prefix table), and `optimize/SKILL.md:142-158` (15-stage GPU pass-name list) | 4 files | M |
| 5.18 | Standardize SKILL.md structure: every SKILL.md should have front-matter, a one-sentence intro, Flow, and (optional) Anti-patterns / Recovery. Drop "Why this exists" sections (`simplify`, `polish-checkpoint`, `optimize`). Drop "When to invoke" sections that just re-list front-matter trigger phrases (most files; see §4.1) | 9 files (one-pass trim) | M |
| 5.19 | Drop emoji bullets (`❌`) from every Anti-patterns and What-This-Does-NOT-Do section. Bare list bullets are the codebase convention | 13 files | XS |
| 5.20 | Fix missing `name:` field in `request-re-review/SKILL.md` YAML front-matter (15 of 16 files have it; this one doesn't) | 1 file | XS |
| 5.21 | Compose `request-re-review/SKILL.md` against `commit-and-push` (for staging) and `start-next-task` (for branch release) instead of restating those phases. Reduce the file to the trigger-discipline + label-swap + "invoke commit-and-push then start-next-task" instructions | 1 file | S |
| 5.22 | Sweep stale tooling/version refs flagged in §4.2: `review-pr/SKILL.md:393` (Opus 4.6), `review-pr/SKILL.md:295-297` (`cmake --build --target format-check`), `review-pr/SKILL.md:36` (`docs/AGENT_FLEET_SETUP.md`), `backend-parity/SKILL.md:265-269` (personal Windows PATH), `simplify/SKILL.md:155-160` (`IRMath::kPi` hedge), `simplify/SKILL.md:189-200` (file:line "Live deviations"), `lua-creation-setup/SKILL.md:255-257` (`T-106` cite) | 4 files | S |
| 5.23 | Sweep Bash-rule violations in skill-prescribed snippets: `polish-checkpoint/SKILL.md:74` (`&&`), `backend-parity/SKILL.md:158-160` (`<(…)` process substitution) | 2 files | XS |
| 5.24 | Trim Anti-patterns sections that restate flow steps in `backend-parity`, `start-next-task` (9 bullets at `352-387`), `attach-screenshots`, `polish-checkpoint` (Anti-patterns vs What-This-Does-NOT-Do overlap), `commit-and-push` (`448-464`), `review-pr` (`495-505`). Keep one or two non-obvious anti-patterns per skill | 6 files | S |
| 5.25 | Pull the pipeline-ordering claim ("INPUT → UPDATE → RENDER") out of `ecs-prefab-creator/SKILL.md:189-196`, `create-creation/SKILL.md:172-181`, and `midi-scene-creator/SKILL.md:80-91`; defer to `engine/system/CLAUDE.md` | 3 files | XS |

**Total proposed.** 25 cleanup tasks. Each can ship as its own PR.
The fastest impact is §5.5 (cross-repo leaks), §5.9 / §5.10 / §5.11 /
§5.12 (drift fixes), and §5.20 (missing `name:` field) — all small,
all high-signal.

---

## 6. Method appendix

- Subagent split: 4 largest files (simplify, review-pr,
  commit-and-push, lua-creation-setup), 5 medium files (backend-parity,
  start-next-task, attach-screenshots, optimize,
  render-trixel-pipeline), 7 smaller files (render-verify,
  polish-checkpoint, ecs-prefab-creator, render-debug-loop,
  create-creation, midi-scene-creator, request-re-review).
- Each subagent read its assigned files in full plus
  `CLAUDE-BASELINE.md` and `FLEET.md` as reference baselines, then
  reported A) baseline-restatements, B) slop/dead content, C) internal
  dedup candidates, D) cross-skill drift, E) compose-with relationships,
  F) other surprises.
- Cross-cutting greps confirmed pattern frequencies: `getComponent`
  appears in 6 SKILL.md files; `fleet-build` in 6; the naming-rule
  fragments (`m_`, `trailing-underscore`, `C_`) in 4; `CLAUDE-BASELINE`
  in only 1 (`simplify`).
- All file:line citations are anchored to the working-tree state on
  branch `claude/T-222-skills-audit` at the time this note was written.

---

*Filed for T-222 (issue #800's sister docs audit; engine TASKS.md
entry T-222, issue #801). Follow-up issues will be filed without
labels, one per cleanup proposal in §5, so the queue-manager can
triage.*

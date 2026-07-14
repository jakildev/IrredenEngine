---
name: attach-screenshots
description: >-
  Capture before/after screenshots for an engine rendering PR and commit
  them to `docs/pr-screenshots/<branch>/` so the PR body can embed them
  via raw GitHub URLs. Runs an auto-screenshot-capable demo (default
  `IRShapeDebug`) once against origin/master and once against the dirty
  working tree — or, in `--two-ref` mode, between two committed refs for the
  feedback-AMEND path — pairs the outputs by shot label, and prints a markdown
  snippet the worker pastes into the PR body. Invoke when a PR touches
  `engine/render/`, `engine/prefabs/irreden/render/`, any `.glsl`/`.metal`
  shader, or `creations/demos/*/src/`, so reviewers can see the visual
  delta without running the executable themselves.
---

# attach-screenshots

## Trigger conditions (diff-based)

Invoke when `git diff --name-only origin/master...HEAD` or the dirty
tree touches any of:

- `engine/render/**`
- `engine/prefabs/irreden/render/**`
- `engine/render/src/shaders/**` (`.glsl`, `.metal`, `.hpp` in shaders dir)
- `creations/demos/*/src/**` and `creations/demos/*/main*.cpp`
- Any file matching `*.glsl` or `*.metal` anywhere in the tree

Skip when:

- The diff is purely `CLAUDE.md` / `README.md` / `docs/**`.
- The diff only touches `engine/system/`, `engine/entity/`, `engine/world/`
  without a render-pipeline effect (e.g. a new component with no visual
  output yet).
- Mechanical refactors with provably no runtime effect (rename,
  extract-header, move-to-detail-namespace).

When uncertain, ask the worker. False positives cost two builds; false
negatives cost reviewer clarity — prefer asking.

## Preconditions

1. **A capturable delta** — *either* a **dirty working tree** reflecting the
   PR's code change (default mode: the "before" capture stashes it to roll
   back to `origin/master`), *or* two distinct **committed refs** passed to
   `--two-ref` (see "Two-ref mode" below — the feedback-AMEND path, where the
   change is already committed on a clean detached HEAD). In default mode a
   clean tree means stop — there is no "before vs after" to capture.
2. **Build host with a usable display.** WSLg (Windows 11), native
   Linux with X/Wayland, or native macOS all work. Headless hosts
   cannot capture GLFW screenshots — the skill reports and exits.
3. **Active build preset is already configured.** `fleet-build` relies
   on `<worktree>/build/` existing from a prior `cmake --preset` run.
   Do not reconfigure from inside the skill.
4. **The chosen demo supports `--auto-screenshot`.** The skill greps
   the demo's main for the flag before running; if absent, it reports
   and exits cleanly rather than hanging on an interactive run.
5. **`git` and filesystem are writable** in the repo — the skill
   stashes, runs the demo, moves PNGs into `docs/pr-screenshots/`, and
   `git add`s them.

## Flow

### 1. Resolve the branch name

```bash
git rev-parse --abbrev-ref HEAD
```

Record this as `BRANCH`. It becomes the directory name under
`docs/pr-screenshots/`. Slash-containing names (e.g.
`claude/render-occupancy-grid`) are kept as nested directories —
GitHub raw URLs handle them fine.

Refuse to run on `master` or `main` — there is no "before vs after"
against the same ref.

### 2. Pick the demo target

Read `git status --porcelain` and the dirty diff to pick a demo. The
selection heuristic, in order:

1. **Diff touches `creations/demos/<name>/`** → use `IR<NameCamelCase>`
   (e.g. `shape_debug/` → `IRShapeDebug`).
2. **Diff touches the sun-shadow / lighting / AO shader family**
   (`ir_sun_*`, `c_bake_sun_shadow_map`, `c_clear_sun_shadow_map`,
   `c_compute_sun_shadow`, `ir_sun_shadow_sample`, `c_compute_voxel_ao`,
   `c_lighting_to_trixel`, `c_*light_volume*`) → use `IRCanvasStress` and
   run **both** passes with
   `--debug-overlay shadow|ao|light_level --no-auto-rotate --no-spin`
   (pick the overlay matching the effect; the freeze flags keep the pose
   comparable). These effects do not render in `shape_debug`, so its
   before/after shots come out byte-identical and read as "no visual
   change" (#2343).
3. **Diff touches only other engine render code** (`engine/render/**`,
   `engine/prefabs/irreden/render/**`, shaders) → default to
   `IRShapeDebug`, which exercises the trixel pipeline most broadly
   and ships `--auto-screenshot`.
4. **Ambiguous** (multiple demo directories touched, or a mix of
   render and non-render paths) → prompt the worker for the demo
   name. Do not guess in ambiguous cases; a wrong demo produces
   useless screenshots.

Step 3 greps the chosen demo for `--auto-screenshot` support. A demo
without the flag falls through to the report-and-exit branch there —
no need to hardcode a "which demos support it" list here, which would
go stale as more demos adopt the flag.

### 3. Verify `--auto-screenshot` support

Grep the demo's entry point:

Use the Grep tool with pattern `--auto-screenshot` against
`creations/demos/<demo-dir>/`. If zero matches, report:

```
attach-screenshots: <demo-name> does not implement --auto-screenshot.
                    add auto-screenshot support to its main.cpp before
                    invoking this skill.
```

and exit. Do **not** try to capture manually — the skill's contract
is automated paired shots.

### 4. Note the output directory

The destination is `docs/pr-screenshots/<BRANCH>/`. **Do not `mkdir`
it here** — `git stash push -u` in step 5 stashes the empty
untracked directory, and the subsequent `git checkout --detach`
drops it. Steps 5 and 6 each `mkdir -p` lazily immediately before
moving PNGs in, so the dir exists at the only points it's needed.

Pre-existing files in `docs/pr-screenshots/<BRANCH>/` from a prior
invocation are left in place — the deterministic `<label>-before.png`
/ `<label>-after.png` filenames (step 8) overwrite their own outputs
on re-run.

### 5. Capture the "before" pass (origin/master state)

Read the demo's shot labels from its `g_shots[]` array (see
`creations/demos/<demo-dir>/main.cpp`) and record them in order as
`LABELS[0..N-1]`. Reading live rather than hardcoding means adding a
new shot to the demo is a one-sided change.

Stash the dirty tree and move HEAD to `origin/master`:

```bash
git fetch origin master
git stash push -u -m "attach-screenshots:<BRANCH>"
```

If the stash reports "No local changes to save", the tree was
already clean — stop and report that there is no delta to capture.

The `-m "attach-screenshots:<BRANCH>"` message is our **race-safe
handle** for this stash. `refs/stash` is shared across every worktree
on this clone (see [CLAUDE-BASELINE.md § Hard rules](../../../docs/agents/CLAUDE-BASELINE.md#hard-rules-for-autonomous-fleet-roles)),
so a parallel agent's `git stash push` in another worktree can shift
`stash@{0}` out from under us between now and the restore. Every
restore below re-resolves *our* entry by this branch-unique message
(`<BRANCH>` differs per worktree) and reapplies it **by commit SHA** —
never by `stash@{0}` / bare `git stash pop`, which would silently
apply or consume another worktree's entry.

```bash
git checkout --detach origin/master
```

The stash is preserved across the detach. Any build artifacts in
`build/` stay in place, which is fine — `fleet-build` rebuilds only
what changed.

Clear the demo's prior screenshots so the counter starts at
`screenshot_000001.png`. The harness's bash classifier blocks
`rm -rf` even on paths strictly inside the worktree, so rotate the
directory aside instead — `mv` is not gated, and the build dir is
throwaway:

```bash
if [ -d build/creations/demos/<demo-dir>/save_files/screenshots ]; then
    mv build/creations/demos/<demo-dir>/save_files/screenshots \
       "build/creations/demos/<demo-dir>/save_files/screenshots.prev.$(date +%s)"
fi
```

(The save path is `<exe-cwd>/save_files/screenshots/`; `fleet-run`
cd's into the exe's directory before launching, so `save_files/`
lands next to the binary under `build/`. The rotated `.prev.*`
sibling is harmless — it persists in `build/` (gitignored) until a
clean build; it does not appear in `git status`.)

Build and run:

```bash
fleet-build --target <demo-name>
fleet-run <demo-name> --auto-screenshot 10
```

`fleet-run` reports `exited cleanly after Ns` on normal completion.
Do not add `--timeout` — auto-screenshot fires `closeWindow()` when the
shot sequence is done; a timeout would mask hangs (see [BUILD.md §Timeout choices](../../../docs/agents/BUILD.md#timeout-choices)).

If `fleet-build` or `fleet-run` fails, **restore** before
propagating the failure. Check the branch back out, then re-resolve
*our* stash by its branch-unique message and reapply it by SHA:

```bash
git checkout <BRANCH>
git stash list --format='%gd %H %gs' | grep "attach-screenshots:<BRANCH>"
```

That returns exactly one line; note its `stash@{N}` index and `<SHA>`.
Reapply by **SHA** (immutable — race-proof even if a parallel agent
shifted the index), then drop that entry by its `stash@{N}` index
(`git stash drop` requires the `stash@{N}` form, not a raw SHA):

```bash
git stash apply <SHA>
git stash drop stash@{N}
```

Never use bare `git stash pop` / `git stash drop` here — both default
to `stash@{0}`, which may be another worktree's entry. If the index
shifted between the two commands above, re-run `git stash list` and
take the index of the line matching `<SHA>`.

Then report and exit. Do **not** stage any partial output.

On success, create the output directory (it was never created in
step 4, see the rationale there) and move the captured PNGs into
it, renaming each by its shot label with a `-before` suffix:

```bash
mkdir -p docs/pr-screenshots/<BRANCH>/
```

```
build/creations/demos/<demo-dir>/save_files/screenshots/screenshot_000001.png
  → docs/pr-screenshots/<BRANCH>/<LABELS[0]>-before.png
... (one per shot)
```

If the PNG count differs from the label count, something crashed
mid-sequence — report and exit without staging.

Restore the branch state. As in the build-failure restore above,
re-resolve *our* stash by its branch-unique message and reapply by
SHA — never bare `git stash pop`:

```bash
git checkout <BRANCH>
git stash list --format='%gd %H %gs' | grep "attach-screenshots:<BRANCH>"
```

That returns the single matching line; take its `stash@{N}` index
and `<SHA>`, then:

```bash
git stash apply <SHA>
git stash drop stash@{N}
```

### 6. Capture the "after" pass (dirty working tree)

With the stash restored and `<BRANCH>` checked out, build and run
again. Step 5 already moved the before-pass PNGs out of
`save_files/screenshots/`, so the counter resets on its own — no
second `rm` needed:

```bash
fleet-build --target <demo-name>
fleet-run <demo-name> --auto-screenshot 10
```

`mkdir -p docs/pr-screenshots/<BRANCH>/` as a safety call (the
before pass created it on success; the explicit mkdir here is
cheap insurance against a subsequent invocation where the before
pass exited before reaching its move step). Then move the PNGs
to the output directory with `-after` suffixes, paired by label
with the before-pass outputs.

Mismatched shot counts (before ≠ after) indicate the demo's shot
list changed between refs or one run crashed mid-sequence. Report
and exit without staging.

### 7. Stage the screenshots

```bash
git add docs/pr-screenshots/<BRANCH>/
```

**Do not commit.** Screenshots ship as part of the feature commit
that `commit-and-push` creates next. A separate "add screenshots"
commit would muddle the PR history.

The worker's subsequent `commit-and-push` picks up the staged PNGs
via its normal "stage specific paths" flow. If the worker skips
`commit-and-push` and commits manually, they must remember to
include `docs/pr-screenshots/<BRANCH>/` in the staged set — the
skill's stdout confirmation makes this hard to miss.

### 8. Emit the markdown snippet

Print a block the worker pastes into the PR body. Format:

```markdown
## Screenshots

<details>
<summary>zoom1_origin</summary>

| Before | After |
|--------|-------|
| ![](https://raw.githubusercontent.com/jakildev/IrredenEngine/<BRANCH>/docs/pr-screenshots/<BRANCH>/zoom1_origin-before.png) | ![](https://raw.githubusercontent.com/jakildev/IrredenEngine/<BRANCH>/docs/pr-screenshots/<BRANCH>/zoom1_origin-after.png) |

</details>

<details>
<summary>zoom2_origin</summary>

| Before | After |
|--------|-------|
| ![](.../zoom2_origin-before.png) | ![](.../zoom2_origin-after.png) |

</details>

... (one `<details>` block per shot label)
```

`<details>` collapses each shot so a 6-shot PR is scannable, and the
table makes side-by-side visual diffing one glance.

### 9. Report

Print a compact summary:

```
attach-screenshots: <demo-name> (<N> shots)
  before: docs/pr-screenshots/<BRANCH>/<label>-before.png × N
  after:  docs/pr-screenshots/<BRANCH>/<label>-after.png × N
  staged: <path>/ (<2N+> files)
  markdown snippet printed above — paste into PR body
```

## Two-ref mode (feedback-AMEND)

The default flow assumes the PR change is **uncommitted** (the authoring flow:
dirty tree = "after", stashed-away `origin/master` = "before"). The
**feedback-AMEND** path is different — the change is already committed and the
worker is on a clean **detached HEAD** (`fleet-pr-claim-feedback` /
`fleet-pr-checkout-detached`), so there is no dirty tree to stash and the
default flow exits at Precondition #1 / step 5. The "attach a screenshot pair"
nit is a common render-PR nit *and* render PRs routinely finish via
feedback-AMEND, so this path is first-class.

Invoke explicitly:

```bash
attach-screenshots --two-ref [<before-ref>] [<after-ref>]
```

`<before-ref>` defaults to `origin/master`; `<after-ref>` defaults to the
current `HEAD` (the detached PR-head commit). The mode runs the same
build → run → pair → stage → snippet flow as steps 1–9, with these deltas:

- **Capture refs up front, no stash.** Record `RETURN=$(git rev-parse HEAD)`
  (the detached PR-head, to land back on) and resolve `AFTER` (`<after-ref>`,
  default `$RETURN`) **before** any checkout. The tree is clean, so the whole
  `git stash` dance — and the shared-`refs/stash` cross-worktree race it
  guards against — is **skipped**.
- **Step 1 (dir name).** On a detached HEAD `git rev-parse --abbrev-ref HEAD`
  returns `HEAD`. Resolve the `docs/pr-screenshots/<DIR>/` name from the PR's
  head ref instead (the feedback context records it) — never write under a
  `HEAD/` directory.
- **Step 5 (before).** `git checkout --detach <before-ref>` instead of
  stash+detach; build, run, move PNGs to `<label>-before.png`.
- **Step 6 (after).** `git checkout --detach "$AFTER"` instead of restoring a
  stash; build, run, move PNGs to `<label>-after.png`.
- **Restore.** `git checkout --detach "$RETURN"` to return to the PR-head the
  feedback flow left you on. No `stash apply` / `drop`.
- **Step 7 (stage).** PNGs are new untracked files — `git add` them as usual.
  In feedback-AMEND the worker folds them into the amend commit (the AMEND
  flow), not a fresh `commit-and-push`.

If `<before-ref>` and `<after-ref>` resolve to the same commit, stop — there
is no delta to capture.

### Camera-yaw fixes need a non-cardinal shot

The default `g_shots` cardinal sequence is **byte-identical before/after for
yaw-only render fixes** — the delta only shows at non-cardinal camera yaw
(e.g. 45°/30°). For a PR in the camera-yaw family, capture at a non-cardinal
yaw (a demo yaw flag / yaw-sweep shot); a cardinal-only suite understates the
change and reads as "no visual delta" (this compounded #1953: 8 of 10 base
shots were identical — only the 45°/30° shots showed the fix).

## Failure modes

Handle each cleanly — no partial commits, no orphan PNGs, no left-
over stash:

| Failure                                | Response                                                                                               |
|----------------------------------------|--------------------------------------------------------------------------------------------------------|
| Clean working tree (default mode)      | Stop with `nothing to capture — tree is clean`. No stash, no capture. (In `--two-ref` mode a clean tree is expected — the delta is the two refs.) |
| On `master`/`main`                     | Refuse to run.                                                                                         |
| Chosen demo lacks `--auto-screenshot`  | Report the gap; exit without capturing.                                                                |
| `fleet-build` fails in either pass     | Restore stash if mid-before-pass, report the build error, exit. No PNGs staged.                        |
| `fleet-run` crashes / times out        | Restore stash if mid-before-pass, report the crash, exit. No PNGs staged.                              |
| Headless host (no display)             | Detect via `fleet-run`'s exit code + empty `save_files/screenshots/`. Report; recommend running on a host with a display (WSLg, native Linux desktop, macOS). |
| Shot count mismatch (before ≠ after)   | Report both counts; do not stage a half-paired set.                                                    |
| Stash apply conflicts on restore       | Do **not** force; report and ask the worker to resolve manually. We `apply` (then `drop` only on a clean apply), so the entry survives — recover it by its `<SHA>` from `git stash list`. |

## Anti-patterns

- Deleting prior `docs/pr-screenshots/<other-branch>/` directories
  "for tidiness". Each branch owns its own; historical branches are a
  visual changelog.
- Invoking this skill on PRs that don't touch visual code. Pure
  refactors and doc passes should skip it — the build and run cost
  is not worth no-op screenshots.

## Recovery

If the skill exits mid-flight (usage-limit error, interrupted, crash
during "before" pass):

1. Find *our* entry by its branch-unique message — **do not assume
   `stash@{0}` is ours**; `refs/stash` is shared across all worktrees
   on this clone, so a parallel agent's stash may sit on top:
   `git stash list --format='%gd %H %gs'` and pick the line ending
   `attach-screenshots:<BRANCH>`. Note its `stash@{N}` index and `<SHA>`.
2. If the worktree is detached (detached HEAD from step 5), check
   the branch back out: `git checkout <BRANCH>`.
3. Reapply by SHA, then drop that entry by its index (never bare
   `git stash pop` / `git stash drop` — both default to `stash@{0}`
   and may consume another worktree's entry):
   `git stash apply <SHA>` then `git stash drop stash@{N}`.
4. Verify with `git status` that the dirty tree matches what you had
   before the skill ran.
5. Re-invoke the skill once the underlying issue is resolved.

## Scope

What this skill does:

- Engine-demo support (default `IRShapeDebug`).
- Single-demo capture per invocation.
- Stash/run/restore flow against `origin/master` (default), or a
  checkout-based two-ref flow for the feedback-AMEND path (`--two-ref`).

What this skill does **not** do:

- Auto-invoke from worker roles or `commit-and-push` — wiring the
  trigger conditions above into the fleet roles is follow-up work.
- Pick game-creation run targets under `creations/game/` — the demo
  picker here only knows about `creations/demos/*/`.
- Pixel-level regression diffing. Reviewers eyeball the paired PNGs;
  for "show me where the drift is" pipe a pair through
  `tools/img_diff` (red-on-grey diff image) — and for aggregate
  pass/fail metrics use `scripts/render-compare.py`. Both are
  complementary read-only tools; this skill just produces the input
  PNGs.

## Example

User: "attach screenshots for this lighting PR"

The skill captures the 6-shot `IRShapeDebug` sequence at
`origin/master` and on the dirty tree, moves the PNGs into
`docs/pr-screenshots/<BRANCH>/` with `-before` / `-after` suffixes,
stages them, and prints the step 8 markdown snippet. The worker
pastes that snippet into the PR body when running `commit-and-push`;
the reviewer sees side-by-side before/after inline on the PR page.

---
name: attach-screenshots
description: >-
  Capture before/after screenshots for an engine rendering PR and commit
  them to `docs/pr-screenshots/<branch>/` so the PR body can embed them
  via raw GitHub URLs. Runs an auto-screenshot-capable demo (default
  `IRShapeDebug`) once against origin/master and once against the dirty
  working tree, pairs the outputs by shot label, and prints a markdown
  snippet the worker pastes into the PR body. Invoke when a PR touches
  `engine/render/`, `engine/prefabs/irreden/render/`, any `.glsl`/`.metal`
  shader, or `creations/demos/*/src/`, so reviewers can see the visual
  delta without running the executable themselves.
---

# attach-screenshots

Automated before/after screenshot capture for engine rendering PRs.
Reviewer agents can read a diff but cannot run the executable — so
visual changes (AO, shadows, shape SDFs, parity fixes) often ship with
no way to tell whether they *look right*. This skill closes that gap
by running the demo twice — once at `origin/master`, once on the
dirty tree — and committing paired PNGs to the branch.

## When to invoke

Trigger when:

- The user says "attach screenshots", "add screenshots to this PR",
  "capture before/after", "show me the visual diff".
- A worker role is about to run `commit-and-push` on a diff that
  touches visual code (render pipeline, shaders, render prefabs,
  render-heavy demos). See "Trigger conditions" below.
- The reviewer agent asks for a visual side-by-side and the worker
  is addressing the feedback.

Do **not** auto-invoke on pure doc passes, header-only refactors, or
non-render code. Running the skill is not free — two builds and two
timed demo runs. Reserve it for PRs where a reviewer genuinely needs
to see pixels.

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

1. **Dirty working tree** reflecting the PR's code change. The "before"
   capture stashes this to roll back to `origin/master`. If the tree is
   clean, stop — there is no "before vs after" to capture.
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
2. **Diff touches only engine render code** (`engine/render/**`,
   `engine/prefabs/irreden/render/**`, shaders) → default to
   `IRShapeDebug`, which exercises the trixel pipeline most broadly
   and ships `--auto-screenshot`.
3. **Ambiguous** (multiple demo directories touched, or a mix of
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

### 4. Prepare the output directory

```bash
mkdir -p docs/pr-screenshots/<BRANCH>/
```

Leave any pre-existing files in place. Repeated invocations on the
same branch overwrite their own outputs (deterministic filenames,
see step 8).

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

```bash
git checkout --detach origin/master
```

The stash is preserved across the detach. Any build artifacts in
`build/` stay in place, which is fine — `fleet-build` rebuilds only
what changed.

Clear the demo's prior screenshots so the counter starts at
`screenshot_000001.png`:

```bash
rm -rf build/creations/demos/<demo-dir>/save_files/screenshots
```

(The save path is `<exe-cwd>/save_files/screenshots/`; `fleet-run`
cd's into the exe's directory before launching, so `save_files/`
lands next to the binary under `build/`.)

Build and run:

```bash
fleet-build --target <demo-name>
fleet-run --timeout 30 <demo-name> --auto-screenshot 10
```

`IRShapeDebug` finishes its 6-shot sequence in ~3s; `--timeout 30`
gives a safety margin for slower hosts without leaving windows open
if something hangs. `fleet-run` reports `exited cleanly after Ns`
on normal completion.

If `fleet-build` or `fleet-run` fails, **restore** before
propagating the failure:

```bash
git checkout <BRANCH>
git stash pop
```

Then report and exit. Do **not** stage any partial output.

On success, move the captured PNGs into the output directory,
renaming each by its shot label with a `-before` suffix:

```
build/creations/demos/<demo-dir>/save_files/screenshots/screenshot_000001.png
  → docs/pr-screenshots/<BRANCH>/<LABELS[0]>-before.png
... (one per shot)
```

If the PNG count differs from the label count, something crashed
mid-sequence — report and exit without staging.

Restore the branch state:

```bash
git checkout <BRANCH>
git stash pop
```

### 6. Capture the "after" pass (dirty working tree)

With the stash restored and `<BRANCH>` checked out, build and run
again. Step 5 already moved the before-pass PNGs out of
`save_files/screenshots/`, so the counter resets on its own — no
second `rm` needed:

```bash
fleet-build --target <demo-name>
fleet-run --timeout 30 <demo-name> --auto-screenshot 10
```

Move the PNGs to the output directory with `-after` suffixes,
paired by label with the before-pass outputs.

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

## Failure modes

Handle each cleanly — no partial commits, no orphan PNGs, no left-
over stash:

| Failure                                | Response                                                                                               |
|----------------------------------------|--------------------------------------------------------------------------------------------------------|
| Clean working tree                     | Stop with `nothing to capture — tree is clean`. No stash, no capture.                                  |
| On `master`/`main`                     | Refuse to run.                                                                                         |
| Chosen demo lacks `--auto-screenshot`  | Report the gap; exit without capturing.                                                                |
| `fleet-build` fails in either pass     | Restore stash if mid-before-pass, report the build error, exit. No PNGs staged.                        |
| `fleet-run` crashes / times out        | Restore stash if mid-before-pass, report the crash, exit. No PNGs staged.                              |
| Headless host (no display)             | Detect via `fleet-run`'s exit code + empty `save_files/screenshots/`. Report; recommend running on a host with a display (WSLg, native Linux desktop, macOS). |
| Shot count mismatch (before ≠ after)   | Report both counts; do not stage a half-paired set.                                                    |
| Stash pop conflicts on restore         | Do **not** force-pop; report and ask the worker to resolve manually. The stash ref is preserved.       |

## Anti-patterns

- ❌ Committing screenshots in a separate commit. Stage and let
  `commit-and-push` bundle them into the feature commit.
- ❌ Running the capture from inside a worker role's tick function or
  hot path. This skill is explicit, worker-invoked, and costs one
  full rebuild + timed demo run per pass.
- ❌ Capturing from any demo other than one that implements
  `--auto-screenshot`. The "before" pass must be deterministic — a
  hand-driven demo is not.
- ❌ Deleting prior `docs/pr-screenshots/<other-branch>/` directories
  "for tidiness". Each branch owns its own; historical branches are a
  visual changelog.
- ❌ Invoking this skill on PRs that don't touch visual code. Pure
  refactors and doc passes should skip it — the build and run cost
  is not worth no-op screenshots.

## Recovery

If the skill exits mid-flight (usage-limit error, interrupted, crash
during "before" pass):

1. Check `git stash list` — the stash entry begins with
   `attach-screenshots:<BRANCH>`.
2. If the worktree is detached (detached HEAD from step 5), check
   the branch back out: `git checkout <BRANCH>`.
3. `git stash pop` to restore.
4. Verify with `git status` that the dirty tree matches what you had
   before the skill ran.
5. Re-invoke the skill once the underlying issue is resolved.

## Scope

What this skill does:

- Engine-demo support (default `IRShapeDebug`).
- Single-demo capture per invocation.
- Stash/run/restore flow against `origin/master`.

What this skill does **not** do:

- Auto-invoke from worker roles or `commit-and-push` — wiring the
  trigger conditions above into the fleet roles is follow-up work.
- Pick game-creation run targets under `creations/game/` — the demo
  picker here only knows about `creations/demos/*/`.
- Pixel-level regression diffing. Reviewers eyeball the paired PNGs;
  an automated diff-gate would be a separate skill.

## Example

User: "attach screenshots for this lighting PR"

The skill captures the 6-shot `IRShapeDebug` sequence at
`origin/master` and on the dirty tree, moves the PNGs into
`docs/pr-screenshots/<BRANCH>/` with `-before` / `-after` suffixes,
stages them, and prints the step 8 markdown snippet. The worker
pastes that snippet into the PR body when running `commit-and-push`;
the reviewer sees side-by-side before/after inline on the PR page.

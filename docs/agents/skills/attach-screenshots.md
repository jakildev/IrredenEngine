# attach-screenshots — shared flow

The canonical `attach-screenshots` flow: capture before/after screenshots for
a rendering PR and commit them under the **screenshot output root** so the
PR body can embed them via raw-URL links. Runs an auto-screenshot-capable
demo once against the **default branch** and once against the dirty working
tree — or, in `--two-ref` mode, between two committed refs for the
feedback-AMEND path — pairs the outputs by shot label, and prints a markdown
snippet the worker pastes into the PR body.

Every repo that runs a fleet keeps its `.claude/skills/attach-screenshots/
SKILL.md` as a thin wrapper that points here and supplies only its
**deltas** (below). See [`docs/design/skill-sharing.md`](../../design/skill-sharing.md)
for the mechanism. Wherever a step needs a repo-specific value it names a
**delta key** in bold.

---

## Repo deltas this flow needs

| Delta key | What it is | Engine value |
|---|---|---|
| **repo** | The `gh --repo` slug / raw-URL owner-repo path. | `jakildev/IrredenEngine` |
| **raw URL base** | The `raw.githubusercontent.com` prefix used in the emitted markdown. | `https://raw.githubusercontent.com/jakildev/IrredenEngine` |
| **default branch** | The ref the "before" pass captures against. | `master` |
| **default demo** | Fallback demo when the diff touches only engine render code. | `IRShapeDebug` |
| **visual-file globs** | Paths whose change triggers this skill. | `engine/render/**`, `engine/prefabs/irreden/render/**`, `engine/render/src/shaders/**` (`.glsl`, `.metal`, `.hpp` in shaders dir), `creations/demos/*/src/**`, `creations/demos/*/main*.cpp`, any `*.glsl` / `*.metal` anywhere |
| **screenshot output root** | Where paired PNGs land, nested by branch/dir name. | `docs/pr-screenshots` |
| **demo save path** | Where a demo's `--auto-screenshot` run writes PNGs, relative to the build dir. | `build/creations/demos/<demo-dir>/save_files/screenshots` |
| **sha-pin token** | The literal placeholder emitted in step 8's URLs in place of a deletable branch ref; a downstream consumer (the **PR-create flow**'s step 8, or a feedback-AMEND body edit) substitutes it with the real commit SHA once one exists. | `@COMMIT_SHA@` |
| **build tool** | The build wrapper. | `fleet-build --target <demo-name>` |
| **run tool** | The run wrapper. | `fleet-run <demo-name> --auto-screenshot 10` |

---

## Trigger conditions (diff-based)

Invoke when `git diff --name-only <default-branch>...HEAD` or the dirty tree
touches any of the **visual-file globs**.

Skip when:

- The diff is purely `CLAUDE.md` / `README.md` / `docs/**`.
- The diff only touches non-render modules without a render-pipeline effect
  (e.g. a new component with no visual output yet).
- Mechanical refactors with provably no runtime effect (rename,
  extract-header, move-to-detail-namespace).

When uncertain, ask the worker. False positives cost two builds; false
negatives cost reviewer clarity — prefer asking.

## Preconditions

1. **A capturable delta** — *either* a **dirty working tree** reflecting the
   PR's code change (default mode: the "before" capture stashes it to roll
   back to the **default branch**), *or* two distinct **committed refs**
   passed to `--two-ref` (see "Two-ref mode" below — the feedback-AMEND
   path, where the change is already committed on a clean detached HEAD).
   In default mode a clean tree means stop — there is no "before vs after"
   to capture.
2. **Build host with a usable display.** WSLg (Windows 11), native Linux
   with X/Wayland, or native macOS all work. Headless hosts cannot capture
   GLFW screenshots — the skill reports and exits.
3. **Active build preset is already configured.** The **build tool** relies
   on the build dir existing from a prior configure. Do not reconfigure
   from inside the skill.
4. **The chosen demo supports `--auto-screenshot`.** The skill greps the
   demo's main for the flag before running; if absent, it reports and exits
   cleanly rather than hanging on an interactive run.
5. **`git` and filesystem are writable** in the repo — the skill stashes,
   runs the demo, moves PNGs into the **screenshot output root**, and
   `git add`s them.

## Flow

### 1. Resolve the branch name

```bash
git rev-parse --abbrev-ref HEAD
```

Record this as `BRANCH`. It becomes the directory name under the
**screenshot output root**. Slash-containing names (e.g.
`claude/render-occupancy-grid`) are kept as nested directories — raw-URL
links handle them fine.

Refuse to run on the **default branch** — there is no "before vs after"
against the same ref.

### 2. Pick the demo target

Read `git status --porcelain` and the dirty diff to pick a demo. The
selection heuristic, in order:

1. **Diff touches `creations/demos/<name>/`** → use the matching
   `IR<NameCamelCase>` target.
2. **Diff touches an effect family the default demo does not render**
   (per the repo wrapper's effect-routing table — e.g. the engine's
   sun-shadow / lighting / AO shaders, which only produce a visible delta
   in the lighting-capable stress demo under a `--debug-overlay` mode at a
   frozen pose) → use the routed demo and flags for **both** passes. The
   default demo's before/after shots for such diffs are byte-identical and
   read as "no visual change", giving the reviewer zero evidence.
3. **Diff touches only other engine render code** (matching the
   render-scoped **visual-file globs**) → use the **default demo**, which
   exercises the trixel pipeline most broadly and ships
   `--auto-screenshot`.
4. **Ambiguous** (multiple demo directories touched, or a mix of render and
   non-render paths) → prompt the worker for the demo name. Do not guess in
   ambiguous cases; a wrong demo produces useless screenshots.

Step 3 greps the chosen demo for `--auto-screenshot` support. A demo
without the flag falls through to the report-and-exit branch there — no
need to hardcode a "which demos support it" list here, which would go
stale as more demos adopt the flag.

### 3. Verify `--auto-screenshot` support

Grep the demo's entry point for `--auto-screenshot` under
`creations/demos/<demo-dir>/`. If zero matches, report:

```
attach-screenshots: <demo-name> does not implement --auto-screenshot.
                    add auto-screenshot support to its main.cpp before
                    invoking this skill.
```

and exit. Do **not** try to capture manually — the skill's contract is
automated paired shots.

### 4. Note the output directory

The destination is `<screenshot output root>/<BRANCH>/`. **Do not `mkdir`
it here** — `git stash push -u` in step 5 stashes the empty untracked
directory, and the subsequent `git checkout --detach` drops it. Steps 5
and 6 each `mkdir -p` lazily immediately before moving PNGs in, so the dir
exists at the only points it's needed.

Pre-existing files in `<screenshot output root>/<BRANCH>/` from a prior
invocation are left in place — the deterministic `<label>-before.png` /
`<label>-after.png` filenames (step 8) overwrite their own outputs on
re-run.

### 5. Capture the "before" pass (default-branch state)

Read the demo's shot labels from its shot-list array (see the demo's
`main.cpp`) and record them in order as `LABELS[0..N-1]`. Reading live
rather than hardcoding means adding a new shot to the demo is a one-sided
change.

Stash the dirty tree and move HEAD to the **default branch**:

```bash
git fetch origin <default-branch>
git stash push -u -m "attach-screenshots:<BRANCH>"
```

If the stash reports "No local changes to save", the tree was already
clean — stop and report that there is no delta to capture.

The `-m "attach-screenshots:<BRANCH>"` message is our **race-safe handle**
for this stash. `refs/stash` is shared across every worktree on this
clone, so a parallel agent's `git stash push` in another worktree can
shift `stash@{0}` out from under us between now and the restore. Every
restore below re-resolves *our* entry by this branch-unique message
(`<BRANCH>` differs per worktree) and reapplies it **by commit SHA** —
never by `stash@{0}` / bare `git stash pop`, which would silently apply
or consume another worktree's entry.

```bash
git checkout --detach origin/<default-branch>
```

The stash is preserved across the detach. Any build artifacts in the
build dir stay in place, which is fine — the **build tool** rebuilds only
what changed.

Clear the demo's prior screenshots so the counter starts at
`screenshot_000001.png`. Rotate the directory aside rather than deleting
it (`rm -rf` may be blocked by harness policy; `mv` is not) — the build
dir is throwaway:

```bash
if [ -d <demo save path> ]; then
    mv <demo save path> "<demo save path>.prev.$(date +%s)"
fi
```

Build and run:

```bash
<build tool>
<run tool>
```

The **run tool** reports clean completion. Do not add a timeout —
auto-screenshot fires window-close when the shot sequence is done; a
timeout would mask hangs.

If the build or run fails, **restore** before propagating the failure.
Check the branch back out, then re-resolve *our* stash by its
branch-unique message and reapply it by SHA:

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

Never use bare `git stash pop` / `git stash drop` here — both default to
`stash@{0}`, which may be another worktree's entry. If the index shifted
between the two commands above, re-run `git stash list` and take the
index of the line matching `<SHA>`.

Then report and exit. Do **not** stage any partial output.

On success, create the output directory (it was never created in step 4,
see the rationale there) and move the captured PNGs into it, renaming
each by its shot label with a `-before` suffix:

```bash
mkdir -p <screenshot output root>/<BRANCH>/
```

```
<demo save path>/screenshot_000001.png
  → <screenshot output root>/<BRANCH>/<LABELS[0]>-before.png
... (one per shot)
```

If the PNG count differs from the label count, something crashed
mid-sequence — report and exit without staging.

Restore the branch state. As in the build-failure restore above,
re-resolve *our* stash by its branch-unique message and reapply by SHA —
never bare `git stash pop`:

```bash
git checkout <BRANCH>
git stash list --format='%gd %H %gs' | grep "attach-screenshots:<BRANCH>"
```

That returns the single matching line; take its `stash@{N}` index and
`<SHA>`, then:

```bash
git stash apply <SHA>
git stash drop stash@{N}
```

### 6. Capture the "after" pass (dirty working tree)

With the stash restored and `<BRANCH>` checked out, build and run again.
Step 5 already moved the before-pass PNGs out of the **demo save path**,
so the counter resets on its own — no second cleanup needed:

```bash
<build tool>
<run tool>
```

`mkdir -p <screenshot output root>/<BRANCH>/` as a safety call (the
before pass created it on success; the explicit mkdir here is cheap
insurance against a subsequent invocation where the before pass exited
before reaching its move step). Then move the PNGs to the output
directory with `-after` suffixes, paired by label with the before-pass
outputs.

Mismatched shot counts (before ≠ after) indicate the demo's shot list
changed between refs or one run crashed mid-sequence. Report and exit
without staging.

### 7. Stage the screenshots

```bash
git add <screenshot output root>/<BRANCH>/
```

**Do not commit.** Screenshots ship as part of the feature commit that
the **PR-create flow** creates next. A separate "add screenshots" commit
would muddle the PR history.

The worker's subsequent PR-create pass picks up the staged PNGs via its
normal "stage specific paths" flow. If the worker commits manually
instead, they must remember to include `<screenshot output root>/<BRANCH>/`
in the staged set — the skill's stdout confirmation makes this hard to
miss.

### 8. Emit the markdown snippet

Print a block the worker pastes into the PR body. Format:

```markdown
## Screenshots

<details>
<summary>zoom1_origin</summary>

| Before | After |
|--------|-------|
| ![](<raw URL base>/@COMMIT_SHA@/<screenshot output root>/<BRANCH>/zoom1_origin-before.png) | ![](<raw URL base>/@COMMIT_SHA@/<screenshot output root>/<BRANCH>/zoom1_origin-after.png) |

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

**The ref component of every URL is the literal `<sha-pin token>`, not
`<BRANCH>`.** At this point in the flow no commit exists yet that
contains the just-staged screenshots — the PR-create flow's commit step
runs after this one — so a branch-name ref would 404 once the branch is
deleted post-merge, and any earlier-resolved SHA would be stale. Emitting
the token here and deferring substitution to the consumer (the PR-create
flow's step that drafts the PR body, or a feedback-AMEND path's body edit)
is what makes the URL resolve correctly once the commit lands: only the
**path** segment (`<screenshot output root>/<BRANCH>/...`) uses the
branch/dir name — that segment is a committed path, not a ref, and stays
resolvable at any ref including the final SHA.

### 9. Report

Print a compact summary:

```
attach-screenshots: <demo-name> (<N> shots)
  before: <screenshot output root>/<BRANCH>/<label>-before.png × N
  after:  <screenshot output root>/<BRANCH>/<label>-after.png × N
  staged: <path>/ (<2N+> files)
  markdown snippet printed above — paste into PR body
```

## Two-ref mode (feedback-AMEND)

The default flow assumes the PR change is **uncommitted** (the authoring
flow: dirty tree = "after", stashed-away default-branch state = "before").
The **feedback-AMEND** path is different — the change is already committed
and the worker is on a clean **detached HEAD**, so there is no dirty tree
to stash and the default flow exits at Precondition #1 / step 5. The
"attach a screenshot pair" nit is a common render-PR nit *and* render PRs
routinely finish via feedback-AMEND, so this path is first-class.

Invoke explicitly:

```bash
attach-screenshots --two-ref [<before-ref>] [<after-ref>]
```

`<before-ref>` defaults to the **default branch**; `<after-ref>` defaults
to the current `HEAD` (the detached PR-head commit). The mode runs the
same build → run → pair → stage → snippet flow as steps 1–9, with these
deltas:

- **Capture refs up front, no stash.** Record `RETURN=$(git rev-parse HEAD)`
  (the detached PR-head, to land back on) and resolve `AFTER` (`<after-ref>`,
  default `$RETURN`) **before** any checkout. The tree is clean, so the
  whole `git stash` dance — and the shared-`refs/stash` cross-worktree race
  it guards against — is **skipped**.
- **Step 1 (dir name).** On a detached HEAD `git rev-parse --abbrev-ref HEAD`
  returns `HEAD`. Resolve the `<screenshot output root>/<DIR>/` name from
  the PR's head ref instead (the feedback context records it) — never
  write under a `HEAD/` directory.
- **Step 5 (before).** `git checkout --detach <before-ref>` instead of
  stash+detach; build, run, move PNGs to `<label>-before.png`.
- **Step 6 (after).** `git checkout --detach "$AFTER"` instead of
  restoring a stash; build, run, move PNGs to `<label>-after.png`.
- **Restore.** `git checkout --detach "$RETURN"` to return to the PR-head
  the feedback flow left you on. No `stash apply` / `drop`.
- **Step 7 (stage).** PNGs are new untracked files — `git add` them as
  usual. In feedback-AMEND the worker folds them into the amend commit,
  not a fresh commit-and-push pass.
- **Step 8 (markdown snippet).** The emitted URLs still use the
  **sha-pin token** in the ref position. The AMEND path's own body-edit
  step (`gh pr edit --body`) is the consumer that substitutes it — against
  the **post-amend** HEAD (`git rev-parse HEAD` after the amend commit
  lands and is pushed), not `$AFTER`, which is only the pre-amend "after"
  capture ref used for the screenshot delta.

If `<before-ref>` and `<after-ref>` resolve to the same commit, stop —
there is no delta to capture.

### Camera-yaw fixes need a non-cardinal shot

The default cardinal shot sequence is **byte-identical before/after for
yaw-only render fixes** — the delta only shows at non-cardinal camera yaw
(e.g. 45°/30°). For a PR in the camera-yaw family, capture at a
non-cardinal yaw (a demo yaw flag / yaw-sweep shot); a cardinal-only suite
understates the change and reads as "no visual delta".

## Failure modes

Handle each cleanly — no partial commits, no orphan PNGs, no leftover
stash:

| Failure                                | Response                                                                                               |
|----------------------------------------|--------------------------------------------------------------------------------------------------------|
| Clean working tree (default mode)      | Stop with `nothing to capture — tree is clean`. No stash, no capture. (In `--two-ref` mode a clean tree is expected — the delta is the two refs.) |
| On the **default branch**              | Refuse to run.                                                                                         |
| Chosen demo lacks `--auto-screenshot`  | Report the gap; exit without capturing.                                                                |
| Build fails in either pass             | Restore stash if mid-before-pass, report the build error, exit. No PNGs staged.                        |
| Run crashes / times out                | Restore stash if mid-before-pass, report the crash, exit. No PNGs staged.                              |
| Headless host (no display)             | Detect via the run tool's exit code + empty save path. Report; recommend running on a host with a display. |
| Shot count mismatch (before ≠ after)   | Report both counts; do not stage a half-paired set.                                                    |
| Stash apply conflicts on restore       | Do **not** force; report and ask the worker to resolve manually. We `apply` (then `drop` only on a clean apply), so the entry survives — recover it by its `<SHA>` from `git stash list`. |

## Anti-patterns

- Deleting prior `<screenshot output root>/<other-branch>/` directories
  "for tidiness". Each branch owns its own; historical branches are a
  visual changelog.
- Invoking this skill on PRs that don't touch visual code. Pure refactors
  and doc passes should skip it — the build and run cost is not worth
  no-op screenshots.

## Recovery

If the skill exits mid-flight (usage-limit error, interrupted, crash
during "before" pass):

1. Find *our* entry by its branch-unique message — **do not assume
   `stash@{0}` is ours**; `refs/stash` is shared across all worktrees on
   this clone, so a parallel agent's stash may sit on top:
   `git stash list --format='%gd %H %gs'` and pick the line ending
   `attach-screenshots:<BRANCH>`. Note its `stash@{N}` index and `<SHA>`.
2. If the worktree is detached (detached HEAD from step 5), check the
   branch back out: `git checkout <BRANCH>`.
3. Reapply by SHA, then drop that entry by its index (never bare
   `git stash pop` / `git stash drop` — both default to `stash@{0}` and
   may consume another worktree's entry):
   `git stash apply <SHA>` then `git stash drop stash@{N}`.
4. Verify with `git status` that the dirty tree matches what you had
   before the skill ran.
5. Re-invoke the skill once the underlying issue is resolved.

## Scope

What this skill does:

- Engine-demo support (**default demo**).
- Single-demo capture per invocation.
- Stash/run/restore flow against the **default branch**, or a
  checkout-based two-ref flow for the feedback-AMEND path (`--two-ref`).

What this skill does **not** do:

- Auto-invoke from worker roles or the PR-create flow — wiring the
  trigger conditions above into the fleet roles is follow-up work.
- Pick non-engine-demo run targets (e.g. a game creation) — the demo
  picker here only knows about the engine's own demo directories.
- Pixel-level regression diffing. Reviewers eyeball the paired PNGs; a
  repo may layer a separate diff tool (red-on-grey diff image) and an
  aggregate pass/fail comparator on top — both complementary read-only
  tools that consume this skill's output PNGs.

## Example

User: "attach screenshots for this lighting PR"

The skill captures the shot sequence at the **default branch** and on the
dirty tree, moves the PNGs into `<screenshot output root>/<BRANCH>/` with
`-before` / `-after` suffixes, stages them, and prints the step 8 markdown
snippet (URLs pinned to the **sha-pin token**). The worker pastes that
snippet into the PR body when running the PR-create flow, which substitutes
the token with the real commit SHA; the reviewer sees side-by-side
before/after inline on the PR page, and the images keep resolving after the
branch is deleted.

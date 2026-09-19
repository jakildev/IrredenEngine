# attach-screenshots — shared flow

Capture before/after screenshots for a rendering PR and stage them under
the **screenshot output root** so the PR body can embed them via raw-URL
links. Runs an auto-screenshot-capable demo once against the **default
branch** and once against the dirty working tree — or, in `--two-ref`
mode, between two committed refs — pairs the outputs by shot label, and
prints a markdown snippet for the PR body.

Each repo's `.claude/skills/attach-screenshots/SKILL.md` is a thin wrapper
that points here and answers the delta keys below
([`docs/design/skill-sharing.md`](../../design/skill-sharing.md)). A step
that needs a repo-specific value names its **delta key** in bold.

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
| **clip preset** | `--config-preset` path for the clip capture pass. | `scripts/fleet/clip-preset.lua` |
| **clip frames** | Default `--auto-record` frame count for the clip pass. | `180` |

---

## Trigger

Invoke when `git diff --name-only <default-branch>...HEAD` or the dirty
tree touches a **visual-file glob**. Skip for docs-only diffs, non-render
modules with no render-pipeline effect, and mechanical refactors with
provably no runtime effect. When uncertain, ask — two extra builds are
cheaper than a reviewer without evidence.

## Preconditions

- A capturable delta: a dirty working tree (default mode) or two distinct
  committed refs (`--two-ref`). A clean tree in default mode means stop.
- A host with a display (WSLg, native Linux X/Wayland, native macOS);
  headless hosts cannot capture GLFW screenshots — report and exit.
- The build preset is already configured; never reconfigure from inside
  the skill.
- The chosen demo implements `--auto-screenshot` (step 3 checks).

## Flow

### 1. Branch

`git rev-parse --abbrev-ref HEAD` → `BRANCH`, the directory name under the
**screenshot output root** (slashes become nested directories). Refuse to
run on the **default branch**.

### 2. Pick the demo

In order: a diff under `creations/demos/<name>/` → that demo's
`IR<NameCamelCase>` target; a diff in an effect family the default demo
does not render → the demo and flags the wrapper's effect-routing table
names, for **both** passes (otherwise the pair is byte-identical and reads
as "no visual change"); a diff in other engine render code → the
**default demo**; ambiguous (several demo directories, or render mixed
with non-render) → ask the worker.

### 3. Verify `--auto-screenshot`

Grep the demo's entry point under `creations/demos/<demo-dir>/`. No match →
report `attach-screenshots: <demo-name> does not implement
--auto-screenshot` and exit; never capture manually.

### 3b. Verify the clip gate

Grep the same entry point for the auto-record wire-up (`appendAutoRecordIfRequested`
or `createAutoRecordSystem`, same check as step 3) and check `ffmpeg` is on
`PATH`. Either missing sets `CLIP_ENABLED=0` and prints
`attach-screenshots: clip skipped (<reason>)` — never a hard failure, the PNG
flow (steps 4–9) runs regardless. Otherwise `CLIP_ENABLED=1`.

### 4. Output directory

`<screenshot output root>/<BRANCH>/`. Do not `mkdir` it yet — `git stash
push -u` in step 5 would stash the empty directory and the detach would
drop it. Steps 5 and 6 `mkdir -p` right before moving PNGs in. Prior
files there are left in place; the deterministic filenames overwrite
their own outputs.

### 5. "Before" pass (default-branch state)

Read the shot labels from the demo's shot-list array in its `main.cpp`
into `LABELS[0..N-1]`. Then:

```bash
git fetch origin <default-branch>
git stash push -u -m "attach-screenshots:<BRANCH>"
git checkout --detach origin/<default-branch>
```

"No local changes to save" → the tree was clean; stop. The branch-unique
stash message is the race-safe handle: `refs/stash` is shared across every
worktree on the clone, so every restore re-resolves *our* entry by message
and applies it by SHA — never `stash@{0}` or bare `git stash pop`.

Rotate the demo's prior screenshots aside so the counter restarts at
`screenshot_000001.png` (`mv` rather than `rm -rf`; the build dir is
throwaway):

```bash
if [ -d <demo save path> ]; then
    mv <demo save path> "<demo save path>.prev.$(date +%s)"
fi
```

Build and run — no timeout; auto-screenshot closes the window itself and a
timeout would mask hangs:

```bash
<build tool>
<run tool>
```

When `CLIP_ENABLED=1`, also capture a clip (raw captures are never
committed — only the composed clip step 7 produces is staged). Re-run the
step-3b wiring grep against this detached (before) ref first: a miss means
the before ref lacks `--auto-record` (the PR that adds the wiring itself, or
a demo the before ref does not have at all) and skips this capture — no
`-before.mp4`, not a failure; step 7 takes the single-arm form.

```bash
<run tool> --auto-record <clip frames> --config-preset <clip preset>
mv <demo save path>/../../capture.mp4 <demo save path>/../../<demo-name>-before.mp4
```

(`capture.mp4` lands beside the exe — two directories up from **demo save
path**, which nests `save_files/screenshots` under the exe dir.)

Restore is the same on failure and on success:

```bash
git checkout <BRANCH>
git stash list --format='%gd %H %gs' | grep "attach-screenshots:<BRANCH>"
git stash apply <SHA>          # by SHA — immune to index shifts
git stash drop stash@{N}       # drop needs the index form; re-list if it moved
```

On failure, restore, report, and exit without staging. On success,
`mkdir -p <screenshot output root>/<BRANCH>/` and move each
`screenshot_00000K.png` to `<LABELS[K-1]>-before.png`; a PNG count that
differs from the label count means a mid-sequence crash — report and exit
without staging. Then restore.

### 6. "After" pass (dirty tree)

With the stash restored on `<BRANCH>`, build and run again (the before
pass already moved its PNGs out, so the counter resets). `mkdir -p` the
output directory, move the PNGs in with `-after` suffixes paired by label.
Before ≠ after counts → the shot list changed between refs or a run
crashed; report and exit without staging. When `CLIP_ENABLED=1`, repeat the
clip capture the same way as step 5, moving `capture.mp4` to
`<demo-name>-after.mp4`.

### 7. Compose the clip, then stage — do not commit

When `CLIP_ENABLED=1`:

```bash
fleet-clip <demo-name>-before.mp4 <demo-name>-after.mp4 \
    <screenshot output root>/<BRANCH>/<demo-name>-clip
```

No before arm (new demo, or a capability the before ref lacks per step 5)
uses the single-arm form instead — a literal `-` in the before position:

```bash
fleet-clip - <demo-name>-after.mp4 \
    <screenshot output root>/<BRANCH>/<demo-name>-clip
```

`fleet-clip` creates `<screenshot output root>/<BRANCH>/` itself, so a
standalone compose needs no `mkdir -p` first. It exits 3 (ffmpeg missing) or
1 (a compose failure) without writing partial output — either way, log
`attach-screenshots: clip skipped (<reason>)` and continue with the PNG
flow; the raw `-before.mp4` / `-after.mp4` captures are never committed.

```bash
git add <screenshot output root>/<BRANCH>/
```

The screenshots (and, when composed, the clip pair) ship in the feature
commit the **PR-create flow** makes next.

### 8. Emit the markdown snippet

```markdown
## Screenshots

<details>
<summary>zoom1_origin</summary>

| Before | After |
|--------|-------|
| ![](<raw URL base>/@COMMIT_SHA@/<screenshot output root>/<BRANCH>/zoom1_origin-before.png) | ![](<raw URL base>/@COMMIT_SHA@/<screenshot output root>/<BRANCH>/zoom1_origin-after.png) |

</details>

Clip: ![](<raw URL base>/@COMMIT_SHA@/<screenshot output root>/<BRANCH>/<demo-name>-clip.gif)
[full clip (mp4)](<raw URL base>/@COMMIT_SHA@/<screenshot output root>/<BRANCH>/<demo-name>-clip.mp4)

... (one `<details>` block per shot label, each followed by the same Clip
line when `CLIP_ENABLED=1`; omit the Clip line entirely when it is 0)
```

The Clip line is unchanged (same `<demo-name>-clip.{gif,mp4}` names) whether
step 7 composed a paired or a single-pane clip.

The ref segment of every URL is the literal **sha-pin token**, never
`<BRANCH>`: no commit containing the screenshots exists yet, a branch ref
404s after the branch is deleted post-merge, and any earlier SHA is stale.
The consumer substitutes it once the commit lands. Only the path segment
uses the branch name — it is a committed path, resolvable at any ref.

### 9. Report

```
attach-screenshots: <demo-name> (<N> shots)
  before: <screenshot output root>/<BRANCH>/<label>-before.png × N
  after:  <screenshot output root>/<BRANCH>/<label>-after.png × N
  clip:   <screenshot output root>/<BRANCH>/<demo-name>-clip.{mp4,gif} (or "skipped (<reason>)")
  staged: <path>/ (<2N+> files)
  markdown snippet printed above — paste into PR body
```

## Two-ref mode (feedback-AMEND)

On the feedback-AMEND path the change is already committed and the worker
sits on a clean detached HEAD, so there is nothing to stash:

```bash
attach-screenshots --two-ref [<before-ref>] [<after-ref>]
```

`<before-ref>` defaults to the **default branch**, `<after-ref>` to `HEAD`.
Same flow as steps 1–9 with these deltas: record `RETURN=$(git rev-parse
HEAD)` and resolve `AFTER` before any checkout; derive the output
directory name from the PR's head ref (a detached HEAD reports `HEAD` —
never write under `HEAD/`); step 5 is `git checkout --detach
<before-ref>`, step 6 `git checkout --detach "$AFTER"`, restore is `git
checkout --detach "$RETURN"`; no stash apply/drop; the PNGs fold into the
amend commit; the AMEND path's own `gh pr edit --body` substitutes the
**sha-pin token** against the **post-amend** pushed HEAD, not `$AFTER`.
Identical before/after refs → stop. The clip gate (step 3b) and capture
(steps 5–7) run unchanged against `<before-ref>` / `$AFTER`.

### Camera-yaw fixes need a non-cardinal shot

The default cardinal shot sequence is byte-identical before/after for
yaw-only render fixes; the delta only shows at a non-cardinal yaw (45°/30°).
For a PR in the camera-yaw family, capture with a yaw flag or yaw-sweep
shot — a cardinal-only pair reads as "no visual delta".

## Failure modes

No partial commits, no orphan PNGs, no leftover stash:

| Failure | Response |
|---|---|
| Clean tree (default mode) | `nothing to capture — tree is clean`; no stash. |
| On the **default branch** | Refuse. |
| Demo lacks `--auto-screenshot` | Report; exit without capturing. |
| Build or run fails in either pass | Restore the stash if mid-before-pass; report; nothing staged. |
| Headless host | Run tool exits non-zero with an empty save path; report and recommend a host with a display. |
| Shot count mismatch | Report both counts; stage nothing. |
| Stash apply conflicts | Never force; the entry survives (`apply`, then `drop` only on a clean apply) — recover by SHA from `git stash list`. |
| Demo lacks auto-record wiring, or `ffmpeg` absent | `CLIP_ENABLED=0`; log and skip the clip only — the PNG flow is unaffected, never a hard failure. |
| `fleet-clip` exits non-zero | Log `attach-screenshots: clip skipped (<reason>)`; stage the PNGs, omit the clip pair and its snippet lines. |

## Recovery

After a mid-flight exit: `git stash list --format='%gd %H %gs'`, pick the
line ending `attach-screenshots:<BRANCH>` (never assume `stash@{0}`), `git
checkout <BRANCH>` if detached, `git stash apply <SHA>`, `git stash drop
stash@{N}`, confirm with `git status`, re-invoke.

## Scope

Single-demo capture per invocation against the repo's own demo
directories; no pixel diffing (reviewers eyeball the pair; a repo may layer
a diff tool and a pass/fail comparator on these PNGs). Never delete another
branch's directory under the **screenshot output root** — each branch owns
its own, and the history is a visual changelog. Not auto-invoked by roles
or the PR-create flow.

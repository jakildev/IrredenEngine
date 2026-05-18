# Cross-host smoke validation

OpenGL (Linux/Windows) and Metal (macOS) are two independent render backends.
A render-touching PR built only on one host can compile-fail, fail at
shader-link time, or render incorrectly on the other backend without any
local signal. The cross-host smoke protocol catches this before merge by
having a fleet agent on the *other* host check out the PR, build the smoke
target, run it, and verdict the result.

This doc is the single source of truth for the protocol. Both halves —
the **reviewer** tagging side and the **author** claiming side — live
here. Role files ([`role-opus-reviewer.md`](../../.claude/commands/role-opus-reviewer.md),
[`role-sonnet-reviewer.md`](../../.claude/commands/role-sonnet-reviewer.md),
[`role-opus-worker.md`](../../.claude/commands/role-opus-worker.md),
[`role-sonnet-author.md`](../../.claude/commands/role-sonnet-author.md))
point here rather than restating the procedure.

Engine repo only. Game-repo PRs do not get cross-host smoke labels —
backends live in the engine.

---

## Labels at a glance

| Label | Owner | Purpose |
|---|---|---|
| `fleet:authored-on-linux` / `fleet:authored-on-macos` | `commit-and-push` at PR creation | Records which host the author built + smoked on. Permanent fact, not a state. |
| `fleet:needs-linux-smoke` / `fleet:needs-macos-smoke` | reviewer (after verdict) | Requests a clean-checkout build + `IRShapeDebug` run from an agent on that host. Cleared by the smoking agent on success; flipped to `fleet:needs-fix` on failure. |
| `fleet:reviewing-<host>-<agent>` | `fleet-claim` script | Atomic lock around the smoke run (same lock the reviewer roles use). Prevents two same-host author agents from racing on the same PR. Released by `fleet-claim review-release`. |

A PR with an outstanding `fleet:needs-<host>-smoke` label is **not safe
to merge** — the human merger waits for the label to clear.

See [`FLEET.md`](FLEET.md) § "Issue/PR labeling discipline" for the
canonical label list. This doc only restates the smoke-relevant
subset.

---

## Reviewer side: tagging

After a reviewer (`role-opus-reviewer` or `role-sonnet-reviewer`) sets
a verdict label on an engine PR, they decide whether to add a smoke
label. The decision is purely path-based — no host-side execution
happens at tagging time.

### When to tag

Tag when **all** of the following hold:

1. The PR is in the engine repo.
2. The diff touches at least one render path:
   - `engine/render/` (any file)
   - `engine/prefabs/irreden/render/` (any file)
   - any `*.glsl` shader
   - any `*.metal` shader
   - any file under `engine/render/src/shaders/`

   Use `gh pr diff <N> --name-only` to read the changed paths.

3. The PR is not in a `needs-fix` / `blocker` / WIP state (smoke runs
   are wasted on PRs the reviewer is sending back).

Skip for:

- Game-repo PRs (no engine backends to validate)
- Non-render engine PRs (tooling, docs, non-render modules) — smoke
  labels narrow the "did this port build on the other backend"
  question, not as general CI
- PRs that already carry the smoke label from a prior pass

### What to tag

Subtract the author's host (`commit-and-push` stamped
`fleet:authored-on-<host>` at PR creation — the author already smoke-
tested their own host per the workflow):

- PR has `fleet:authored-on-linux` → add `fleet:needs-macos-smoke`
- PR has `fleet:authored-on-macos` → add `fleet:needs-linux-smoke`
- Neither label present (Windows-native author, or pre-fix PR) → add **both**

```
gh pr edit <N> --add-label "fleet:needs-<other-host>-smoke"
```

If `sonnet-reviewer` already added the labels on a first pass, the
later `opus-reviewer` recheck does nothing — the labels are
idempotent.

---

## Author side: claiming + running

Both `role-opus-worker` and `role-sonnet-author` poll for the smoke
label that matches their host and execute the protocol below. The
fleet runs at most one smoke run per author iteration so task pickup
isn't starved by back-to-back builds.

### Host detection

Derive the host key from `uname -s`:

- `Linux` → host key `linux`, poll `fleet:needs-linux-smoke`
- `Darwin` → host key `macos`, poll `fleet:needs-macos-smoke`

### Picking a PR

From the cached `repos.engine.prs[]`, pick PRs whose `labels` array:

- **contains** `fleet:needs-<host>-smoke`
- **contains** `fleet:approved`
- contains **none** of `fleet:needs-fix`, `fleet:blocker`,
  `human:wip`, `fleet:wip`, `fleet:merger-cooldown`,
  `human:needs-fix`
- contains **no** `fleet:reviewing-*` label (another agent is
  already smoking it)

The filter keeps only approved PRs that aren't flagged for fixes and
aren't claimed by the human. If the list is empty, skip the rest of
the protocol. Otherwise pick the oldest (smallest number).

### Acquiring the claim

**Always acquire the claim BEFORE checking out the PR.** Two
same-host author agents (`opus-worker-1` / `opus-worker-2`, or
`sonnet-fleet-1` / `sonnet-fleet-2`) poll the same label and could
race. The reviewer-claim lock serializes them:

```
fleet-claim review-claim <N> <your-worktree-basename>
```

- **Exit 0** — you own this smoke. Proceed.
- **Exit 1** — another agent is already smoking it. Skip to the
  next iteration's task pickup.

### Build + run

a. Re-touch heartbeat so the witness doesn't alarm during the build:
   ```
   fleet-heartbeat <your-worktree-basename>
   ```
b. Check out the PR:
   ```
   gh pr checkout <N> --repo jakildev/IrredenEngine
   ```
c. Build the smoke target:
   ```
   fleet-build --target IRShapeDebug
   ```
   If the build fails, jump to **failure verdict** with the build log.
d. Run the smoke:
   ```
   fleet-run IRShapeDebug --auto-screenshot 10
   ```
   The `10` is the warmup-frame count; the creation's shot table
   decides how many screenshots are taken, and
   `IRWindow::closeWindow()` fires once they are done. Usually
   completes in 10–20 seconds. **Do not** add `--timeout` —
   `fleet-run --timeout` reports "alive at deadline" as success,
   which would mask an `--auto-screenshot` hang.

### Verdict

**Success path** — build and run both exited zero, no crash:

```
gh pr edit <N> --repo jakildev/IrredenEngine --remove-label "fleet:needs-<host>-smoke"
gh pr comment <N> --repo jakildev/IrredenEngine --body "Cross-host smoke OK on <host> (fresh checkout build + IRShapeDebug --auto-screenshot 10)."
```

**Failure path** — build failed, run crashed, or run exited nonzero.
Leave the smoke label on (human/author needs to fix the backend
issue), post a comment with the failure details, and drop the
verdict to `needs-fix`:

```
gh pr comment <N> --repo jakildev/IrredenEngine --body "Cross-host smoke FAILED on <host>: <one-line symptom>. Details: <attach log excerpt>"
gh pr edit <N> --repo jakildev/IrredenEngine --remove-label "fleet:approved" --remove-label "fleet:has-nits" --add-label "fleet:needs-fix"
```

### Release + reset

Always release the claim and reset to a scratch branch, whether the
smoke passed or failed:

```
fleet-claim review-release <N> <your-worktree-basename>
git checkout -B claude/<your-worktree-basename>-scratch origin/master
```

Queue-tick's `cleanup --gh` would sweep a forgotten
`fleet:reviewing-*` label after 30 min, but during that window the
PR cannot be re-smoked.

---

## Sonnet-vs-Opus split

A Sonnet agent's host-smoke pass and an Opus agent's host-smoke pass
**catch different bugs**. The split is deliberate and load-bearing —
Sonnet is faster and cheaper, so it handles the common path; Opus
handles the cases that need judgment.

### What Sonnet catches

`role-sonnet-author`'s smoke run catches anything visible in the
`fleet-build` + `fleet-run` exit codes and logs:

- Build breakage (compile errors, missing symbols, link failures)
- Nonzero exit from `fleet-run`
- Process crashes during the screenshot run
- Shader-compile errors that surface in the run's stdout/stderr

### What Sonnet escalates

Sonnet does **NOT** inspect the generated screenshots. Visual
regressions that exit clean — missing voxels, inverted colors, a
fully black image that still hit `closeWindow()` — are invisible to
Sonnet's pass.

If the run log mentions shader-compile warnings/errors but still
exits zero, the Sonnet agent comments
"smoke run exited clean but log flagged compile warnings; flagging
for Opus recheck" and **leaves the smoke label on** so an opus-worker
re-validates on the next iteration.

### What Opus catches

`role-opus-worker`'s smoke run does everything Sonnet's does, plus:

- Reads the captured screenshots and diagnoses visual regressions
  against the reference behavior baked into
  [`engine/render/CLAUDE.md`](../../engine/render/CLAUDE.md)
  ("Verifying render changes")
- Decides whether ambiguous output (slight color drift, half-voxel
  silhouette differences) is acceptable or a regression
- For diffs that touch shaders or render systems specifically,
  invokes the [`render-debug-loop`](../../.claude/skills/render-debug-loop/SKILL.md)
  skill instead of a single-shot smoke run

### When the split breaks

If a `fleet:needs-<host>-smoke` PR sits with the label for multiple
iterations without an opus-worker picking it up (sonnet-fleet keeps
deferring; opus-worker is busy with `[opus]` tasks), the queue is
underweighted on Opus author capacity for that host. Surface the
backlog in the per-worktree feedback channel (see [`FLEET.md`](FLEET.md)
§ "Fleet feedback channel").

---

## See also

- [`FLEET.md`](FLEET.md) — workflow rules, full label list, model
  split, design escalation
- [`engine/render/CLAUDE.md`](../../engine/render/CLAUDE.md) §
  "Verifying render changes" — what diffs require visual
  verification and what the visual baseline is
- [`.claude/skills/attach-screenshots/SKILL.md`](../../.claude/skills/attach-screenshots/SKILL.md)
  — before/after screenshot capture for the PR body
- [`.claude/skills/render-debug-loop/SKILL.md`](../../.claude/skills/render-debug-loop/SKILL.md)
  — Opus-side visual diagnostic loop used when the smoke run reveals
  a regression

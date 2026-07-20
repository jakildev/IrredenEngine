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
[`role-worker.md`](../../.claude/commands/role-worker.md),
[`role-smoke-worker.md`](../../.claude/commands/role-smoke-worker.md))
point here rather than restating the procedure.

**Dedicated smoke-only mode.** When running a second host (e.g. Ubuntu
alongside a macOS fleet) that should only handle smoke testing, set
`FLEET_SMOKE_WORKER=1` in `~/.fleet/fleet-up.conf` and restart fleet-up.
This dispatches the smoke-worker role into idle pool panes to pick
smoke labels exclusively,
leaving the rest of the pool free for task work. See
[`role-smoke-worker.md`](../../.claude/commands/role-smoke-worker.md) for
the role spec and
[`scripts/fleet/fleet-up.conf.sample`](../../scripts/fleet/fleet-up.conf.sample)
for the configuration option.

Engine repo only. Game-repo PRs do not get cross-host smoke labels —
backends live in the engine.

---

## Labels at a glance

| Label | Owner | Purpose |
|---|---|---|
| `fleet:authored-on-linux` / `fleet:authored-on-macos` / `fleet:authored-on-windows` | `commit-and-push` at PR creation | Records which host the author built + smoked on. Permanent fact, not a state. |
| `fleet:needs-linux-smoke` / `fleet:needs-macos-smoke` / `fleet:needs-windows-smoke` | reviewer (after verdict) | Requests a clean-checkout build + `IRShapeDebug` run from an agent on that host. Cleared by the smoking agent on that host (Windows smoke runs on a native-Windows fleet, or `platform-catchup` as fallback) on success; flipped to `fleet:needs-fix` on failure. |
| `fleet:verified-linux` / `fleet:verified-macos` / `fleet:verified-windows` | smoking agent (Windows: native-Windows fleet or `platform-catchup`) | Set on success to provide a permanent audit trail of which hosts validated the PR. Not used by the merge gate logic; informational. |
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
2. The diff touches at least one render-or-platform path:
   - `engine/render/` (any file)
   - `engine/prefabs/irreden/render/` (any file)
   - any `*.glsl` shader
   - any `*.metal` shader
   - any file under `engine/render/src/shaders/`
   - `engine/system/**` — platform-conditional blocks (`#ifdef _WIN32`, `__APPLE__`, etc.) can silently work on one host and break on another
   - any `CMakeLists.txt` or `CMakePresets.json` — build configuration is the most common silent cross-host failure mode

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

Verification is organized into **two tiers**, not three independent hosts:

- **OpenGL tier = {linux, windows}** — same backend on both; a clean build +
  `IRShapeDebug` smoke on **either one** satisfies it (never both). The engine
  ships on Windows, so `windows` is the tier's canonical representative.
- **Metal tier = {macos}** — its own backend, verified independently.

`commit-and-push` stamped `fleet:authored-on-<host>` at PR creation, and the
author smoke-tested their own host per the workflow — so authoring covers that
host's **tier**. Add a smoke label only for a tier the author did NOT cover:

- `fleet:authored-on-linux` (OpenGL ✓) → add `fleet:needs-macos-smoke`
- `fleet:authored-on-windows` (OpenGL ✓) → add `fleet:needs-macos-smoke`
- `fleet:authored-on-macos` (Metal ✓) → add `fleet:needs-windows-smoke` (one OpenGL host, the ship platform)
- None (unrecognized host or pre-fix PR) → add `fleet:needs-windows-smoke` + `fleet:needs-macos-smoke`

```
# Linux or Windows author (OpenGL covered) → Metal smoke only:
gh pr edit <N> --add-label "fleet:needs-macos-smoke"

# macOS author (Metal covered) → one OpenGL smoke, routed to the ship platform:
gh pr edit <N> --add-label "fleet:needs-windows-smoke"

# None (pre-fix PR) → one OpenGL + Metal:
gh pr edit <N> --add-label "fleet:needs-windows-smoke"
gh pr edit <N> --add-label "fleet:needs-macos-smoke"
```

**Merge gate.** The OpenGL requirement is satisfied by an OpenGL author **or**
by either `fleet:verified-windows` / `fleet:verified-linux` — never both at
once. Routing the OpenGL representative to `windows` exercises the ship platform;
to instead require a Windows verification on *every* render PR (stricter than
"either is enough"), also add `fleet:needs-windows-smoke` to linux-authored PRs.

If `sonnet-reviewer` already added the labels on a first pass, the
later `opus-reviewer` recheck does nothing — the labels are
idempotent.

---

## Author side: claiming + running

Worker iterations of every class poll for the smoke
label that matches their host and execute the protocol below. The
fleet runs at most one smoke run per author iteration so task pickup
isn't starved by back-to-back builds.

`role-smoke-worker` is a dedicated alternative: it *only* claims and
executes smoke runs, never picks tasks, and can run on a second host
dedicated to cross-host validation. Enable it with `FLEET_SMOKE_WORKER=1`
in `~/.fleet/fleet-up.conf`.

### Host detection

Derive the host key from `uname -s`:

- `Linux` → host key `linux`, poll `fleet:needs-linux-smoke`
- `Darwin` → host key `macos`, poll `fleet:needs-macos-smoke`
- `MINGW*` / `MSYS*` / `CYGWIN*` → host key `windows`, poll `fleet:needs-windows-smoke`

The native-Windows fleet (MSYS2 bash + tmux) runs its smoke dispatches exactly like
the other hosts — `fleet-build` / `fleet-run` internally apply the MSYS2 mingw64
`PATH` fix (the `cc1plus` silent-crash guard) and resolve the `.exe` artifact,
so no `cmd /c` hand-wrapping is needed. When no Windows fleet is online, the
`platform-catchup` workflow (#1093) is the manual fallback for clearing
`fleet:needs-windows-smoke`.

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
same-host worker dispatches (in different pool worktrees) poll the
same label
and could race. The reviewer-claim lock serializes them:

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
fleet-review-verdict verdict-needs-fix <N> --agent <your-worktree-basename> --repo jakildev/IrredenEngine
```

`fleet-review-verdict` guards the verdict against your reviewing claim
(`fleet:reviewing-<host>-<your-worktree-basename>`, still held here — the
verdict runs BEFORE the release below), so a smoke failure can only ever
drop the PR *you* claimed to `needs-fix`, never a same-numbered PR from a
batch swap. `verdict-needs-fix` removes `fleet:approved`/`fleet:has-nits`
(and any other stale verdict label) and adds `fleet:needs-fix` in one
idempotent call.

### Release + reset

Always release the claim and reset to a scratch branch, whether the
smoke passed or failed:

```
fleet-claim review-release <N> <your-worktree-basename>
fleet-assert-worktree <your-worktree-basename>
git -C ~/src/IrredenEngine/.claude/worktrees/<your-worktree-basename> checkout -B claude/<your-worktree-basename>-scratch origin/master
```

Run each command as its own Bash call. A bare `git checkout -B`
resolves against the Bash tool's persisted cwd and has parked scratch
branches in shared main clones — the explicit `-C` worktree path makes
the reset cwd-proof. If the assert fails, `cd` back into your worktree
as its own Bash call first (see
[REVIEWER-PROTOCOL.md § Scratch reset & main-clone cwd discipline](REVIEWER-PROTOCOL.md#scratch-reset--main-clone-cwd-discipline)).

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

A sonnet-class smoke run catches anything visible in the
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
exits zero, the sonnet-class iteration comments
"smoke run exited clean but log flagged compile warnings; flagging
for Opus recheck" and **leaves the smoke label on** so an opus+-class
iteration re-validates on the next pass.

### What Opus catches

An opus+-class smoke run does everything Sonnet's does, plus:

- Reads the captured screenshots and diagnoses visual regressions
  against the reference behavior baked into
  [`engine/render/CLAUDE.md`](../../engine/render/CLAUDE.md)
  ("Verifying render changes")
- Decides whether ambiguous output (slight color drift, half-voxel
  silhouette differences) is acceptable or a regression
- For diffs that touch shaders or render systems specifically,
  invokes the [`render-debug-loop`](../../.claude/skills/render-debug-loop/SKILL.md)
  skill instead of a single-shot smoke run

### Windows smoke

When a native-Windows fleet is online, its smoke-worker dispatches poll
`fleet:needs-windows-smoke` and runs the same Sonnet-tier build+run smoke as the
other hosts (host key `windows`), clearing the label to `fleet:verified-windows`
on success. When no Windows fleet is running, the label is instead cleared by the
`platform-catchup` workflow (#1093); if it persists on a PR and neither a Windows
fleet nor platform-catchup has run recently, surface it in the feedback channel.

### When the split breaks

If a `fleet:needs-linux-smoke` or `fleet:needs-macos-smoke` PR sits with the
label for multiple iterations without an opus+-class pass picking it up
(sonnet-class keeps deferring; the heavy classes are busy with tasks), the
queue is underweighted on heavy-class author capacity for that host. Surface
the backlog in the per-worktree feedback channel (see [`FLEET.md`](FLEET.md)
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

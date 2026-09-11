# Cross-host smoke validation

OpenGL (Linux/Windows) and Metal (macOS) are independent backends; a
render PR built on one host can fail to compile, link shaders, or render
on the other with no local signal, so a fleet agent on the other host
checks the PR out, builds the smoke target, runs it, and verdicts. This
doc owns both halves; role files point here. Engine repo only. Labels:
[`fleet-labels-reference.md § Cross-host smoke`](fleet-labels-reference.md);
an outstanding `fleet:needs-<host>-smoke` label is not safe to merge.

**Dedicated smoke-only host.** `FLEET_SMOKE_WORKER=1` in
`~/.fleet/fleet-up.conf` (then restart `fleet-up`) dispatches
`role-smoke-worker` into idle pool panes to claim smoke labels
exclusively (`scripts/fleet/fleet-up.conf.sample`).

---

## Reviewer side: tagging

After setting a verdict on an engine PR, decide from paths alone
(`gh pr diff <N> --name-only`). Tag when the PR is not in a needs-fix /
blocker / WIP state and the diff touches `engine/render/`,
`engine/prefabs/irreden/render/`, any `*.glsl` / `*.metal`, anything under
`engine/render/src/shaders/`, `engine/system/**` (platform-conditional
blocks), or any `CMakeLists.txt` / `CMakePresets.json`. Skip game-repo
PRs, non-render engine PRs, and PRs already carrying the label (tagging is
idempotent across the sonnet pass and the opus recheck).

Two tiers: OpenGL `{linux, windows}` (either host satisfies it; `windows`,
the ship platform, is the canonical representative) and Metal `{macos}`.
`fleet:authored-on-<host>` covers the author's tier, so add only the tier
the author did not cover:

```
# fleet:authored-on-linux or -windows (OpenGL covered):
gh pr edit <N> --add-label "fleet:needs-macos-smoke"

# fleet:authored-on-macos (Metal covered):
gh pr edit <N> --add-label "fleet:needs-windows-smoke"

# no authored-on label:
gh pr edit <N> --add-label "fleet:needs-windows-smoke"
gh pr edit <N> --add-label "fleet:needs-macos-smoke"
```

Merge gate: OpenGL is satisfied by an OpenGL author or by either
`fleet:verified-windows` / `fleet:verified-linux`. To require Windows on
every render PR, also add `fleet:needs-windows-smoke` to linux-authored
PRs.

---

## Author side: claiming + running

Worker iterations of every class poll for their host's label (at most one
smoke run per iteration); `role-smoke-worker` does only this.

**Host key** from `uname -s`: `Linux` → `linux`, `Darwin` → `macos`,
`MINGW*` / `MSYS*` / `CYGWIN*` → `windows`; poll `fleet:needs-<key>-smoke`.
On native Windows `fleet-build` / `fleet-run` apply the MSYS2 mingw64
`PATH` fix and resolve the `.exe`; with no Windows fleet online,
`platform-catchup` clears `fleet:needs-windows-smoke`. The routing is also
enforced in code — `fleet_task_class.HOST_SMOKE_LABELS` maps each host key
to its label (macOS key `mac`, label `macos`), and `fleet-up`'s boot
predicate and the dispatcher's `smoke_worker_should_fire` never wake a
host with no smoke work of its own — while the projection stays
host-agnostic so `state.json` records all outstanding smoke debt.
`review-claim` has no host term, so this host check is what stops a pane
reached by another route from smoking another host's PR.

**Pick** from cached `repos.engine.prs[]`: labels contain
`fleet:needs-<host>-smoke` and `fleet:approved`, none of `fleet:needs-fix`,
`fleet:blocker`, `human:wip`, `fleet:wip`, `fleet:merger-cooldown`,
`human:needs-fix`, and no `fleet:reviewing-*`. Oldest first.

**Claim before checkout** (two same-host panes can race); exit 0 → yours,
exit 1 → another agent's, move on:

```
fleet-claim review-claim <N> <your-worktree-basename>
```

**Build + run:**

```
fleet-heartbeat <your-worktree-basename>
gh pr checkout <N> --repo jakildev/IrredenEngine
fleet-build --target IRShapeDebug
fleet-run IRShapeDebug --auto-screenshot 10
```

`10` is the warmup-frame count; the shot table decides the screenshots
and `IRWindow::closeWindow()` ends the run (10–20 s). No `--timeout`: it
reports "alive at deadline" as success and would mask a hang. Verdict per
[`FLEET.md § Clean-exit policy`](FLEET.md).

**Verdict** — success (build and run exited zero, no crash) is the
`smoke-verify-<host>` edge plus a comment:

```
gh pr edit <N> --repo jakildev/IrredenEngine --remove-label "fleet:needs-<host>-smoke" --add-label "fleet:verified-<host>"
gh pr comment <N> --repo jakildev/IrredenEngine --body "Cross-host smoke OK on <host> (fresh checkout build + IRShapeDebug --auto-screenshot 10)."
```

Failure (build failed, run crashed or non-zero): leave the smoke label,
comment the details, and drop the verdict while still holding the claim
(`fleet-review-verdict` refuses unless you hold
`fleet:reviewing-<host>-<basename>` on that PR):

```
gh pr comment <N> --repo jakildev/IrredenEngine --body "Cross-host smoke FAILED on <host>: <one-line symptom>. Details: <log excerpt>"
fleet-review-verdict verdict-needs-fix <N> --agent <your-worktree-basename> --repo jakildev/IrredenEngine
```

**Release + reset**, pass or fail, each as its own Bash call:

```
fleet-claim review-release <N> <your-worktree-basename>
fleet-assert-worktree <your-worktree-basename>
git -C ~/src/IrredenEngine/.claude/worktrees/<your-worktree-basename> checkout -B claude/<your-worktree-basename>-scratch origin/master
```

If the assert fails, `cd` into your worktree as its own call first
([REVIEWER-PROTOCOL.md § Scratch reset & main-clone cwd discipline](REVIEWER-PROTOCOL.md#scratch-reset--main-clone-cwd-discipline)).
A forgotten `fleet:reviewing-*` label blocks re-smoke until `cleanup --gh`
sweeps it (30 min).

---

## Sonnet-vs-Opus split

A sonnet-class smoke catches what the exit codes and logs show — build
breakage, non-zero `fleet-run`, crashes, shader-compile errors in
stdout/stderr — and does not inspect screenshots; if the log shows
shader-compile warnings but the run exited zero, comment "smoke run
exited clean but log flagged compile warnings; flagging for Opus recheck"
and leave the smoke label on for an opus+ pass. An opus+-class smoke also
reads the captured screenshots against `engine/render/CLAUDE.md`
"Verifying render changes", judges ambiguous output (color drift,
half-voxel silhouettes), and for shader / render-system diffs runs
`render-debug-loop` instead of a single-shot smoke. A smoke label that
sits for multiple iterations without an opus+ pickup means heavy-class
capacity is short on that host — surface it in the feedback channel
([`FLEET.md § Fleet feedback channel`](FLEET.md)).

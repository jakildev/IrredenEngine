---
name: platform-catchup
description: >-
  Processes the backlog of `fleet:needs-<host>-smoke` PRs on the current host
  (Linux, macOS, or native Windows — the primary validation host): builds the
  engine demo cohort against `origin/master`, runs each demo with
  `--auto-screenshot`, and on green batch-swaps `fleet:needs-<host>-smoke` →
  `fleet:verified-<host>` on every eligible merged PR; on a build break or a
  per-demo crash it fix-forwards the offender and holds the labels on PRs that
  touch the implicated paths. Cue-only, never auto-run — use when the human
  says "platform-catchup", "catch up smoke tests", or "catch up Windows
  builds".
---

# platform-catchup

Builds the demo cohort on `origin/master`, runs it, sweeps the smoke labels on
green, fixes forward on red. Label semantics:
[`docs/agents/FLEET-CROSS-HOST-SMOKE.md`](../../../docs/agents/FLEET-CROSS-HOST-SMOKE.md)
§"Labels at a glance". This skill is the batch path that clears them when no
smoke worker is online for the host.

## When

- The human cues `/platform-catchup`, "catch up on smoke tests", "process the
  smoke backlog", or "verify <host> builds the recent PRs".
- A role banner counted ≥ 5 `fleet:needs-<host>-smoke` PRs and the human
  chooses to spend the wall-time.

Skip when the working tree is dirty, or the marker (step 2) matches
`origin/master` and zero PRs are in scope.

## Preconditions

1. `git status --porcelain` is empty; otherwise print
   `platform-catchup: working tree dirty — commit, stash, or discard before invoking`
   and exit.
2. `fleet-build` / `fleet-run` on PATH (`fleet-help` for diagnostics; if
   absent, point at `scripts/fleet/install.sh`). `fleet-build` auto-configures;
   never run `cmake --preset`
   ([`CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md) §"Hard
   rules for autonomous fleet roles").
3. A display. If the first run logs `GLFW: failed to create window`, downgrade
   to build-only smoke: sweep only build-clean PRs and flag the missing display
   in the report.
4. `timeout` on PATH. macOS: `brew install coreutils` and put
   `/opt/homebrew/opt/coreutils/libexec/gnubin` ahead of `/usr/bin` (or alias
   `gtimeout` as `timeout`).
5. Bash tool usage per [`CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md)
   §"Bash tool rules" — single commands, no `cd &&`, pipes only to parse
   wrapper output.

## Per-repo configuration

| Repo | Build cohort |
|---|---|
| `jakildev/IrredenEngine` | every `add_executable(...)` across `creations/demos/*/CMakeLists.txt` plus the `lighting/` and `z_yaw_rotation/` `foreach` lists (~41 targets). `ir-run --targets --plan` is incomplete on some hosts — grep the CMakeLists. |
| `jakildev/irreden` | `IRGame` |

Unknown repo → print
`platform-catchup: no smoke-target config for <repo>; add a mapping to .claude/skills/platform-catchup/SKILL.md`
and exit.

Built but not run-gated:

- `IRMetalClearTest` — Metal only; never built on Linux / Windows.
- `IRCreationDefault` — no `--auto-screenshot`; interactive, never exits.
- `IRZYawInteractive` — mouse-driven, no `--auto-screenshot`.
- `DemoMidiDevice` — needs a USB MIDI controller.

A single-target smoke (`IRShapeDebug` alone) is not a catch-up: a broken
include chain surfaces only in the demos that include it.

## Native Windows delta (host-tag `windows-x86_64`)

Windows is the primary validation host; Linux validation is minimal and
macOS/Metal is the parity backend. Wrappers, the mingw64 PATH fix, and gdb:
[`docs/agents/BUILD.md`](../../../docs/agents/BUILD.md) §"`ir-build` /
`ir-run`", §"Windows-native build", §"Debugging under gdb". Windows has a real
desktop (no build-only downgrade) and MSYS2 ships `/usr/bin/timeout`.

- Every `gh pr list` passes `--limit 500`; the default is 30 and under-counts
  the backlog.
- Building a named target skips `cmake_check_build_system`: after a
  `CMakeLists.txt` edit, reconfigure first
  (`cmd.exe /c "set PATH=C:\msys64\mingw64\bin;%PATH% && cmake <build-dir>"`)
  or build without a single-target filter.
- Shaders are bundled per demo: a `.glsl` edit reaches a demo only after its
  `*Assets` target re-copies it — rebuild the cohort before re-running.
- `fleet-run-targets` mis-scopes `/c/...` vs `C:/...` paths on Windows —
  derive the cohort from the CMakeLists instead.
- A trailing `; echo …` masks a wrapper's exit code — make the command you
  gate on the last one, or scan the log.
- SIGSEGV (exit 139) backtrace:
  `cmd.exe /c "set PATH=C:\msys64\mingw64\bin;%PATH% && cd /d <exe-dir> && gdb --batch -ex run -ex bt -ex quit --args <exe>.exe --auto-screenshot 5"`
- The native GL driver enforces rules lenient Linux/macOS drivers tolerate (a
  uniform block shared by name across stages must be member-identical;
  out-of-bounds `std::span` reads and GL calls during static destruction fault
  here first). A clean Linux build is not evidence the program links on
  Windows; a Windows run crash is an engine bug to root-cause under gdb, not a
  flaky demo.

## Flow

### 1. Detect host + repo

`uname -s -m` →

| `uname` | marker / log host-tag | label host-tag |
|---|---|---|
| `Linux x86_64` | `linux-x86_64` | `linux` |
| `Darwin x86_64` | `macos-x86_64` | `macos` |
| `Darwin arm64` | `macos-arm64` | `macos` |
| `MINGW*` / `MSYS*` | `windows-x86_64` | `windows` |

Labels use the short tag (`fleet:needs-windows-smoke`, `fleet:verified-linux`);
marker and log paths use the arch-qualified tag. One substitution for both
sweeps the wrong label or splits the marker.

`gh repo view --json nameWithOwner --jq .nameWithOwner` → cohort from the table above.

### 2. Read marker; short-circuit if up to date

Marker: `~/.fleet/platform-catchup/<repo-basename>-<host-tag>.json`, shared by
every worktree of the repo on the host:

```json
{
  "last_verified_commit": "<oid>",
  "last_run_at": "<ISO8601>",
  "last_outcome": "green" | "red" | "partial",
  "held_prs": [<numbers>],
  "skipped_targets": [<names>]
}
```

If `last_verified_commit == git rev-parse origin/master` AND
`gh pr list --label "fleet:needs-<label-host-tag>-smoke" --state all --json number --jq length --limit 500`
is `0`, print
`platform-catchup: marker matches origin/master, no labeled PRs in scope; nothing to do`
and exit.

### 3. Read backlog

```bash
gh pr list --repo <repo> --label "fleet:needs-<label-host-tag>-smoke" \
  --state all --json number,title,state,mergedAt,mergeCommit,labels,headRefName \
  --limit 500 > ~/.fleet/platform-catchup/<host-tag>-backlog.json
```

Drop CLOSED PRs (not on master). MERGED = the swap set; OPEN = not actionable
here (head is not on master) — note in the report, exclude from the sweep.
Skip any PR labeled `fleet:wip`, `human:wip`, `fleet:blocker`, or
`fleet:needs-fix`; its smoke label is stale or misapplied.

### 4. Verify tree clean + fetch

```bash
git status --porcelain
git fetch origin --quiet
```

Dirty tree → abort per Preconditions. Fetch failure → print and exit; never
run against a stale `origin/master`.

### 5. Aggregate build pass

`<log-dir>` = `~/.fleet/platform-catchup/logs/<host-tag>/` (`mkdir -p` first);
steps 5, 6, and 8d write there.

```bash
fleet-build --target <target-list> -- -k > <log-dir>/build-all.log 2>&1
```

`-- -k` keeps going past the first failure so every regression surfaces in one
run. Scan for `error:`, `FAILED`, `gmake.*Error`; map each failed target to its
source file and message. Failed targets are skipped in step 6; the rest proceed.

### 6. Aggregate run pass

Per built target:

```bash
timeout --kill-after=10 <hard-budget> \
  fleet-run --timeout <budget> <target> --auto-screenshot 30 \
  > <log-dir>/<target>.log 2>&1
```

`fleet-run --timeout` precedes the exe name — `ir-run` stops option parsing at
the first positional and passes the rest to the demo. The bash `timeout` is the
backstop for a demo whose argv loop never arms `--auto-screenshot`. `<budget>` =
30 s (lighting demos and `IRLuaPerfGrid` legitimately run 13–25 s);
`<hard-budget>` = `<budget> + 15` = 45 s.

Pass criteria, all required:

- Wrapper verdict `ir-run: RESULT=CLEAN`
  ([`FLEET.md`](../../../docs/agents/FLEET.md) §"Clean-exit policy" — a
  teardown crash with every shot saved is still a crash).
- Exit code 0 (124 = killed by the bash watchdog → fail).
- ≥ 1 `Saved screenshot:` line. None → the "no-shots" bucket, never a pass.
- No `panic`, `segmentation fault`, `FATAL`, `[error]` in the log tail.

### 7. Decide outcome

| Build | Run | Outcome |
|---|---|---|
| All targets build | All pass | **green** |
| All targets build | Some hang (124) or crash | **partial-runtime** |
| Some targets fail to build | Survivors pass or fail | **partial-build** |
| All targets fail to build | n/a | **red** |

### 8a. green — full label sweep

For every merged PR in the backlog, serially:

```bash
gh pr edit <N> --repo <repo> \
  --remove-label "fleet:needs-<label-host-tag>-smoke" \
  --add-label "fleet:verified-<label-host-tag>"
```

OPEN PRs keep their label. Marker: `last_verified_commit = origin/master HEAD`,
`last_outcome = "green"`.

### 8b. partial-runtime — sweep most, hold the offender's PRs

A runtime hang or per-demo crash implicates only that demo's source paths.
Sweep every merged PR except those whose `gh pr diff <N> --name-only` touches
the offending demo's tree:

| Offending demo | Hold-back path glob |
|---|---|
| `IRPerfGrid` | `creations/demos/perf_grid/` |
| `IRLuaPerfGrid` | `creations/demos/lua_perf_grid/` |
| `IRCanvasStress` | `creations/demos/canvas_stress/` |
| `IRLighting*` | `creations/demos/lighting/` |
| other | the demo's source dir |

Held PRs keep their labels and are listed in the marker's `held_prs`.

Then fix forward ([`FLEET.md`](../../../docs/agents/FLEET.md)
§"Fix-forward"): bisect the deterministic repro, root-cause, and open a fix PR
this session, whichever merged change introduced it. File instead only when the
fix genuinely exceeds the session (design escalation, other-host-only repro):

```bash
gh issue create --repo <repo> \
  --title "platform-parity: <demo> <symptom> on <host-tag>" \
  --body "<...>" --label "fleet:task"
```

Body: the demo, the exact repro command and its `ir-run: RESULT=` line, a log
excerpt, the bisect window or suspected root cause, and what was ruled out.

Marker: `last_outcome = "partial"`, `skipped_targets = [<failed-targets>]`,
`held_prs = [<N>, ...]`.

### 8c. partial-build — identify the offender, fix inline

Every PR merged after the offender sits on a master that does not compile.

**Latent header / missing-include bug** (a header was always wrong; a recent
include chain exposed it):

1. Identify the missing include from the compiler error.
2. `git checkout -b claude/fix-<short-description> origin/master`
3. `git add <header>` and `git commit -m "<...>"`
4. Push and open the PR via `commit-and-push` (or `git push -u` +
   `gh pr create` if a background build would contend for CPU).
5. `gh pr edit <PR> --add-label "fleet:authored-on-<label-host-tag>"`

**Functional bug** introduced by a recent PR: file the issue — the original
author owns the fix —
`gh issue create --title "platform-parity: <host-tag> build break in PR #<N>" --label "fleet:task" --body "<...>"`.

Either way, hold the labels on the offender PR and every PR touching the same
source paths, and continue the run pass for the targets that built.

Marker: `last_outcome = "partial"`, `held_prs = [<offender-PR-and-related>]`.

### 8d. red — walk back

Full build break (`IRShapeDebug` failed). Linear newest-first walk:

```bash
for pr in <merged-by-mergedAt-desc>:
    git checkout <pr.mergeCommit.oid>
    fleet-build --target IRShapeDebug -- -k > <log-dir>/walkback-<pr.number>.log 2>&1
    if green:
        FIRST_GREEN = pr
        break
```

The offender is the PR merged after `FIRST_GREEN`; file it per 8c "Functional
bug". Restore:

```bash
git checkout master
git pull --ff-only origin master
```

If the pull is not fast-forward, leave the tree at the fetched `origin/master`
SHA, print a note, and exit.

Marker: `last_outcome = "red"`, `last_verified_commit = <last-green oid>`,
`held_prs = <all merged after last green>`.

### 9. Report

```
platform-catchup: <repo> on <host-tag>
  backlog: <N-merged> merged PRs labeled fleet:needs-<host-tag>-smoke
           (<N-open> open PRs ignored; <N-closed> closed PRs ignored)
  master:  <oid> (<git log --oneline -1>)
  build:   <X/Y> targets clean (<failed>)
  run:     <X/Y> targets pass (<failed>)
  outcome: <green | partial-runtime | partial-build | red>
  swept:   <N> PRs (fleet:needs-<label-host-tag>-smoke → fleet:verified-<label-host-tag>)
  held:    <N> PRs (touched <held-paths>; will sweep after <PR/issue> resolves)
  filed:   <N> parity-fix issue(s): <#issue-list>
  fix PR:  #<N> (if step 8c opened one)
  marker:  ~/.fleet/platform-catchup/<repo>-<host-tag>.json
```

## Safety and budget

- Fixes go on a fresh branch off `origin/master`. `git pull --ff-only` only;
  never `--rebase` or `--force`.
- Hard budget 60 minutes, enforced by the Bash tool's `timeout` parameter;
  emit the report when it expires, even mid walk-back.
- Walk-back is linear newest-first; move to `git bisect run` only if backlogs
  routinely exceed 30 PRs.
- GitHub labels are the shared state; there is no cross-host ledger. The
  cohort is rebuilt unconditionally (no Lua-only-PR shortcut). Game / editor
  cohorts need their own config row.

## Failure modes

| Condition | Response |
|---|---|
| Dirty tree | Refuse to run. |
| No display | Build-only smoke; sweep only build-clean PRs; flag it in the report. |
| `fleet-build` reconfigures (build dir gone) | Allow — auto-configure is normal. |
| Demo hangs past hard budget | Bash `timeout` kills it (124); recorded as a run failure; 8b path. |
| `gh pr edit` rate-limited | Back off 60 s, retry once; on second failure write the unprocessed PR list to marker `skipped_prs` and exit. |
| `git fetch` fails | Print and exit. |
| Unknown repo | Print and exit. |

## Recovery after a mid-flight exit

1. `git status` — on a detached HEAD from walk-back, `git checkout master`.
2. Read the marker: `last_run_at` is the last write; `held_prs` is what was
   not swept.
3. Re-invoke; step 2 short-circuits if HEAD is unchanged and no PRs are in
   scope.

---
name: platform-catchup
description: >-
  Process the backlog of `fleet:needs-<host>-smoke` PRs on the current host.
  Builds a representative demo cohort against current `origin/master`, runs
  each with `--auto-screenshot`, and on green batch-swaps
  `fleet:needs-<host>-smoke` → `fleet:verified-<host>` on every eligible
  merged PR. On red (build break), files / opens a fix PR for the offender
  and holds labels on PRs that touch the implicated source paths until
  the fix lands. On partial (per-demo runtime hang or crash but build
  green), sweeps the rest, then fix-forwards the offending demo this
  session (issue only as the documented fallback). Runs on Linux, macOS, AND native Windows (MSYS2
  mingw64) — Windows is now the primary validation host. Use when the human
  cues "platform-catchup", "catch up smoke tests", or "catch up Windows
  builds". Skill is **cue-only**, never auto-run — multi-target builds and
  ~40-demo runs are expensive, the human chooses when to spend them.
---

# platform-catchup

## Purpose

The fleet labels PRs with `fleet:authored-on-<host>` at creation and
`fleet:needs-<other-host>-smoke` after the reviewer's verdict, but no
role loop processes the smoke backlog. Without a catch-up workflow the
labels accumulate forever and `fleet:authored-on-macos` PRs that broke
the Linux build only get noticed the next time a fleet agent on Linux
touches the affected demo. This skill is the structured catch-up: build
the representative demo set on `origin/master`, run them, sweep the
labels on green, file / open fixes on red.

## Trigger conditions

Invoke when:

- The human types `/platform-catchup`, "catch up on smoke tests",
  "process the smoke backlog", or "verify Linux builds the recent PRs".
- A fleet role's startup banner counted ≥ 5
  `fleet:needs-<host>-smoke` PRs on the current host and the human
  decides to spend wall-time on processing them.

Skip when:

- Working tree is dirty (the skill refuses to commingle user work
  with the catch-up — commit, stash, or discard first).
- Local marker (see step 2) reports the current `origin/master` HEAD
  was already verified AND zero PRs are in scope.

## Bash tool rules

Single-command Bash. No `cd && <cmd>`. No shell pipes / redirects
unless strictly needed to parse fleet-build / fleet-run output (which
prints to stdout regardless). Prefer Read / Glob / Grep tools for
filesystem queries. Violations block unattended operation with
permission prompts.

## Preconditions

1. **Tree is clean.** `git status --porcelain` returns zero lines.
   Skill prints `platform-catchup: working tree dirty — commit,
   stash, or discard before invoking` and exits.
2. **fleet-build / fleet-run on PATH.** Both wrappers are required;
   the skill also calls `fleet-help` for diagnostics. If absent, point
   the user at `scripts/fleet/install.sh`.
3. **Host has a usable display.** WSLg, native Linux X/Wayland, or
   macOS desktop. Headless hosts can build but `--auto-screenshot`
   needs a window; the skill detects "GLFW: failed to create window"
   in the first run's log and downgrades to build-only smoke (sweep
   only build-clean PRs; flag the missing display in the report).
4. **Build preset already configured.** `fleet-build` auto-configures
   on first run; the skill does not run `cmake --preset` itself (per
   the architect / worker hard rules).
5. **`timeout` available.** Linux ships `timeout` in GNU coreutils.
   macOS lacks it by default — `brew install coreutils` provides
   `gtimeout`; ensure it is aliased or symlinked as `timeout` (or that
   `/opt/homebrew/opt/coreutils/libexec/gnubin` is on PATH before
   `/usr/bin`). macOS validation is deferred for v1; add this to the
   macOS setup checklist when enabling it.

## Per-repo configuration

Auto-detect repo from `gh repo view --json nameWithOwner --jq .nameWithOwner`.
The known mappings live inline in this skill (one place to update when
a new creation joins the fleet):

| Repo                       | Build cohort                                                 |
|----------------------------|--------------------------------------------------------------|
| `jakildev/IrredenEngine`   | Engine demo cohort (see below) — ~41 targets, derive from source |
| `jakildev/irreden`         | `IRGame` (single-target)                                     |

### Engine demo cohort

> **This static list drifts — derive the cohort from the source.** The
> authoritative build set is every `add_executable(...)` across
> `creations/demos/*/CMakeLists.txt` plus the `lighting/` (15 targets, incl.
> `IRLightingHDR`) and `z_yaw_rotation/` (2) `foreach` lists. As of 2026-06
> that is **41 targets** — the snapshot below predates `IRLightingHDR` and the
> newer demos `IRFogDemo`, `IRDayCycle`, `IRSceneReset`, `IRSkeletalDemo`,
> `IRCollisionOverlapDemo`, `IRLuaWidgetsDemo`, `IRAudioPlayback`,
> `IRChunkStreamingSmoke`, `IRAnalyticOracle`, `IRVoxelYaw`. `ir-run --targets
> --plan` is the build-system view but is incomplete on some hosts; grep the
> CMakeLists when in doubt.

```
IRShapeDebug              IRCanvasStress              IRGpuParticles
IRLowresPan               IRLuaPerfGrid               IRLuaPipelineDemo
IRModifierDemo            IRPerfGrid                  IRSpriteDemo
IRStatelessParticles      IRUIWidgetsDemo             IRUiDockspace
IRLightingAO              IRLightingSunShadow         IRLightingEmissive
IRLightingCombined        IRLightingDebugOverlays     IRLightingPoint
IRLightingSpot            IRLightingDirectional       IRLightingSunShading
IRLightingSdfCascade      IRLightingSdfBlocker        IRLightingSunOrbit
IRLightingSunElevationOrbit  IRLightingPerCanvasScope IRZYawStatic
```

**Excluded from the auto cohort** (build OK, run-pass criteria don't apply):

- `IRMetalClearTest` — Metal backend only; never built on Linux / Windows.
- `IRCreationDefault` — no `--auto-screenshot` support; interactive,
  never auto-exits, hangs the runner until the watchdog kills it.
- `IRZYawInteractive` — mouse-driven, no `--auto-screenshot`.
- `DemoMidiDevice` — requires a USB MIDI controller plugged in.

**Why a multi-target cohort instead of just `IRShapeDebug`?** A
single-target smoke misses real regressions: the
`command_close_window.hpp` missing-include bug found during catch-up
on 2026-05-24 (PR #1152 fix) only manifested when building
`IRUIWidgetsDemo` / `IRUiDockspace`. `IRShapeDebug` built clean
against the same source. v1 of this skill must build multi-target.

## Native Windows (host-tag `windows-x86_64`)

Windows is a first-class catch-up host — the fleet's **primary** validation
host going forward (Linux validation is minimal; macOS/Metal is the parity
backend). Everything below is the Windows delta on the generic flow.

**Build / run wrappers.** Use `ir-build` / `ir-run` (the `fleet-build` /
`fleet-run` shims still work). On Windows `ir-build` runs the whole
`cmake --build` inside `cmd.exe` with `C:\msys64\mingw64\bin` prepended to
PATH — the cc1plus silent-crash fix (see BUILD.md). `ir-run` resolves the
`.exe` suffix and prepends the mingw64 runtime DLLs.

**Display + timeout.** Native Windows has a real desktop, so GLFW window
creation and `--auto-screenshot` work — no headless build-only downgrade.
MSYS2 ships `timeout` at `/usr/bin/timeout`, so the belt-and-suspenders bash
wrapper from step 6 is available; `ir-run --timeout` (pure bash) also works.

**Gotchas from the first native-Windows run (2026-06, PR #2034):**

- **`gh pr list` default `--limit 30` under-counts the backlog** — always
  `--limit 500` (see step 2). The real merged backlog was 135, not 30.
- **Building a NAMED target skips `cmake_check_build_system`.** After editing
  a `CMakeLists.txt`, `ir-build --target <one-target>` does NOT reconfigure
  and the stale recipe runs. Force a reconfigure first
  (`cmd.exe /c "set PATH=C:\msys64\mingw64\bin;%PATH% && cmake <build-dir>"`),
  or build with no single-target filter.
- **Shaders are bundled per-demo.** A `.glsl` edit only reaches a demo after
  its `*Assets` target re-copies it — rebuild the cohort to re-bundle before
  re-running.
- **`fleet-run-targets` has a Windows path-scope bug** (`/c/...` vs `C:/...`
  mismatch → "scope … is outside …; showing whole tree" / "no built
  executables"). Derive the cohort from the CMakeLists instead (above); a fix
  is tracked separately.
- **A trailing `; echo …` masks a wrapper's real exit code** — the `echo`
  returns 0 and hides a failed `ir-build`/`ir-run`. Make the command you care
  about the LAST one, or scan the log rather than trusting the exit status.
- **`gdb` works** (`/c/msys64/mingw64/bin/gdb.exe`). For a SIGSEGV (exit 139),
  get a backtrace with
  `cmd.exe /c "set PATH=C:\msys64\mingw64\bin;%PATH% && cd /d <exe-dir> && gdb
  --batch -ex run -ex bt -ex quit --args <exe>.exe --auto-screenshot 5"` — this
  is how the first run's three segfaults were root-caused (#2031, #2032).

**Stricter-driver class of finding.** The native-Windows GL driver enforces
GLSL/GL rules lenient Linux/macOS drivers tolerate — e.g. a uniform block
shared by name across stages MUST be member-identical (the
`FrameDataIsoTriangles` vertex/fragment mismatch, PR #2034, crashed every demo
at link). A clean Linux build is NOT evidence the GL program links on Windows.
Expect "latent everywhere, fatal only here" UB — out-of-bounds `std::span`
derefs, GL calls at process-exit static destruction — to surface first on
Windows under gcc-15/MSYS2. Treat a Windows run-pass crash as a real engine
bug to root-cause (gdb), not a flaky demo.

## Flow

### 1. Detect host + repo

```bash
uname -s -m
```

Map to host-tag:

| `uname` output       | host-tag (marker/log) | label host-tag |
|----------------------|-----------------------|----------------|
| `Linux x86_64`       | `linux-x86_64`        | `linux`        |
| `Darwin x86_64`      | `macos-x86_64`        | `macos`        |
| `Darwin arm64`       | `macos-arm64`         | `macos`        |
| `MINGW*` / `MSYS*`   | `windows-x86_64`      | `windows`      |

**Two host-tags — do not conflate them.** The `fleet:needs-<host>-smoke` /
`fleet:verified-<host>` LABELS use the **short** form (`windows`, `linux`,
`macos`) — verified against the live label catalog. The **marker + log-dir
paths** use the arch-qualified form (`windows-x86_64`, matching the existing
`linux-x86_64` markers). Build the label name from the short tag and every
local path from the arch tag; one `<host-tag>` substitution used everywhere
will either sweep the wrong label or split the marker.

**Native Windows is supported and is now the primary validation host** (see
"Native Windows" below) — the historical "exit on `MINGW*`" guard is gone. The
fleet runs natively on Windows (MSYS2 mingw64, OpenGL) via `ir-build` /
`ir-run`; WSL is no longer required and Linux validation is minimal going
forward. macOS/Metal stays the parity backend.

```bash
gh repo view --json nameWithOwner --jq .nameWithOwner
```

Unknown repo → `platform-catchup: no smoke-target config for <repo>;
add a mapping to .claude/skills/platform-catchup/SKILL.md` and exit.

### 2. Read marker; short-circuit if up-to-date

Marker path: `~/.fleet/platform-catchup/<repo-basename>-<host-tag>.json`.

(Departure from issue #1093 spec, which named
`.claude/platform-verify/<os>-<arch>.json`. The fleet-shared location
is used so multiple worktrees of the same repo on the same host share
the marker. The original in-repo location would have fragmented the
marker per-worktree, defeating the short-circuit.)

Format:

```json
{
  "last_verified_commit": "<oid>",
  "last_run_at": "<ISO8601>",
  "last_outcome": "green" | "red" | "partial",
  "held_prs": [<numbers>],
  "skipped_targets": [<names>]
}
```

Read `git rev-parse origin/master`. If `last_verified_commit == HEAD`
AND `gh pr list --label "fleet:needs-<label-host-tag>-smoke" --state all
--json number --jq length --limit 500` is `0`, print `platform-catchup:
marker matches origin/master, no labeled PRs in scope; nothing to do` and
exit. Otherwise continue.

> **`--limit` gotcha (load-bearing).** `gh pr list` silently defaults to
> **30** rows. EVERY count/list query in this skill MUST pass `--limit 500`.
> On the first native-Windows catch-up the real merged backlog was **135**,
> but the default-limited `length` reported `30` and badly under-counted the
> work. A missing `--limit` reads as "small backlog" when it isn't.

### 3. Read backlog

```bash
gh pr list --repo <repo> --label "fleet:needs-<label-host-tag>-smoke" \
  --state all --json number,title,state,mergedAt,mergeCommit,labels,headRefName \
  --limit 500 > ~/.fleet/platform-catchup/<host-tag>-backlog.json
```

Strip CLOSED PRs — not on master. Group remaining by state:
- MERGED: validation set (the swap target).
- OPEN: non-actionable here (head branch isn't on master); note in
  report, don't include in sweep.

### 4. Verify tree clean + fetch

```bash
git status --porcelain
git fetch origin --quiet
```

Abort per Preconditions if `git status` returns lines.

### 5. Aggregate build pass

Log root for this run: `~/.fleet/platform-catchup/logs/<host-tag>/`
(create with `mkdir -p` before writing). Steps 5, 6, and 8d all write
to this directory — referenced as `<log-dir>` throughout.

```bash
fleet-build --target <target-list> -- -k > <log-dir>/build-all.log 2>&1
```

The `-- -k` flag (`--keep-going`) tells GNU make to continue past the
first failure so multiple regressions surface in one run. Skipping it
forces N-1 wasted re-runs when there are N independent regressions.

Scan the log for `error:`, `FAILED`, `gmake.*Error`. Map every failed
target to its source file + error message. Failed-to-build targets are
skipped in step 6 but the catch-up still proceeds for the targets that
did build.

### 6. Aggregate run pass

For each target that built successfully:

```bash
timeout --kill-after=10 <hard-budget> \
  fleet-run --timeout <budget> <target> --auto-screenshot 30 \
  > <log-dir>/<target>.log 2>&1
```

**Argument-order gotcha:** `fleet-run --timeout <N>` must come BEFORE
the executable name. `ir-run` parses options up to the first non-flag
positional (the exe name); arguments after the exe name are passed
through to the program. Getting this wrong means `--timeout` is
silently ignored, the demo hangs forever, and the bash `timeout`
backstop is the only thing that eventually kills it.

The bash `timeout --kill-after=10 <hard-budget>` wrapper is
non-negotiable belt-and-suspenders. `fleet-run`'s watchdog should
fire first, but if a demo bypasses `--auto-screenshot` parsing (e.g.
the flag is stale, the demo's argv-loop skips it) the watchdog never
arms. The bash `timeout` guarantees forward progress.

Budget recommendations:
- `<budget>` = 30s for most demos.
- `<hard-budget>` = `<budget> + 15` = 45s.
- Lighting demos and `IRLuaPerfGrid` can legitimately run 13–25s; do
  not set `<budget>` below 30s.

Per-target pass criteria:
- Exit code = 0 (124 from bash `timeout` = killed by watchdog → fail)
- ≥ 1 screenshot saved (grep `Saved screenshot:` in log; demos that
  pass without shots go into the "no-shots" bucket; do not auto-pass)
- No `panic`, `segmentation fault`, `FATAL`, `[error]` (engine logs
  `[error]` for shader compile fails and resource creation errors)
  in the log tail

### 7. Decide outcome

"Pass" for a run means the wrapper's `ir-run: RESULT=CLEAN` verdict — a
`RESULT=CRASH` (teardown crashes included, even with all screenshots
saved) is a per-demo crash for this table, per
[`docs/agents/FLEET.md`](../../../docs/agents/FLEET.md) §"Clean-exit
policy".

| Build result                       | Run result                                  | Outcome    |
|------------------------------------|---------------------------------------------|------------|
| All targets build                  | All pass                                    | **green**  |
| All targets build                  | Some hang (timeout 124) or per-demo crashes | **partial-runtime** |
| Some targets fail to build         | Survivors pass                              | **partial-build**   |
| Some targets fail to build         | Survivors hang / crash                      | **partial-build**   |
| All targets fail to build          | n/a                                         | **red**             |

### 8a. On **green**: full label sweep

For every merged PR in the backlog (closed PRs already stripped):

```bash
gh pr edit <N> --repo <repo> \
  --remove-label "fleet:needs-<label-host-tag>-smoke" \
  --add-label "fleet:verified-<label-host-tag>"
```

Run serially. The OPEN PRs with the label are left untouched.

Update marker: `last_verified_commit = origin/master HEAD`,
`last_outcome = "green"`.

### 8b. On **partial-runtime**: sweep most, hold the offender's PRs

A runtime hang or per-demo crash (e.g. `IRPerfGrid` 1-FPS hang)
implicates **only that demo's source paths**, not master as a whole.
The catch-up's value is still real for the other 26+ demos.

Sweep all merged PRs EXCEPT those whose `gh pr diff <N> --name-only`
touches the offending demo's source tree:

| Offending demo            | Hold-back path glob                         |
|---------------------------|---------------------------------------------|
| `IRPerfGrid`              | `creations/demos/perf_grid/`                |
| `IRLuaPerfGrid`           | `creations/demos/lua_perf_grid/`            |
| `IRCanvasStress`          | `creations/demos/canvas_stress/`            |
| `IRLighting*`             | `creations/demos/lighting/`                 |
| (other demo regressions)  | the demo's source dir                       |

For each held-back PR, do NOT edit the labels. Note them in the
report's `held_prs` field.

Then apply fix-forward
([`docs/agents/FLEET.md`](../../../docs/agents/FLEET.md) §"Fix-forward"):
after the sweep, attempt the offender's fix **this session** — bisect
the deterministic repro, root-cause, and open a fix PR — regardless of
which merged change introduced it. Fall back to filing only when the
fix genuinely exceeds the session (design escalation, other-host-only
repro), and then with full forensics:

```bash
gh issue create --repo <repo> \
  --title "platform-parity: <demo> <symptom> on <host-tag>" \
  --body "<...>" --label "fleet:task"
```

Body covers: which demo, the exact repro command and its
`ir-run: RESULT=` line, log excerpt, the bisect window or suspected
root cause, and what was already ruled out.

Update marker: `last_outcome = "partial"`,
`skipped_targets = [<failed-targets>]`,
`held_prs = [<N>, ...]`.

### 8c. On **partial-build**: identify the offender, fix it inline

Build failures are blocking — every PR after the offender is sitting
on a master that doesn't compile. Two paths:

**Latent header / missing-include bug** (the
`command_close_window.hpp` class — a header was always wrong but only
became visible when a recent PR added an include chain that exposed
it). The fix is the right move:

1. Identify the missing include from the compiler error.
2. Apply the fix on a fresh branch off `origin/master`:
   ```bash
   git checkout -b claude/fix-<short-description> origin/master
   ```
3. Stage + commit:
   ```bash
   git add <header>
   git commit -m "<...>"
   ```
4. Push + open PR via `commit-and-push` skill (or, if a background
   build is running and you want to avoid CPU contention, manually
   via `git push -u` + `gh pr create`).
5. Apply the host-author label: `gh pr edit <PR> --add-label
   "fleet:authored-on-<label-host-tag>"`.

**Functional bug** in a recent PR (the recent change actually
introduced a wrong-behavior regression): file an issue, do NOT
attempt to fix unilaterally — the original author needs to own the
fix. Do `gh issue create --title "platform-parity: <host-tag> build
break in PR #<N>" --label "fleet:task" --body "<...>"`.

In both cases, hold the labels on the offender PR AND all PRs that
touch the same source paths. Continue the catch-up's run pass for
targets that built successfully (the partial-build outcome is still
useful — most demos build clean even when one chain is broken).

Update marker: `last_outcome = "partial"`,
`held_prs = [<offender-PR-and-related>]`.

### 8d. On **red**: walk back

Full build break (`IRShapeDebug` failed). Find the offending PR via
linear newest-first walk:

```bash
for pr in <merged-by-mergedAt-desc>:
    git checkout <pr.mergeCommit.oid>
    fleet-build --target IRShapeDebug -- -k > <log-dir>/walkback-<pr.number>.log 2>&1
    if green:
        FIRST_GREEN = pr
        break
```

When the first green commit is found, the offending PR is the next
merged-after. File the parity-fix issue per 8c "Functional bug" path.

Restore tree:

```bash
git checkout master
git pull --ff-only origin master
```

If the pull is not fast-forward, leave the tree where it is (the
originally-fetched `origin/master` SHA) — print a note and exit; the
user can sort out the divergence.

Update marker: `last_outcome = "red"`,
`last_verified_commit = <last-green oid>`,
`held_prs = <all merged-after-last-green>`.

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

## Safety

- **Never commits to `master`.** All fixes go on a fresh branch off
  `origin/master`.
- **Never `git pull --rebase` or `--force`.** Only `--ff-only`.
- **Hard time budget: 60 minutes default.** Each step has a sub-budget;
  the report is emitted as soon as the budget is exhausted even if
  walk-back is mid-flight. The Bash tool's `timeout` parameter
  enforces this externally.
- **Walk-back is linear newest-first.** Upgrade to `git bisect run`
  if backlogs routinely exceed 30 PRs.
- **Skips any PR labeled `fleet:wip`, `human:wip`, `fleet:blocker`, or
  `fleet:needs-fix`.** Those aren't merge-ready; the smoke label on
  them is stale or misapplied.

## Failure modes

| Condition                                     | Response                                                                                                |
|-----------------------------------------------|---------------------------------------------------------------------------------------------------------|
| Dirty tree                                    | Refuse to run.                                                                                          |
| No display (headless)                         | Downgrade to build-only smoke; sweep only build-clean PRs; flag missing display in report.              |
| `fleet-build` reconfigures (build dir gone)   | Allow — auto-configure is normal.                                                                       |
| Demo hangs past hard budget                   | Bash `timeout` kills it (exit 124); recorded as run failure; sweep follows 8b partial-runtime path.     |
| `gh pr edit` rate-limited                     | Back off 60s, retry once; on second failure, write unprocessed PR list to marker `skipped_prs`, exit.   |
| `git fetch` fails (no network)                | Print + exit; do not run against stale `origin/master`.                                                 |
| Unknown repo                                  | Print + exit; per-repo config is authoritative.                                                         |

## Recovery

If the skill exits mid-flight (usage-limit, interrupt, crash):

1. `git status` — if on detached HEAD from walk-back, `git checkout master`.
2. Check `~/.fleet/platform-catchup/<repo>-<host-tag>.json` —
   `last_run_at` is the most recent write; `held_prs` lists what wasn't
   swept.
3. Re-invoke; the marker short-circuit (step 2) skips if HEAD unchanged
   AND no new labeled PRs.

## Spec departures from issue #1093

The spec at https://github.com/jakildev/IrredenEngine/issues/1093 prescribed
behaviors that didn't survive contact with the real catch-up. The departures:

1. **Marker location.** Spec said
   `.claude/platform-verify/<os>-<arch>.json` (per-worktree). Moved to
   `~/.fleet/platform-catchup/<repo>-<host-tag>.json` (per-host-user) so
   worktrees on the same host share the short-circuit.
2. **Single smoke target.** Spec said one target per repo (`IRShapeDebug`
   for engine). Extended to a 27-target cohort because real regressions
   live outside `IRShapeDebug` (the `command_close_window.hpp` bug only
   broke `IRUIWidgetsDemo` / `IRUiDockspace`).
3. **Reference image comparison.** Spec said "compare against newest
   committed reference under `docs/pr-screenshots/` for this host."
   Skipped: the repo has no per-host canonical reference convention;
   `docs/pr-screenshots/<branch>/` is per-PR-branch ad-hoc. v1 pass
   signal is `build green + run exit 0 + ≥1 shot saved`. Pixel-diff
   against a canonical baseline is follow-up work that needs the
   baseline convention defined first (which baseline lives where, who
   updates it on intended visual changes, tolerance).
4. **`.gitignore` addition for `.claude/platform-verify/`.** Not needed
   — `.gitignore` already has `.claude/*` with explicit re-includes for
   `skills/`, `commands/`, `rules/`, `agents/`; anything else under
   `.claude/` is ignored.
5. **Outcome taxonomy.** Spec was binary green / red. Real catch-up has
   four outcomes (green, partial-runtime, partial-build, red) because
   "build-broken master with one offender" needs a different label-sweep
   policy than "demo X hangs but the rest are fine."
6. **Walk-back trigger.** Spec walked back on any failure. Refined to
   walk back only on **red** (full build break) or **partial-build**
   (some demos fail to compile). Partial-runtime (a demo hangs but
   build is clean) doesn't need walk-back — the failing demo's source
   dir is the implicated path, not a single offending commit.

Issue #1093 should be updated to reference this skill as the
authoritative spec going forward.

## Out of scope

- **`git bisect`-based walk-back.** Linear only for v1.
- **Cross-host shared ledger.** GitHub labels are the shared state.
- **Lua-script-only PR optimization.** v1 rebuilds the cohort
  unconditionally.
- **Game / editor-specific catch-up.** Engine demos today. Game-side
  smoke needs a separate config block + cohort if/when the game
  accumulates a backlog.

## Example invocation

```
User: /platform-catchup
Claude: [runs the flow, reports]
  platform-catchup: jakildev/IrredenEngine on linux-x86_64
    backlog: 99 merged + 0 open + 2 closed
    master:  3d031e44 (queue: maintenance sync)
    build:   25/27 targets clean (IRUIWidgetsDemo, IRUiDockspace failed)
    run:     26/26 targets pass (IRPerfGrid timed out)
    outcome: partial-build + partial-runtime
    swept:   97 PRs
    held:    2 PRs (#1094 touched command_close_window.hpp chain;
                    #1117 touched perf_grid/)
    filed:   1 issue: #<perf-investigation>
    fix PR:  #1152 (command_close_window.hpp missing include)
    marker:  ~/.fleet/platform-catchup/IrredenEngine-linux-x86_64.json
```

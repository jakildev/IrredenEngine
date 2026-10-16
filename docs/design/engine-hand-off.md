# Engine hand-off — suspend, run a child, resume

`IREngine.handOff` lets a running creation hand the machine to a second run of
an engine executable and come back when it exits. An editor's "take" button is
the motivating caller: write the configuration, launch the same executable in
its take mode (its own window layout, resolution and recording), and resume the
editor afterwards. Lua surface summary: [`engine/script/CLAUDE.md`](../../engine/script/CLAUDE.md)
§"Engine service bindings".

## The call

```lua
local r = IREngine.handOff{ argv = { "IRMyCreation", "--auto-record", "600" }, resume = true }
-- r.ok, r.supported, r.exitCode (set once the child started), r.error
```

- `argv` is a non-empty dense array of strings: the child's `ir-run`
  arguments, executable name first. Each element reaches the child as one
  argument; nothing is joined into a shell command.
- `resume` defaults to `true`; `false` raises. Replacing the parent for good
  is a different lifecycle, not an alternate meaning of this call.
- A non-table argument, a missing / empty / sparse / non-string `argv`, an
  element holding a NUL byte (the OS would truncate it), a non-boolean
  `resume`, and a call off the main thread all raise before anything is
  drained or released. The C++ `runHandOff` refuses a NUL byte the same way.
- `ok` is true only when the child started and exited 0. A spawn failure or a
  non-zero exit returns `supported = true`, `ok = false`, the reading in
  `error` / `exitCode`, and the parent resumes.
- C++ callers use `IREngine::handOff(HandOffRequest)` (`ir_engine.hpp`).

## The sequence

`runHandOff` (`engine/world/src/hand_off.cpp`) runs a fixed order:

1. validate the request and the wrapper capability;
2. drain the GPU — `RenderDevice::finish()` (`glFinish`; Metal commits and
   waits on the frame command buffer);
3. release the `ir-run` gpu lease when this run holds one;
4. launch `ir-run <argv...>` and block in the OS wait (`posix_spawn` +
   `waitpid`, or `CreateProcessW` + `WaitForSingleObject`), stdio inherited so
   the child's `ir-run: RESULT=` line lands in the parent's log;
5. reacquire the lease;
6. re-baseline the clock (`TimeManager::resumeAfterSuspend`) and return.

Drain precedes release on every path: a release followed by a drain would let
the child link shaders while the parent still has GPU work queued, the
cross-process driver stall the gpu lock exists to prevent
([`.claude/rules/engine-process-launches.md`](../../.claude/rules/engine-process-launches.md)).

The call is synchronous on the main thread, so no update or render runs while
it blocks; the parent keeps its window and GL / Metal context, and the last
presented frame stays on screen. Input aimed at the parent during the wait is
queued by the OS and delivered when the loop resumes.

**Lease lost.** If the reacquire fails (its queue timeout, or the owning
`ir-acquire` died), the parent must not render without the lock: `World`
records the loss, `runMainLoopFrame` runs no further update or render, and
`gameLoop` leaves through `end()`.

**Clock.** Without the re-baseline the whole hand-off interval would arrive
as UPDATE lag (a clamped catch-up burst) and as one RENDER delta. The resume
drops pending catch-up ticks to the one in flight and restarts both marks.

## The wrapper side

`ir-run` exports what the engine needs; without it the call is inert:

| Variable | Set by | Meaning |
|---|---|---|
| `IR_RUN_WRAPPER` | `ir-run` | canonical wrapper path the child launches through |
| `IR_RUN_BASH` | `ir-run`, native Windows | the bash that runs the extensionless wrapper |
| `IRREDEN_BUILD_DIR` | `ir-run` | the build tree the child resolves its executable in |
| `IR_RUN_LOCK_TOKEN`, `IR_ACQUIRE_TOOL` | `ir-acquire gpu` / `benchmark` | the gpu lease and the tool that lends it |

The gpu lock belongs to the `ir-acquire` shell, not to the engine, so the
engine lends it through `ir-acquire --lease-release <token>` and takes it back
with `ir-acquire --lease-reacquire <token>`, each a short-lived control process.
The lease record (`$IR_LOCK_ROOT/leases/<token>/`) names the owner; every
transition re-verifies that owner is alive and restamps the lock to it, never to
the control process. Double transitions, an unknown token and a dead owner are
refused without touching the lock. The owner's exit trap takes the same mutex
that serializes the transitions before it drops the lease and releases its
locks, so a transition already past its checks finishes against a live owner
and cannot free a lock a contender took after the owner left. Once released, the parent's
`IR_ACQUIRE_HOLDER_PID` no longer owns `gpu/lock`, so the nested `ir-run` queues
on its own lock instead of riding the inherited hold. `ir-run` passes a token
only to a run its lock covers.

A run with no gpu lease (macOS interactive runs, `IR_RUN_GL_EXCLUSIVE=0`) still
drains and launches through the wrapper. Outside `ir-run` the call logs one
notice and returns `ok = false, supported = false` with no drain, no child, no
lease change.

## Constraints

- **Only the gpu lease is lent.** A benchmark parent keeps its cpu and perf
  locks, so a child argv carrying `--auto-profile` is refused before anything
  is released (it would queue on the parent's own locks).
- **Audio and MIDI stay open.** The parent keeps its audio stream and RtMidi
  ports while handed off. WinMM MIDI ports open in one process at a time on
  Windows, so a child that opens the same port fails to open it.
- **Native Windows ghosting.** A window that stops pumping messages for 5 s is
  marked "(Not Responding)" and DWM swaps it for a ghost whose close ends the
  process (`ir-run` reports that as `RESULT=HOST-CLOSED`). The launcher calls
  `DisableProcessWindowsGhosting()` before the wait, so the last frame stays
  and no close prompt appears. macOS shows the beachball over the parent; it is
  cosmetic.
- **Window mode under the fleet.** `fleet-run` marks a run unattended, so
  `ir-run` defaults it to `background` unless `--window-mode normal` follows the
  executable. The child inherits that marker and the parent's
  `IR_WINDOW_MODE`; pass `--window-mode` in the child `argv` to choose its mode.

## Validators

- `ctest --test-dir build -R HandOff` — the controller sequence and failure
  arms with a stub platform (`test/world/hand_off_test.cpp`), the frame seam,
  the Lua binding (`test/script/lua_engine_hand_off_test.cpp`) and the clock
  re-baseline (`test/time/time_manager_hand_off_test.cpp`).
- `bash scripts/fleet/tests/run_all.sh --only ir_run_handoff_lock` — the real
  wrapper lending its lock to one nested `ir-run` and taking it back.
- `fleet-run --timeout 120 IRLuaWidgetsDemo --hand-off-fixture auto` — the
  round trip end to end: `LUA_WIDGETS HANDOFF before` and `resumed` print equal
  `frame=` / `tick=` readings, the child's verdict, then the parent's.

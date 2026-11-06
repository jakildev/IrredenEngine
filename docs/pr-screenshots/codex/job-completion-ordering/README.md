# Task-completion ordering on macOS ARM

Release editor profiling exposed intermittent hangs during ordinary automatic-worker transform propagation. A native sample showed the main thread waiting for an enki task to complete while the sampled worker threads were waiting for new tasks. Both sampled and accurate lighting modes timed out; changing the rendering path did not establish isolation.

The pinned enkiTS v1.11 source lacks the fixes confirmed in [upstream issue 91](https://github.com/dougbinks/enkiTS/issues/91). This change backports [845b837](https://github.com/dougbinks/enkiTS/commit/845b837b6a5bb6f0d36d0e594610ba4dad0b81e1) and [d1752d0](https://github.com/dougbinks/enkiTS/commit/d1752d0898f4b1aa64a60944aaa9f6bf4b7ebfa4): four waiter-counter read-modify-writes and six task-running-count decrements use acquire-release ordering. Scheduling, task partitioning, worker counts and blocking semaphores remain unchanged.

The dependency wrapper checks actual source on each configure. It recognizes a complete existing patch, atomically applies a clean patch, and fails on partial/conflicting state. Custom revision overrides remain the caller's responsibility, as documented in the dependency VERSION file. The hash-checked default archive and custom `IR_ENKITS_GIT_REF` transport match the dependency changes in [PR 4880](https://github.com/jakildev/IrredenEngine/pull/4880), without its unrelated build changes. The 37-commit revision upgrade would also change partitioning, work stealing and shutdown behavior, so the backport keeps this correction narrow.

## Validation

- Seven offline patch/archive/wrapper tests pass. Mutations removing a fix, skipping application, allowing partial application, or sharing custom revision directories fail their intended checks. The final custom-directory mutation fails isolated-source verification; the earlier Git-based transport's stash-conflict log is historical evidence, not the final mutation result.
- Final Debug editor/test and Release editor builds pass. The final 33 focused job tests pass 20 repetitions (660 checks), including 40,000 short dispatches with asserted worker participation and checked data visibility after every join.
- Both previously hanging editor scenarios pass three default-worker repetitions: sampled 67°/zoom 8 with stage timing off (360 frames), and default-accurate 30°/zoom 4 with stage timing on (120 frames). No timeout or diagnostic stack capture fired. The final Release binary hash matches these controls.
- The fresh guarded editor matrix completes all 24 runs × 360 frames = 8,640 frames, with outer exit zero, `GUARDED`, and `qualified: true`. Pose, route, artifact identity, frame coverage and overflow checks pass. The earlier failed matrix remains recorded with the parent PR; it is not pooled into this result.
- The native lighting regression gate passes all checks after the dependency correction. The final Box/Line GUI replay passes 25/25 assertions with zero layout findings; all 25 full RGB frames are identical to the pre-backport capture.

The final archive-compatible Debug and Release builds pass. Both fetched scheduler files match the previously tested source hashes; the final Release editor is byte-identical to the measured binary (`39f79be0dd503f007490556d2c727342da11624fd544c0bcd7eac60590836b13`). The timing and failure-reproduction controls therefore still apply to the final executable.

## Editor performance verdict

On this Apple M4 Max/Metal Release fixture with 219 authored cells, stage-timing-off GPU-envelope means change from 5.004 to 5.160 ms at 30°/zoom 4 and from 5.133 to 5.241 ms at 67°/zoom 8 when enabling accurate lighting. These are observed increases of 0.156 and 0.108 ms. Steady wall means increase by 2.110 and 2.303 ms, but three-run variation is large and stage instrumentation changes the direction of one comparison. This supports an editor-only quality default with a modest measured GPU difference; it is not a stable FPS claim or a 60 Hz guarantee. The matrix used AC power throughout.

[Detailed paired performance analysis](editor-performance.md) includes p95 and instrumentation controls. Raw reports, identities and qualification are in [editor-perf-postfix](editor-perf-postfix/). Job repetitions and prior-failure controls are in [job-postfix-tests-final](job-postfix-tests-final/) and [editor-postfix-controls](editor-postfix-controls/).

Passing stress and native repetitions support this fix for the reproduced workload; they do not prove absence of every scheduler race. The upstream memory-order correction supplies the rationale, while local tests check integration, publication after completion, and the previously failing application paths.

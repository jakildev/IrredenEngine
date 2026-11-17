# Event-cursor pick prototype

This is functional evidence for an **unqualified prototype**, not a performance
acceptance result. The mechanism is described in
[the pick contract](../../design/pick-frame-contract.md). Authored with Codex
(GPT-6), on Apple M4 Max, macOS, Metal, Debug, 2026-11-17.

The parent renderer is `e5c0ce71386f4cad2a3e65d24c6b395caa05ab4d`.
For the cursor positive control, the new ShapeDebug fixture was compiled against
that renderer. The head uses the completed-image implementation. Logs retain
the exact invocation, display witness, assertions and terminal job result.

| Check | Parent | Prototype |
|---|---|---|
| Cardinal MOVE A to B + PRESS, same batch | Click/hover A (488), stale=true, two stale input ticks | Click/hover B (491), stale=false, zero stale input ticks |
| Non-cardinal yaw 0.35, same batch | Click/hover A (488), stale=true, two stale input ticks | Click/hover B (491), stale=false, zero stale input ticks |
| ShapeDebug GUI | Positive control fails the two new predicates at each yaw | 56/56 assertions pass |
| Previous-completed-frame fault | Not repeated on parent | Frame-current assertions fail at 0/30; 54/56 overall |
| Rotation owner | Not repeated on parent | 8/8 assertions pass |
| Detached hitbox padding band | Not repeated on parent | 1/1; hovered=439, expected=439 |
| Native `PresentedPick*` | Existing parent suite retained | 6/6 tests pass |
| Editor `face_pick` | 12/14; both `pick_matches_render` probes read texels=0 | Same two failures and identical readings |

[Parent cursor](parent-cursor.txt), [head cursor](head-cursor.txt),
[fault](older-frame-fault.txt), [rotation](rotation.txt),
[parent editor](parent-editor.txt), [head editor](head-editor.txt),
[detached hitbox](detached-hitbox.txt).

The initial added rotation shot changed the default pivot's persistent offset,
making the subsequent drag-pan fixture fail on both parent and head. The head
pins the rotation shot's explicit pivot, as the existing rotation fixture does;
drag-pan then reports the expected owner twice. The parent cursor log predates
this fixture correction; its cardinal shot is unchanged. The retained cardinal
parent/head PNG pair is byte-identical. This single pair is not the full image
regression gate.

The legacy `same_batch_move_press_is_stale` assertion is inverted to
`same_batch_move_press_is_current`. The click callback separately records the
stale flag and owner at dispatch, so settled frames cannot hide a stale click.
The GUI counter reads the same counter used by the profile report; the report's
row has not been qualified in a performance run here.

## Outstanding acceptance gates

- The editor's two zero-texel probes reproduce the failure tracked in
  [engine issue 5133](https://github.com/jakildev/IrredenEngine/issues/5133).
  The task's criterion 7 still requires a pass; this prototype does not claim it.
- No guarded five-pair Release timing result was obtained. The initial million
  preflight waited inside the benchmark runner and was interrupted with exit
  130 before producing a report. Latency, steady frame cost, GPU command-buffer
  spans and the alternative one-pixel re-query's delay remain unmeasured.
- The broad ShapeDebug render-verify run was cancelled with terminal exit 143
  while processing extra runs. It is incomplete, not a pass. CanvasStress and
  PerfGrid render verification and the nine-yaw parent/head comparison remain
  outstanding. Inherited ShapeDebug references are tracked in
  [engine issue 4904](https://github.com/jakildev/IrredenEngine/issues/4904).
- OpenGL execution remains a cross-host residual. GLSL/MSL counterpart audit,
  header/layout/kernel-registry checks and comment-reference lint passed.

## Wrapper rejection

The optional export command was rejected before a job started:

```text
fleet-jobs start render-verify -- --target IRShapeDebug --no-build --json-out /private/tmp/pick-5002-shape-render.json
fleet-jobs: --json-out value '/private/tmp/pick-5002-shape-render.json' is not allowed
```

The standard invocation without the optional export was accepted. That is the
subsequently cancelled job described above; no JSON result is claimed.

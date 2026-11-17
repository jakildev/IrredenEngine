# Event-cursor pick prototype

This is evidence for an **unqualified prototype** whose whole-frame mechanism
is now rejected by the locked performance criteria. The mechanism is described in
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
| Editor `face_pick`, default view | 12/14; both `pick_matches_render` probes read texels=0 | Same two failures and identical readings |
| Editor legacy controls | Not repeated on parent | `face_pick` 14/14; `part_gizmo_reach` 24/24 |
| Million Release performance | Five paired controls | Direct-ring head: frame +0.292 ms, GPU +0.271 ms, input-to-present mean +0.290 ms; criteria 4 and 5 fail |

[Parent cursor](parent-cursor.txt), [head cursor](head-cursor.txt),
[fault](older-frame-fault.txt), [rotation](rotation.txt),
[parent editor](parent-editor.txt), [head editor](head-editor.txt),
[editor correction matrix](editor-pick-matrix.txt), [performance](performance.txt),
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

## Locked-gate result and outstanding gates

- The corrected criterion 7 controls pass: legacy-view `face_pick` is 14/14 and
  `part_gizmo_reach` is 24/24. Head and parent default view remain byte-for-byte
  identical at 12/14, with the two zero-texel failures tracked in open issue
  [#5133](https://github.com/jakildev/IrredenEngine/issues/5133).
- Five guarded Release pairs reject the whole-frame mechanism. Even after
  removing its full-image copy, mean input-to-present regresses 0.290 ms against
  a 0.118 ms bound, frame mean regresses 0.292 ms against a 0.160 ms bound, and
  GPU span regresses 0.271 ms against a 0.009 ms same-matrix parent spread.
  Criteria 4 and 5 fail; selecting another mechanism requires design direction.
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

# Event-cursor pick evidence

This directory records the functional and performance qualification for event-cursor picking.
The selected implementation keeps the normal 32-byte hover record and performs a completion-gated
GPU re-query only when the INPUT cursor differs from the rendered cursor. The rejected predecessor
retained a 32-byte record for every framebuffer pixel. The mechanism and completion contract are
described in [the pick contract](../../design/pick-frame-contract.md).

Authored with Codex (GPT-5), on Apple M4 Max, macOS, Metal, Debug unless a row states Release.
The parent renderer is `e5c0ce71386f4cad2a3e65d24c6b395caa05ab4d`.

| Check | Parent | Selected re-query head |
|---|---|---|
| Cardinal MOVE A to B + PRESS, same batch | Click/hover A (488), stale=true, two stale input ticks | Click/hover B (491), stale=false, zero stale input ticks |
| Non-cardinal yaw 0.35, same batch | Click/hover A (488), stale=true, two stale input ticks | Click/hover B (491), stale=false, zero stale input ticks |
| ShapeDebug GUI | Positive control fails the two new predicates at each yaw | 56/56 assertions pass |
| Query request-to-dispatch | Not applicable | 7.9–9.3 ms in the GUI run |
| Previous-completed-frame fault | Not repeated on parent | Frame-current assertions fail by construction |
| Rotation owner | Not repeated on parent | 8/8 assertions pass |
| Detached hitbox padding band | Not repeated on parent | 1/1; hovered=439, expected=439 |
| Native `PresentedPick*` | Existing parent suite retained | Focused suite passes |
| Editor `face_pick`, default view | 12/14; both `pick_matches_render` probes read texels=0 | Same two failures and identical readings |
| Editor legacy controls | Not repeated on parent | `face_pick` 14/14; `part_gizmo_reach` 24/24 |
| Rejected whole-frame Release performance | Five paired controls | Frame +0.292 ms, GPU +0.271 ms, input-to-present +0.290 ms; rejected |
| Selected re-query Release performance | Five interleaved pairs in each of three baseline scenes | Criterion 4 passes all scenes; million frame +0.004 ms and GPU span +0.002 ms, criterion 5 passes |

[Parent cursor](parent-cursor.txt), [head cursor](head-cursor.txt),
[fault](older-frame-fault.txt), [rotation](rotation.txt),
[parent editor](parent-editor.txt), [head editor](head-editor.txt),
[editor correction matrix](editor-pick-matrix.txt), [performance](performance.txt),
[detached hitbox](detached-hitbox.txt).

The legacy `same_batch_move_press_is_stale` assertion is inverted to
`same_batch_move_press_is_current`. The click callback records the stale flag and owner at dispatch,
and the GUI counter reads the profile report's stale-input counter. The cardinal and rotated
same-batch shots both select B; settled frames cannot hide a stale click.

The default editor's two zero-texel failures are identical on parent and head and tracked in
[#5133](https://github.com/jakildev/IrredenEngine/issues/5133). OpenGL execution remains a
cross-host residual; GLSL/MSL parity, layout and kernel-registry validators cover the backend
contract on this host.

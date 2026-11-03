# Legacy shadow cleanup regression evidence

Baseline: `a82a66f2bcd12f6987c90d3a111bf8fb398a57dc` (parent PR).
After: this change, using freshly staged shader assets. The baseline executable
and all six changed staged shader files were verified against the parent before
staging the cleanup. `comparison.json` records both staged shader hashes, commands,
actual camera state and every PNG hash.

Host: macOS, Metal, 2048 × 1152 offscreen framebuffer. All six wrapper runs exited
with `RESULT=CLEAN`. The initial sandboxed attempt stalled in GLFW window creation;
it produced no accepted captures and was replaced by the successful native run.

| Mode | Paired frames | Exact PNG matches |
|---|---:|---:|
| Legacy beauty | 12 | 12 |
| Legacy shadow diagnostic | 12 | 12 |
| Default finite-coverage shadow diagnostic | 12 | 12 |

Each run used:

```text
fleet-run IRCanvasStress --auto-screenshot 120 --no-auto-rotate --no-spin
```

Legacy runs append `--legacy-depth-shadows`; diagnostic runs append
`--debug-overlay shadow`. The shot sequence sets camera yaw to 0°, 45° and 30°.
Actual effective subdivisions are 1 or 2; requested fractional zooms snap to the
logged effective zoom. The main scene includes world-placed detached casters.

## Representative full frames

| Control | Before | After |
|---|---|---|
| Legacy beauty, yaw 45° | [Before](legacy-beauty-1-before.png) | [After](legacy-beauty-1-after.png) |
| Legacy beauty, floor/cardinal | [Before](legacy-beauty-8-before.png) | [After](legacy-beauty-8-after.png) |
| Legacy shadow diagnostic, yaw 30° | [Before](legacy-shadow-4-before.png) | [After](legacy-shadow-4-after.png) |
| Default shadow diagnostic, yaw 30° | [Before](modern-shadow-4-before.png) | [After](modern-shadow-4-after.png) |

The comparison is a behavior-preservation check, not an oracle for every rendered
shape. Legacy point-scatter holes and inherited surface artifacts are unchanged.
No blur, shadow coverage adjustment, buffer-layout change or performance improvement
is claimed. Full local captures and logs remain under
`/tmp/codex-shadow-cleanup-captures/{baseline,after}`; selected log lines and all
frame hashes are committed here. No temporal claim is made from these stills.

## Executed contracts

`test_render_legacy_shadow_bake.py` exercises 256 CPU route/configuration cases,
540 shader cases per backend, and 21 failing mutation controls. It extracts live
production reset/restore snippets, bake dispatch orchestration and both shader
entry bodies. The adapters record calls and emulate frame writes; geometry
reconstruction, projection and actual GPU binding/ABI behavior are not reproduced.
The native runs complement those controls.

Existing source-face lighting and visibility-routing executable tests pass. The
source-face test covers the shared shadow palette alongside deferred lighting;
the visibility test also exercises the consolidated brace-extraction helper.

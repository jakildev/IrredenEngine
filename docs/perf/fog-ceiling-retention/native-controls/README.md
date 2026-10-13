# Native ceiling controls

The rebuilt `IrredenEngineTest` at `50c685205` ran `FogCrossSection*:FogRevealEval*:LuaFog*`: 125 passed, one OpenGL-only test skipped, CLEAN exit 0. Build job `20261013T233641Z-build-fd4089` has a terminal `succeeded exit=0` wait. The log is adjacent. Native Metal demo captures are recorded in `metal-captures.json`; production runs all finish CLEAN. The grid-placeholder mutation is an expected failing control, never a passing render run.

## Production C++ mutation checks

The disposable harness in `control.cpp.txt` includes the actual production `fog_of_war.hpp`, with its component header copied into a temporary include overlay. It uses the native test compile flags and links the existing `IrredenEngineMath` library (recorded in `compile-args.json`). `run.py.txt` mutates one production expression at a time, compiles the complete harness, and requires the named runtime assertion to fail. Compile errors are not positive controls. The production run passes; all 18 compiled mutants fail as intended. The original header is restored in a `finally` block and was checked byte-identical to the repository afterward. These files are evidence for this run, not an additional maintained test target.

| Mutation | Firing assertion |
|---|---|
| `uncovered` | `FAIL uncovered` |
| `visible` | `FAIL visible` |
| `explored` | `FAIL explored` |
| `maximum` | `FAIL source_maximum` |
| `own_softness` | `FAIL aa_admission` |
| `aa_too_small` | `FAIL aa_pixel_bound` |
| `height_cost` | `FAIL height_and_los_ignored` |
| `los_rejection` | `FAIL height_and_los_ignored` |
| `channels` | `FAIL source_channels` |
| `density` | `FAIL zero_density` |
| `phase_zero` | `FAIL phase_zero` |
| `phase_span` | `FAIL phase_bound` |
| `phase_removed` | `FAIL phase_revives` |
| `shadow_phase` | `FAIL shadow_static` |
| `body` | `FAIL shadow_body` |
| `voxel_paint` | `FAIL voxel_ceiling_paint` |
| `analytic_paint` | `FAIL analytic_ceiling_paint` |
| `los_paint` | `FAIL voxel_los_paint` |

The height-cost fixture uses an enabled ceiling. An earlier trial with a disabled ceiling did not detect an added height cost; that ineffective trial was discarded and the fixture corrected before the final production/mutation run.

## Native Metal controls

- Soft and hard ceiling runs: CPU-retained top equals the carrier top (8 and 5 respectively), `aboveCut=0`, positive cap counts and `hollow=0`; the BODY twin keeps 272 carrier texels, including 192 above the ceiling, with ratio 1.
- Treatment phase 0.37: `changed=76`, `outsideBand=0`. Both phase-0 PNGs equal both omitted-phase PNGs byte-for-byte. Replacing the entire Metal `fogCeilingPhaseHeight` body with `return 0.0f;` in an isolated runtime shader copy produces the same two PNGs byte-for-byte.
- Column-top: `bandTexels=108`, `bandOracleHidden=0`, `bandPainted=0`; `controlTexels=0`, `controlCap=72`, `controlHollow=0`.
- Static shadow: centre/rim/control reach 19.690, BODY 20.996; treatment centre 19.690/19.690 and rim 19.690/19.309, within sun texel 0.463.
- Live grid: centre reach 31.765/31.765. Replacing the Metal `fogShadowCell` read with `uint2(0u, kFogChannelDefault)` in an isolated runtime copy gives 19.690/31.765 and triggers `FOG-SHADOW-PROBE FAIL: treated shadow differs from the CPU-retained solid` (exit 1).
- Curved caster: FIELD 20.996, BODY 22.845; gap 1.849 exceeds minimum 1.149, receiver 39.000/30.841.
- Lua self-test: `LUA-FOG-CAP`, `LUA-FOG-SETUP`, and `LUA-FOG-PROBE` report PASS.

Native executable was built at `f4fd403e` (build job `20261013T225309Z-build-4fcb8f`, terminal success); its only runtime difference at measured `50c685205` is the copied GLSL/Metal face-selector BODY predicate. Subsequent native test and full render builds rebuild the ordinary targets at the final source. The isolated phase-less and placeholder variants never modify the source checkout.

Authored and executed by Codex (GPT-6), macOS / Apple M4 Max / Metal, 2026-10-13. OpenGL remains a separate host check.

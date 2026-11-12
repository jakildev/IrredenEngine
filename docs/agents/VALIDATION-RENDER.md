# VALIDATION-RENDER.md — render gates

Part of the [validation index](VALIDATION.md); its rules for citing a
validator apply. Open it for a change to the render pipeline, a shader, the
cull, or a creation's committed reference shots, and before adding a harness
import or a default-off render feature. A row added here also adds its name to
this page's line in the index's area table.

| Validator | Proves | Run | CI |
|---|---|---|---|
| Render regression | a demo or editor's shots match committed references; manifests are discovered under `creations/demos/` and `creations/editors/`; a master-red row the PR did not move is graded per [AUTHOR-PIPELINE.md](AUTHOR-PIPELINE.md) § "Acceptance evidence"; SDF-silhouette movers use the [confinement rule](../design/sdf-receiver-geometry.md#sdf-silhouette-reference-movers-and-confinement); under `--all` a manifest's `sweep_backends` names the presets it gates — an undeclared preset reads `OUT-OF-SCOPE` (not run, non-failing), a declared one with no reference set reads `ERROR` | `render-verify` skill; `python3 scripts/render-verify.py --target <Creation>` (ignores `sweep_backends`; also the only path that blesses a new preset's set); `python3 scripts/render-verify.py --all` | render-harness-tests.yml tests the harness itself |
| Lighting regression | lighting-demo domain assertions and committed images remain valid after any non-Markdown engine, lighting-demo, render-harness, or build-system change; the author-side executor and inherited-row policy are in [AUTHOR-PIPELINE.md](AUTHOR-PIPELINE.md) § "Build and run" | `python3 scripts/light-verify.py --if-changed` (needs a display; image baselines currently exist for `macos-debug`) | render-harness-tests.yml tests the trigger and harness hermetically |
| Shader cold compile | every top-level GLSL program compiles and links cold (unique nonce, so the driver's disk cache misses) within the per-program budget, default 5 s | `python3 scripts/shader-compile-verify.py [--budget <s>] [<program>...]` (native Windows only; no other engine GL context live) | render-harness-tests.yml tests selection, include resolution and nonce placement hermetically |
| Shader counterpart parity | every top-level GLSL file has its Metal counterpart and every Metal file its GLSL one (`c_`/`ir_` by name, `v_`/`f_`/`g_` to a bare `.metal`), or a reasoned entry in `parity_allowlist.json`; a stale entry fails | `python3 scripts/shader_parity_audit.py` | shader-parity.yml on `engine/render/src/shaders/**` PRs and master pushes; its suite runs in render-harness-tests.yml |
| Cull regression | the live cull drops no on-screen content, and the freeze that check rests on is actually engaged | `python3 scripts/cull-verify.py` (needs a GL/Metal host) | none for the harness; render-harness-tests.yml runs its freeze-guard assertion arms hermetically |
| Per-voxel occlusion cull | identity: cull-off, chunk+per-voxel and chunk-only render byte-identical to the `perf_grid` reference set; fire: the per-voxel refine still reduces the visible count | `python3 scripts/render-verify.py --target IRPerfGrid` (21 checks) and `python3 scripts/occlusion-fire-verify.py` — both, never one alone (needs a GL/Metal host) | render-harness-tests.yml runs the fire gate's marginal / exit-code arms and the manifest `demo_args` threading hermetically |
| Render metrics | shadow, silhouette, coverage, jitter, clip, AO staircase, depth-tier, feeder-margin, pivot readings | `python3 scripts/<metric>-verify.py` / `scripts/render-*-metric.py` | none |

## Harness dependency contract

The render harness under `scripts/` is stdlib-only. The exception tier is the
files listed in `scripts/tests/test_harness_dependency_contract.py`, which may
import `PIL` and nothing else third-party; that suite is the list of record and
derives the real importer set from the source, so an unregistered dependency
fails there. `render-verify`, the comparator, `verify_common`, every
`*-verify.py`, and every metric a committed manifest names are never eligible —
the gating path must run on a bare host. A host without Pillow runs the
exception tier with `python3 -m pip install pillow`; CI pins the version in
`render-harness-tests.yml`. Shader contract suites also require `c++`: a host
without it reports the suite as skipped, and the CI workflow rejects that
result. `run_all.sh` prints each suite's test count, so a suite that skipped or
never ran reads differently from one that passed.

## Default-off features need a positive enabled-path test

Render features routinely default OFF (priority 0, a mode flag off, an opt-in
branch) to preserve byte-identity — but byte-identity at default only proves the
OFF path is a no-op, never that the feature works. Author a test that
exercises the **ENABLED** path (a `--depth-probe`/`-assert` reading, a demo shot
with the flag ON) and confirms the effect end-to-end (CPU author → GPU upload →
shader output). A CPU-authored field uploaded only on a specific path (the
per-frame binding-6 voxel upload, not a detached-revoxelize bake) can silently
never reach the shader, and a "compiles + byte-identical at default" merge ships
a feature that does not function in its actual use case.

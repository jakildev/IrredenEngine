# VALIDATION.md — what proves a change

The index of every validator the repo runs. An issue's `**Acceptance
criteria**` (its definition of done) and a plan's `### Acceptance criteria`
name validators from this table and the reading each must show; a PR's
`## Acceptance evidence` records the run. A criterion no validator can
prove is not done until one exists.

| Validator | Proves | Run | CI |
|---|---|---|---|
| Build | the touched target compiles on this host's preset | `fleet-build --target <target>` (`ir-build` canonical; see BUILD.md) | perf-gate.yml builds the perf demos |
| Unit tests | engine behaviour under `test/` | `ctest --test-dir build -R <pattern>` after building `IrredenEngineTest` | none today |
| Fleet tool tests | fleet scripts and workflows, hermetically | `bash scripts/fleet/tests/run_all.sh [--only <substring>]` | fleet-tests.yml on `scripts/**` |
| Positive control | a new fleet suite would have failed before its fix | `fleet-positive-control <test-file> <pre-fix-ref>` | none (authoring-time) |
| Fleet-test subjects | every out-of-tree file a fleet suite tests is registered in `OUT_OF_TREE_SUBJECTS` and in both `fleet-tests.yml` `paths:` blocks | `python3 scripts/fleet/fleet_test_subjects.py` | fleet-tests.yml |
| Header conventions | header-global ban, anonymous namespaces, `*Detail` namespaces, Metal registries, GLSL reserved words | `cmake -DPROJECT_ROOT=$PWD -P cmake/run_header_checks_standalone.cmake` | header-checks.yml |
| Python lint | ruff rules over `scripts/` | `ruff check scripts/` | python-lint.yml |
| Comment refs | no issue/PR numbers gained in code comments | `python3 scripts/lint_comment_refs.py [--against <ref>]` | comment-refs.yml on every push and PR, measured against the tree it was built on (PR: first parent; push: previous tip) — red only on an introduced offender |
| Instruction size | no agent-facing instruction file grew past its budget; an introduced excess of up to 10 lines is a `::warning` annotation, more fails; budget raises are printed | `python3 scripts/lint_instruction_size.py [--against <ref>] [--warn-band <lines>]` | instruction-size.yml on every push and PR, measured against the tree it was built on (PR: first parent; push: previous tip) |
| Format | clang-format on the branch's changed lines | `fleet-build --target format-changed`; no-configure path `cmake -DPROJECT_ROOT=$PWD -DCLANG_FORMAT_BIN=<bin> [-DFORMAT_DIFF_BASE=<commit>] -P cmake/run_clang_format_changed_standalone.cmake` | format-check.yml (changed lines only, clang-format pinned) |
| Render regression | a demo's shots match committed references; a master-red row the PR did not move is graded per [AUTHOR-PIPELINE.md](AUTHOR-PIPELINE.md) § "Acceptance evidence" | `render-verify` skill; `python3 scripts/render-verify.py --target <Demo>` | render-harness-tests.yml tests the harness itself |
| Shader cold compile | every top-level GLSL program compiles and links cold (unique nonce, so the driver's disk cache misses) within the per-program budget, default 5 s | `python3 scripts/shader-compile-verify.py [--budget <s>] [<program>...]` (native Windows only; no other engine GL context live) | render-harness-tests.yml tests selection, include resolution and nonce placement hermetically |
| SDF surface contract | GLSL and Metal box-interval helpers agree with a double-precision ray oracle across yaw and density; stored-depth and slot aliases remain distinct surfaces | `python3 scripts/tests/test_render_sdf_surface_contract.py` (needs `c++`) | render-harness-tests.yml on `scripts/tests/**` and `engine/render/src/shaders/**` |
| Per-axis dispatch contract | both shader backends flatten multirow occupied-cell dispatches and decode row-major pixels consistently with CPU layout constants; lost-row/wrong-column mutations fail | `python3 scripts/tests/test_render_per_axis_cell_dispatch.py` (needs `c++`; integer adapter, not GPU compilation) | render-harness-tests.yml; also triggered by `ir_render_types.hpp` |
| GUI behaviour | GUI-ASSERT shots pass for a creation | `gui-verify` skill; `python3 scripts/gui-verify.py <Creation>` | none |
| Cull regression | the live cull drops no on-screen content, and the freeze that check rests on is actually engaged | `python3 scripts/cull-verify.py` (needs a GL/Metal host) | none for the harness; render-harness-tests.yml runs its freeze-guard assertion arms hermetically |
| Per-voxel occlusion cull | identity: cull-off, chunk+per-voxel and chunk-only render byte-identical to the `perf_grid` reference set; fire: the per-voxel refine still reduces the visible count | `python3 scripts/render-verify.py --target IRPerfGrid` (21 checks) and `python3 scripts/occlusion-fire-verify.py` — both, never one alone (needs a GL/Metal host) | render-harness-tests.yml runs the fire gate's marginal / exit-code arms and the manifest `demo_args` threading hermetically |
| Render metrics | shadow, silhouette, coverage, jitter, clip, AO staircase, depth-tier, feeder-margin, light, pivot readings | `python3 scripts/<metric>-verify.py` / `scripts/render-*-metric.py` | none |
| Face over-brightness | pixels in a face-colour hue class stay at or below a named RGB lighting ceiling; a capture with no matching hue pixels is invalid | `python3 scripts/render-face-overbright-metric.py --ceiling <R,G,B> <captures...>`; the PASS reading is `overbright_px=0`, `hue_px` greater than zero for every file, and `RESULT=PASS` | render-harness-tests.yml tests the validator; native captures run manually |
| Floor continuity | the shape_debug floor's frame share moves by less than `--max-step-pp` (default 2.0) between consecutive shots of the deterministic zoom-16 yaw sweep, 290–336° by default: no cull or dispatch clip drops a band of floor | `fleet-run IRShapeDebug --spin-yaw --zoom 16 --auto-screenshot 720` into an emptied screenshot dir, then `python3 scripts/render-floor-continuity-metric.py <screenshot-dir>`; the PASS reading is `frames_examined` 93 and `max_step_pp` below the threshold; recipe in script help | render-harness-tests.yml tests the validator; native captures run manually |
| Rotated-view coverage | the rotated-view face store holds everything on screen: no background in any shot of the `IRZYawCoverage` sweep — a floor wider than the view, far from the world origin, from every yaw quadrant under each pivot | `fleet-run IRZYawCoverage --config-preset configs/coverage_portrait.lua --auto-screenshot 8` into an emptied screenshot dir, then `python3 scripts/render-view-coverage-metric.py <screenshot-dir>`; repeat with `configs/coverage_landscape.lua`; the PASS reading is `frames_examined` 19 and `max_background_pct` 0.0; recipe in script help | render-harness-tests.yml tests the validator; native captures run manually |
| Visible box geometry | authored silhouette and centroid; optional placement-only check | `python3 scripts/render-visible-box-metric.py <capture.png> --yaw <degrees>`; fixed capture recipe in script help | none |
| Source-face fidelity | independent projected silhouette; single-voxel face ownership with `--normals` | `python3 scripts/render-source-face-metric.py <capture.png> --shape <voxel\|adjacent\|frame\|octahedron> --yaw <degrees>`; [contract and controls](../design/trixel-face-reconstruction-validation.md) | render-harness-tests.yml tests the validator; native captures run manually |
| Resampled-cell face ownership and sun visibility | revoxelized detached display against its own inverse-resampled cells: silhouette and per-pixel owner face on a normals overlay, or per-trixel lattice sun visibility on a shadow overlay (`--shadow-overlay`; false shadow on a trixel whose ray grazes an occluder within `--terminator-tolerance` cells is the sun map's texel floor, not a failure) | `python3 scripts/render-revox-face-metric.py <capture.png> --fixture <cube\|grounded\|lprism> --yaw <degrees>`; [contract and evidence](../design/revoxelized-display-fidelity.md) | render-harness-tests.yml tests the validator; native captures run manually |
| Mixed private canvas lifecycle | a voxel producer and an SDF producer sharing one entity canvas: frame intact outside a marker guard, markers present and placed; `--control` for raster survival, `--strict` for the SDF footprint; `--fixture <revoxelized solid> --markers … --owner …` for the half-cell phase fixtures | `python3 scripts/render-mixed-canvas-metric.py <capture.png> --yaw <degrees> [--fixture parity --markers x y z …] [--control <marker-free.png>]`; [contract and evidence](../design/mixed-private-canvas-lifecycle.md) | render-harness-tests.yml tests the validator; native captures run manually |
| Shadow comparison presence | attached orange, detached cyan/purple and rainbow probe visibility | `python3 scripts/render-shadow-probes-metric.py <captures...>`; fixed capture recipe in script help | none |
| Voxel face shadows | analytic box projection, visible area and overlap at four cardinal yaws | `python3 scripts/render-shadow-box-metric.py <yaw0.png> <yaw90.png> <yaw180.png> <yaw270.png>`; fixed capture recipe in script help | render-harness-tests.yml runs `test_render_shadow_box_edges.py` (needs the workflow's pinned Pillow) |
| Shadow edge geometry | how straight a fully-shadowed rim is, independent of how much is shadowed: local deviation from a straight fit, tread runs, rim orientations | `python3 scripts/render-shadow-edge-metric.py <capture.png> --roi <x,y,w,h> [--window <px>]` on SHADOW debug-overlay captures | render-harness-tests.yml tests the validator |
| Detached lighting | world-sun face colors at four cardinal camera yaws | `python3 scripts/render-detached-lighting-metric.py <yaw0.png> <yaw90.png> <yaw180.png> <yaw270.png>`; capture recipe in script help | none |
| Perf gate | measured frame-time cells against the most recent capture of the head's SKU on the `perf-baseline` branch whose `ref_ms` is within 1.20× of the head's (one slug spans runner classes, #3924), raw; report-less cells fail as infrastructure errors; the gate's own resolution / exit-mapping / branch-writer logic | `bash scripts/perf/perf_grid_matrix.sh` then `scripts/perf/compare_perf_runs.py`; `python3 scripts/perf/tests/test_baseline_layouts.py`, `scripts/perf/tests/test_baseline_writer.sh`, `bash test/tools/normalization_test.sh` | perf-gate.yml |
| Perf gate (Windows ship host) | a head's frame-time cells against the hand-seeded `windows-x86_64-*` baseline on `perf-baseline`; cue-only, human- or architect-run | `ir-perf-grid --label <tag>` then `python3 scripts/perf/check_regression.py <baseline_root> <head_dir>`; refresh and compare procedure in [`scripts/perf/README.md`](../../scripts/perf/README.md) (native Windows, fleet quiet) | none (no Windows runner) |
| Plan lint | a `## Plan` comment is structurally sound | `fleet-plan-lint <issue> [--repo game]` | none (planner-time) |
| PR-body acceptance lint | each closing issue's criteria have evidence rows before publication | `fleet-pr-body-lint <issue> --body-file .pr-body.md [--repo game]` | fleet-tests.yml |
| PR closes lint | every issue the merge would close (title, every commit message raw, body code-stripped) is declared on a body link line | `fleet-pr-closes-lint --base <branch> --title <t> --body-file .pr-body.md [--repo game]`, or `--pr <N>`; 0 clean, 1 undeclared, 2 unreadable / zero commits | pr-closes-lint.yml on every push and title/body edit — not a required check: red is a needs-fix per REVIEWER-PROTOCOL.md, not a disabled merge button |
| Open-PR overlap | this branch's shared paths with every open PR are trial-merged, upstream PRs told apart by ancestry, and no conflicting competitor or stale stack base remains before publication; clean docs and instruction overlaps are quoted for semantic review | `fleet-pr-overlap --base <branch> [--repo game]` — quote its rows and `VERDICT:` line; 0 clean, 1 overlap, 3 block, 2 could not grade | fleet-tests.yml |
| Role/skill contract | every skill wrapper answers its shared flow's delta keys | `fleet-validate-roles` | fleet-tests.yml |
| Label state machine | a label transition is a declared edge | `fleet-transition <edge> <N>` | fleet-tests.yml |
| Rules sweep | a `.claude/rules/` detector over the tree without the `creations/` walker trap | `fleet-rules-sweep --pattern '<regex>' [--glob '<glob>'] [<scope>]` | none |

Rules for citing a validator:

- Name it as it appears in the first column; give the reading that proves the
  criterion fired ("`cull-verify` reports 12/12 PASS", "`ctest -R Save` adds
  `SaveTrait.InventoryIsComplete` and it passes"), never "nothing broke".
- A validator that runs only on one host (GL-only render checks, native
  Windows fleet suites) is cited with the host; the PR's acceptance evidence
  says which host ran it.
- A validator with no CI column runs at authoring time and its output line
  goes in the PR's `## Acceptance evidence`; a reviewer re-runs it.
- A criterion that needs a fixture or scene that does not exist makes
  creating it part of the task.

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

Adding a validator: one row here, a `tests/test_<name>` suite beside it if it
is a fleet tool, and a CI workflow when it must gate merges.

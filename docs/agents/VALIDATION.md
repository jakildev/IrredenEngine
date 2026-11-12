# VALIDATION.md — what proves a change

The index of every validator the repo runs. An issue's `**Acceptance
criteria**` (its definition of done) and a plan's `### Acceptance criteria`
name validators from this index and its area pages, and the reading each must
show; a PR's `## Acceptance evidence` records the run. A criterion no
validator can prove is not done until one exists.

Cross-cutting validators, which most tasks run:

| Validator | Proves | Run | CI |
|---|---|---|---|
| Build | the touched target compiles on this host's preset | `fleet-build --target <target>` (`ir-build` canonical; see BUILD.md) | perf-gate.yml builds the perf demos |
| Unit tests | engine behaviour under `test/` | `ctest --test-dir build -R <pattern>` after building `IrredenEngineTest` | engine-tests.yml (`ctest --preset linux-default-tests`) on engine, creations, test, tools and cmake changes |
| Whitespace | no whitespace error on a line the PR adds | `git diff --check origin/<base>...HEAD` | whitespace.yml on every pull request; a required check |
| No plan files | no committed plan files or PR screenshot files | none local | no-plan-files.yml on every pull request; a required check |
| Fleet tool tests | fleet scripts and workflows, hermetically | `bash scripts/fleet/tests/run_all.sh [--only <substring>]` | fleet-tests.yml on `scripts/**` |
| Positive control | a new fleet suite would have failed before its fix | `fleet-positive-control <test-file> <pre-fix-ref>` | none (authoring-time) |
| Fleet-test subjects | every out-of-tree file a fleet suite tests is registered in `OUT_OF_TREE_SUBJECTS` and in both `fleet-tests.yml` `paths:` blocks | `python3 scripts/fleet/fleet_test_subjects.py` | fleet-tests.yml |
| Header conventions | header-global ban, anonymous namespaces, `*Detail` namespaces, Metal registries, GLSL reserved words | `cmake -DPROJECT_ROOT=$PWD -P cmake/run_header_checks_standalone.cmake` | header-checks.yml |
| Python lint | ruff rules over `scripts/` | `ruff check scripts/` | python-lint.yml |
| Comment refs | no issue/PR numbers gained in code comments | `python3 scripts/lint_comment_refs.py [--against <ref>]` | comment-refs.yml on every push and PR, measured against the tree it was built on (PR: first parent; push: previous tip) — red only on an introduced offender |
| Instruction size | no agent-facing instruction file grew past its budget; an introduced excess of up to 10 lines is a `::warning` annotation, more fails; budget raises are printed | `python3 scripts/lint_instruction_size.py [--against <ref>] [--warn-band <lines>]` | instruction-size.yml on every push and PR, measured against the tree it was built on (PR: first parent; push: previous tip) |
| Instruction refs | agent-facing instruction files cite only paths, `fleet:`/`human:` labels, `C_<Name>` components and fenced `fleet-*` commands (nested `CLAUDE.md`/`AGENTS.md`) that exist; dead references already recorded in `scripts/lint_instruction_refs_baseline.json` pass, a new one fails, an entry that resolves again fails as stale; `<!-- lint: ref-ok <token> -- <reason> -->` suppresses one deliberate citation | `python3 scripts/lint_instruction_refs.py [--against <ref>] [--update-baseline]` | instruction-refs.yml on every push and PR, unfiltered, measured against the tree it was built on (PR: first parent; push: previous tip) |
| Format | clang-format on the branch's changed lines | `fleet-build --target format-changed`; no-configure path `cmake -DPROJECT_ROOT=$PWD -DCLANG_FORMAT_BIN=<bin> [-DFORMAT_DIFF_BASE=<commit>] -P cmake/run_clang_format_changed_standalone.cmake` | format-check.yml (changed lines only, clang-format pinned) |
| Perf gate | measured frame-time cells against the most recent capture of the head's SKU on the `perf-baseline` branch whose `ref_ms` is within 1.20× of the head's (one slug spans runner classes, #3924), raw except calibration-scaled frame averages for measured classes in `CLASS_MATCH_SCALE_FROM`; report-less cells fail as infrastructure errors; the gate's own resolution / exit-mapping / branch-writer logic | `bash scripts/perf/perf_grid_matrix.sh` then `scripts/perf/compare_perf_runs.py`; `python3 scripts/perf/tests/test_baseline_layouts.py`, `scripts/perf/tests/test_baseline_writer.sh`, `bash test/tools/normalization_test.sh` | perf-gate.yml |
| Perf gate (Windows ship host) | a head's frame-time cells against the hand-seeded `windows-x86_64-*` baseline on `perf-baseline`; cue-only, human- or architect-run | `ir-perf-grid --label <tag>` then `python3 scripts/perf/check_regression.py <baseline_root> <head_dir>` (`--strict-load` for the seed self-check); refresh and compare procedure in [`scripts/perf/README.md`](../../scripts/perf/README.md) (native Windows, fleet quiet) | none (no Windows runner) |
| Perf load gate | on the measured host classes only (the GPU-less hosted runners; the Windows ship host once its quiet corpus exists), a cell whose `load_ref_ms.before` (median of three samples) reaches its host class's threshold against the run floor is informational, never graded; `after` is never read; every other host, macOS included, never grades on brackets. Classes, thresholds, corpora and limits: [`scripts/perf/README.md`](../../scripts/perf/README.md) "Load gate" | `bash test/tools/normalization_test.sh` (classifier and grading), `scripts/perf/tests/test_load_bracket_capture.sh` (three-sample capture) | perf-gate.yml "Gate controls" |
| Plan lint | a `## Plan` comment is structurally sound | `fleet-plan-lint <issue> [--repo game]` | none (planner-time) |
| All-checks status | one `all-checks` commit status per pull request head: `failure` when any other check failed, `pending` while one runs or an always-run workflow has not reported, `success` otherwise; path-filtered workflows that never started are not counted; advisory, not a required context | `python3 scripts/fleet/fleet_checks_aggregate.py --repo <owner/name> --sha <head>` (posts the status; needs `gh` auth) | all-checks.yml on every workflow completion; fleet-tests.yml tests the helper |
| PR-body acceptance lint | each closing issue's criteria have evidence rows before publication | `fleet-pr-body-lint <issue> --body-file .pr-body.md [--repo game]` | fleet-tests.yml |
| PR closes lint | every issue the merge would close (title, every commit message raw, body code-stripped) is declared on a body link line | `fleet-pr-closes-lint --base <branch> --title <t> --body-file .pr-body.md [--repo game]`, or `--pr <N>`; 0 clean, 1 undeclared, 2 unreadable / zero commits | pr-closes-lint.yml on every push and title/body edit — a required check (`closes-lint`): red is a needs-fix per REVIEWER-PROTOCOL.md |
| Open-PR overlap | this branch's shared paths with every open PR are trial-merged, upstream PRs told apart by ancestry, a competitor's own conflict with the base this branch carries (`base-conflict`) told apart from one this branch adds, and no unsequenced conflicting competitor or stale stack base remains before publication; clean overlaps, `base-conflict` rows and competitors actively parked behind this branch's closing issues are quoted for semantic review | `fleet-pr-overlap --base <branch> [--repo game]` — quote its rows and `VERDICT:` line; 0 clean, 1 overlap or sequenced, 3 block, 2 could not grade | fleet-tests.yml |
| Role/skill contract | every role or skill wrapper answers its shared flow's delta keys, with non-empty role and skill lanes | `fleet-validate-roles --strict --repo-root <engine-root>` | `test_fleet_validate_roles.py` via fleet-tests.yml |
| Label state machine | a label transition is a declared edge | `fleet-transition <edge> <N>` | fleet-tests.yml |
| Rules sweep | a `.claude/rules/` detector over the tree without the `creations/` walker trap | `fleet-rules-sweep --pattern '<regex>' [--glob '<glob>'] [<scope>]` | none |

## Area pages

Every other validator lives on its area's page beside this file. Open a page
when the change touches its area, or when an issue or plan cites a validator
listed on its line.

| Page | Read when the change touches | Validators |
|---|---|---|
| [VALIDATION-RENDER.md](VALIDATION-RENDER.md) | the render pipeline, a shader, the cull, or a creation's committed reference shots | Render regression; Lighting regression; Shader cold compile; Shader counterpart parity; Cull regression; Per-voxel occlusion cull; Render metrics |
| [VALIDATION-RENDER-FEATURES.md](VALIDATION-RENDER-FEATURES.md) | one render feature's shader contract or metric (sun shadows, SDF shapes, faces, zoom, LOD, fog) | Finite sun face index coverage; Grazing sun face envelopes; Native query probe fixtures; Raster-only sun depth retention; Native Metal face index bounds; SDF surface contract; Cardinal shape-shadow depth; Per-axis dispatch contract; Fog ceiling compact admission; Face over-brightness; Floor continuity; Yaw lone-frame continuity; Rotated-view coverage; Visible box geometry; Continuous-zoom placement; Source-face fidelity; Resampled-cell face ownership and sun visibility; GRID destination cells; Mixed private canvas lifecycle; Shadow comparison presence; Authored LOD finite faces; Voxel face shadows; Shape floor-shadow footprint; Analytic sun rays; Spatial shape projection; Torus sample ownership; Sphere shadow receiver; Shadow edge geometry; Cardinal receiver edge speckles; Detached lighting; Rigid-cube receiver shadow; Source-face early depth; Sun overflow hierarchy; Sun record-pool capacity |
| [VALIDATION-EDITOR-GUI.md](VALIDATION-EDITOR-GUI.md) | the voxel editor, GUI widgets or input routing, or the trixel fonts | Editor screen alignment; Camera gesture ownership; Editor Box/Line; Editor shadow return; GUI behaviour; GUI trixel font art (large and small sets); GUI scale-1 text legibility |
| [VALIDATION-PACKAGING.md](VALIDATION-PACKAGING.md) | a `<target>Package` archive or its runtime closure | Package clean-box smoke; Windows package closure; macOS package closure |

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

The render harness under `scripts/` is stdlib-only, with one Pillow exception
tier; shader contract suites also need `c++`. Read the contract in
[VALIDATION-RENDER.md](VALIDATION-RENDER.md#harness-dependency-contract)
before adding an import to a harness script or a metric a manifest names.

## Default-off features need a positive enabled-path test

A render feature that defaults OFF needs a test of its ENABLED path; byte
identity at default proves only that OFF is a no-op. Read the rule in
[VALIDATION-RENDER.md](VALIDATION-RENDER.md#default-off-features-need-a-positive-enabled-path-test)
before shipping such a feature.

Adding a validator: a cross-cutting row goes in this file's table; any other
row goes on its area page, and its first-column name joins that page's line in
the area table (`scripts/fleet/tests/test_validation_index.py` fails when the
two disagree). Add a `tests/test_<name>` suite beside it if it is a fleet tool,
and a CI workflow when it must gate merges.

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
| Header conventions | header-global ban, anonymous namespaces, `*Detail` namespaces, Metal registries | `cmake -DPROJECT_ROOT=$PWD -P cmake/run_header_checks_standalone.cmake` | header-checks.yml |
| Python lint | ruff rules over `scripts/` | `ruff check scripts/` | python-lint.yml |
| Comment refs | no issue/PR numbers gained in code comments | `python3 scripts/lint_comment_refs.py` | comment-refs.yml on every push and PR |
| Format | clang-format on the branch's changed lines | `fleet-build --target format-changed`; no-configure path `cmake -DPROJECT_ROOT=$PWD -DCLANG_FORMAT_BIN=<bin> [-DFORMAT_DIFF_BASE=<commit>] -P cmake/run_clang_format_changed_standalone.cmake` | format-check.yml (changed lines only, clang-format pinned) |
| Render regression | a demo's shots match committed references | `render-verify` skill; `python3 scripts/render-verify.py --target <Demo>` | render-harness-tests.yml tests the harness itself |
| GUI behaviour | GUI-ASSERT shots pass for a creation | `gui-verify` skill; `python3 scripts/gui-verify.py <Creation>` | none |
| Cull regression | occlusion-cull statistics against committed baselines | `python3 scripts/cull-verify.py` | none |
| Render metrics | shadow, silhouette, coverage, jitter, clip, depth-tier, feeder-margin, light, pivot, receive-yaw readings | `python3 scripts/<metric>-verify.py` / `scripts/render-*-metric.py` | none |
| Perf gate | frame-time cells against the committed perf baseline | `bash scripts/perf/perf_grid_matrix.sh` then `scripts/perf/compare_perf_runs.py` | perf-gate.yml |
| Plan lint | a `## Plan` comment is structurally sound | `fleet-plan-lint <issue> [--repo game]` | none (planner-time) |
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

Adding a validator: one row here, a `tests/test_<name>` suite beside it if it
is a fleet tool, and a CI workflow when it must gate merges.

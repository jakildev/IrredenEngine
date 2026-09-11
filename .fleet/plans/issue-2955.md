<!--
Plan file for #2955 (per #1932 — committed as the first commit of the
implementer's PR). This is the `## Plan` comment posted on issue #2955 on
2026-08-23 and cleared `fleet:plan-review` SOUND the same day (fleet-plan-lint
exit 0; the reviewer independently re-derived all seven load-bearing negatives
against origin/master). Reproduced verbatim below; the issue thread is the
canonical source.

Two corrections the thread carries and this verbatim copy does not:
  * The plan's "cannot run on native Windows (#3007)" gotcha is stale — #3007
    closed via PR #3051; a Windows pane re-probes AC 4 rather than pre-parking
    (issue comment, 2026-09-10).
  * The plan review's one non-blocking follow-up — file the relative-check
    vacuity guard (a freeze that silently stops pinning the viewport passes at
    ~99.80 %, inside CULL_THRESHOLDS) before the argument that depends on it
    ships — is discharged during implementation and linked from the new
    §"Cross-run contract" section.
-->

## Plan: scripts/verify: cull-verify's committed baselines are write-only — no cross-run cull regression check exists (measured stale, gate still green)

- **Issue:** #2955
- **Model:** sonnet — the design call is made in this plan; what remains is a bounded deletion, a docstring/doc edit, one hermetic unit test, and one harness run. (The issue body says `opus`; this line supersedes it — the approach is fixed below, no design judgment is left to the implementer.)
- **Date:** 2026-08-23

### Scope

Remedy **(2) — delete**: drop `--update-baselines` / `--force` from `scripts/cull-verify.py`, delete the 12 unread `cv_frozen_*.png` under `creations/demos/shape_debug/test/references/macos-debug/cull-verify/`, state the harness's cross-run contract (relative-only, by design) in `docs/design/cull-validation-harness.md`, and pin that contract with a hermetic unit test so the unread set cannot be silently re-added. No engine, demo, or render-verify reference changes.

### Verified current state (read against `origin/master` @ `bd20b514`, 2026-08-23)

1. **The baselines are written by one site and read by none.** `scripts/cull-verify.py:173-189` is the only place `baseline_dir` exists — inside `if args.update_baselines:`. The compare loop (`:199-213`) passes `live` vs `frozen` from the *same* capture to `verify_common.compare`. `scripts/verify_common.py` has no `baseline` / `references` code at all. A repo-top content sweep for `cv_frozen|update-baselines|cull-verify` (rooted at the repo top, never at `creations/` — #2739) hits: `scripts/cull-verify.py` (own label list + flag), `scripts/light-verify.py` (its *own* `--update-baselines`, which it does read at `:277+`), `scripts/tests/test_verify_common_full_frames.py` (imports `cull-verify.py` for `_collect_shots` / `TOTAL_SHOTS` only), `creations/demos/shape_debug/main.cpp:1705-1708` (the label emitter), `docs/design/cull-validation-harness.md`, `.claude/skills/render-debug-loop/diagnosis/lighting.md:43` (light-verify's flag), plus incidental name-drops in `tools/jitter_probe/README.md`, `docs/design/editor-authoring-friction.md`, `.fleet/plans/issue-2358.md`. **No reader of `references/*/cull-verify/` exists anywhere.** `shape_debug/test/references/manifest.json` has no `cull` entry, so render-verify's manifest sweep never touches the subdir either.
2. **The set was never a gate.** `5b6a8859` (#1441 / PR #1460) added the flag and the 12 PNGs in one commit; that PR's test plan lists them under "[ ] Committed frozen baselines are inspected visually" — a manual-inspection artifact. #1441's acceptance criterion — "fails if a future change culls on-screen content under yaw" — is the relative live-vs-frozen invariant, which is what the harness docstring (`:14-17`), `main.cpp:1651-1655`, and the design doc (§"The automated sweep") all describe. No document specifies a cross-run compare.
3. **The set is stale — the issue's own macOS measurement, not re-measured here:** all 12 diverge 80.05–95.71 % match against a fresh `0d708292a` capture, vs `CULL_THRESHOLDS.match_pct = 99.7`. Under remedy (2) staleness is moot (the files go); recorded so the next reader knows the deletion discards nothing that was passing.
4. **Absolute render drift on `shape_debug` already has an owner.** render-verify's `macos-debug/` manifest (21 shots: zoom-4 yaw 45/90/180/270, `zoom4_pan16_yaw{0,45,90,180}_pivot`, …) went **17/24 red** on exactly the three merges the issue names (`53ac2cbac`, `4c4554d5a`, `e640a5b19`) — #2943 — and was re-blessed with per-shot classification in PR #2945 (merged 2026-08-21, `66b30685b`). A wired cull baseline would have been a second alarm in the same window on the same demo.

### Decision: delete, not wire (made here, for the plan reviewer — not a choice left to the implementer)

- **Wiring would add a second absolute render-regression gate to a demo that already has one.** The frozen frames are the scene at 12 zoom-4 poses with shadows off; any deliberate render change (registration, pivot, lighting) moves them exactly as it moves render-verify's shots, so every future #2510 / #2943-style re-bless would have to be done twice, with the same per-shot classification discipline, on the same host. The cull-*specific* signal — "the live cull dropped something the wide frozen cull kept" — is already carried by the relative check and gains nothing from a committed frozen frame.
- **Wiring would not catch the one failure the relative check is blind to.** If the freeze silently stopped pinning the viewport (`IRRender::setCullingFrozen` ignored), `cv_frozen_i` would just be a live frame. The design doc's measured live-vs-frozen residue with shadows off is ≤ 0.2 % of bytes at the worst poses (§"Findings" 1 table: 99.80 % at inter-cardinal / pan poses), which is *inside* `CULL_THRESHOLDS` (99.7 %) — so a committed truly-frozen baseline would still PASS a vacuous run. 2.0 MB of binaries would not buy the protection that could justify them. (A vacuity guard is a non-image concern — a viewport-state assertion — outside this issue's scope and not filed here.)
- **`CULL_THRESHOLDS` are calibrated to the live-vs-frozen AO residue (#1438), not to cross-run drift.** Reusing them cross-run conflates two tolerance regimes; a correct wiring would need its own thresholds plus a host-keyed skip policy — more surface for a redundant gate.
- **Deletion is reversible in git, needs no macOS re-bless, and satisfies the issue's AC 2 for every backend at once.** If absolute coverage of a cull pose is ever wanted, the right home is a shot in `shape_debug`'s render-verify manifest (one reference discipline, one re-bless) — the doc section below says so explicitly.

### Approach

**Phase 0 — re-verify the negative before deleting (cheap; bail path stated).** From the repo top:
```
fleet-rules-sweep --pattern 'cv_frozen|update-baselines|cull-verify/' .
```
Expected: exit 0 with hits only at the sites listed in "Verified current state" 1 (plus this plan file once committed). A hit that *reads* `references/<backend>/cull-verify/` — anything outside that list that opens, globs, or compares against that path — refutes the premise: stop, comment the hit on #2955, put the PR in `fleet:design-blocked`; do not delete under it. (`fleet-rules-sweep` is not allowlisted on every pane — if denied, `rg -n -g '!build/**' 'cv_frozen|update-baselines|cull-verify/' .` from the repo top is the hand-rolled form; **never** root it at `creations/`, #2739.)

**Step 1 — plan file.** Commit this plan as `.fleet/plans/issue-2955.md` (first commit of the branch, #1932).

**Step 2 — delete the unread set.**
`git rm -r creations/demos/shape_debug/test/references/macos-debug/cull-verify/` — exactly 12 files, `cv_frozen_000..011.png`. Nothing else under `test/references/` changes (render-verify's 21 PNGs + `manifest.json` stay): `git diff --stat origin/master -- creations/demos/shape_debug/test/references/ | tail -1` must read 12 files changed, all deletions.

**Step 3 — `scripts/cull-verify.py`.**
- Remove the `--update-baselines` and `--force` `add_argument` calls (`:113-120`) and the whole `if args.update_baselines:` block (`:173-189`).
- Remove `demo_dir` (`:130`) — it becomes unused. Keep `shutil` (still used for `rmtree` at `:141`). Leave the pre-existing unused `REPO_ROOT` alone — unrelated cleanup.
- Module docstring: drop the two `--update-baselines` usage lines (`:24-25`); after the "For each i in 0..N-1 …" paragraph add the contract in two sentences: *"The assertion is relative by design — live_i vs frozen_i from the same capture. There is no committed cross-run baseline: absolute render drift on shape_debug is render-verify's job (see docs/design/cull-validation-harness.md §"Cross-run contract"), and this harness must never grow a reference set it does not read (#2955)."*
- `--help` / `-h` must still exit 0.

**Step 4 — pin the contract in code: `scripts/tests/test_cull_verify_contract.py`** (new; named `test_*.py` so `run_all.sh` discovers it; hermetic — no engine, no build, sub-second; import the dashed script via the same `importlib` `_load` shape as `test_verify_common_full_frames.py:32-42`). Two tests:
- `test_update_baselines_flag_is_gone`: call `_cv.main(["--update-baselines", "--force", "--build-dir", <empty tempdir>])` inside `contextlib.redirect_stderr(io.StringIO())`; assert `SystemExit` with `.code == 2` and `"unrecognized arguments"` in the captured stderr. argparse rejects before any build/run, so this runs on every host. The `--build-dir <empty tempdir>` is what keeps the **positive control** cheap: with the flag restored, parsing succeeds and `verify_common.detect_backend` raises `SystemExit("no CMakeCache.txt …")` — a non-2 code, the test FAILS, and no demo is launched.
- `test_no_committed_cull_verify_reference_dir`: `refs = <repo>/creations/demos/<_cv.DEMO_NAME>/test/references`; assert `sorted(refs.glob("*/cull-verify")) == []` with a message naming #2955 and the doc section. Positive control: `mkdir -p …/macos-debug/cull-verify && touch …/cv_frozen_000.png` → FAILS; remove it.
- Module docstring states why a test asserts an *absence*: the doc section is prose; this is the contract encoded in code (CLAUDE-BASELINE §"Encode contracts in code, not in comments") so a future "let's commit references for cull-verify" has to delete a test — and read the rationale — rather than silently re-create the unread set.

**Step 5 — `docs/design/cull-validation-harness.md`.** Add a section after "### Why `--cull-validate` disables sun shadows":
```
### Cross-run contract: relative-only, by design (#2955)

`scripts/cull-verify.py` asserts one thing: at every pose, `cv_live_i` matches
`cv_frozen_i` **from the same run**. There is no committed baseline and no
bless flag — the harness never compares a capture against a previous run's
frames, and it must not grow a reference set it does not read.

Why not add one:

- Absolute render drift on `shape_debug` is **render-verify's** job. Its
  `macos-debug/` manifest covers the same pose family (zoom-4 yaw sweep,
  pan-under-yaw pivot arms) and it is the gate that went red on the
  registration / camera-pivot merges (#2943) and was re-blessed per shot
  (PR #2945). A second frozen-frame set would have been a duplicate alarm
  in the same window, paid for twice at every deliberate render change.
- A committed frozen frame would not catch the failure the relative check is
  blind to — a freeze that silently stops pinning the viewport. The measured
  live-vs-frozen residue with shadows off (§Findings 1: ≤ 0.2 % of bytes) is
  inside `CULL_THRESHOLDS` (99.7 %), so a vacuous "frozen" phase would still
  pass against a real baseline.
- `CULL_THRESHOLDS` are calibrated to that live-vs-frozen AO residue (#1438),
  not to cross-run drift.

History: #1441 / `5b6a8859` shipped `--update-baselines` and 12
`macos-debug/cull-verify/cv_frozen_*.png` as a manual-inspection artifact;
nothing ever read them, they went stale unnoticed (80–96 % match by
2026-08-07), and #2955 removed both. Want absolute coverage of a cull pose?
Add the shot to `shape_debug`'s render-verify manifest — one reference
discipline, one re-bless. `scripts/tests/test_cull_verify_contract.py` pins
this section.
```
Also, in §"The automated sweep" after the `render-compare.py` snippet, one line: *"This pairwise diff is the whole assertion — see §"Cross-run contract" for why no committed baseline exists."* Leave the P1 "Findings" and the historical "Implication for P4" paragraph as they are (they are the record).

**Step 6 — run the gates (host-independent).**
- `bash scripts/tests/run_all.sh` → all suites PASS (one more suite than before). Run both Step-4 positive controls, revert them, paste the FAIL lines in the PR body.
- `ruff check scripts/cull-verify.py scripts/tests/test_cull_verify_contract.py` clean.
- `python3 scripts/cull-verify.py --help` exits 0 and no longer mentions `--update-baselines`.
- Re-run the phase-0 sweep after the edits: hits are now only `light-verify.py` (its own flag), `main.cpp` (emitter), the design doc, the new test's assertion message, `lighting.md` (light-verify), and this plan file.

**Step 7 — run the harness (macOS or Linux pane — see Gotchas).**
`fleet-build --target IRShapeDebug && python3 scripts/cull-verify.py --no-build` → `[cull-verify] all 12 poses PASS — live cull is conservative`, exit 0. Paste the 12-row table in the PR body under "## Verification".

**Step 8 — `commit-and-push`.** PR title: `scripts/verify: cull-verify is relative-only — drop the unread --update-baselines set (#2955)`. Body carries `Closes #2955`, the phase-0 sweep output (before + after), the 12-deletion `--stat` line, both test positive controls, and the Step-7 table. One PR; no stack.

### Affected files
- `creations/demos/shape_debug/test/references/macos-debug/cull-verify/cv_frozen_000.png` … `cv_frozen_011.png` — **deleted** (12 files, ~2.0 MB).
- `scripts/cull-verify.py` — remove `--update-baselines` / `--force` + the write block + `demo_dir`; docstring states the relative-only contract.
- `scripts/tests/test_cull_verify_contract.py` — **new**; two hermetic tests pinning "no bless flag, no committed set".
- `docs/design/cull-validation-harness.md` — new §"Cross-run contract: relative-only, by design (#2955)" + one pointer line in §"The automated sweep".
- `.fleet/plans/issue-2955.md` — **new** (this plan, first commit).

### Acceptance criteria
1. **Flag removal fires, not no-ops:** `python3 scripts/cull-verify.py --update-baselines --force` exits **2** with `unrecognized arguments` (any host — argparse runs before build/run); `--help` exits 0 without the flag.
2. **Contract test fires:** `bash scripts/tests/run_all.sh` is green with `test_cull_verify_contract.py` included; each of its two tests is shown FAILING under its positive control (flag restored → non-2 exit; a `cull-verify/` dir re-created → assertion fails) and PASSING after revert — both transcripts in the PR body.
3. **No write-only reference set (issue AC 2):** `git ls-files 'creations/demos/*/test/references/*/cull-verify/*'` is empty; the phase-0 sweep shows no reader of that path; `git diff --stat origin/master -- creations/demos/shape_debug/test/references/ | tail -1` = 12 deletions only.
4. **Harness still green (issue AC 1):** on a macOS/Metal (or Linux/GL) host, `python3 scripts/cull-verify.py --no-build` exits 0 with all 12 poses PASS — the 12-row table pasted in the PR.
5. **Contract stated (issue AC 3):** `docs/design/cull-validation-harness.md` §"Cross-run contract" present with the three reasons above; the `cull-verify.py` docstring agrees with it; no file in the tree still documents `cull-verify.py --update-baselines`.
6. `ruff check` clean on the touched Python.

### Gotchas
- **Host for AC 4.** The verify-harness family cannot launch `fleet-run` on native Windows (#3007), so the live run must come from a macOS or Linux pane. Steps 1–6 are host-independent. A native-Windows pane that has claimed this task should do Steps 1–6, push the branch as `fleet:wip`, and state in the PR body that AC 4 is pending a macOS/Linux run — do not drop `fleet:wip` until the run has been done and pasted, and do not ship it as "unchanged code path, skipped".
- **Delete only the `cull-verify/` subdir.** The sibling 21 render-verify PNGs and `manifest.json` in the same `macos-debug/` directory were just re-blessed (#2945) — touching them here would re-open #2943's per-shot classification question inside an unrelated PR.
- **`--build-dir <empty tempdir>` in the flag test is load-bearing** — without it the positive control would find the exe and launch the demo on a pane with a configured build.
- **Don't pre-declare the harness "fixed" for Windows.** #3007 is its own ticket; this PR does not touch `verify_common.run_capture`.
- **`light-verify.py` keeps its `--update-baselines`** — it *reads* its baselines (`:277+`); the sweep will keep showing it and that is correct. `lighting.md:43` documents light-verify, not cull-verify — leave it.
- **Plans are engine-public** — nothing here references private creations.

### Sibling / in-flight reconciliation
- PR #2945 (merged) — the render-verify re-bless this issue forked from; untouched by this plan.
- #3007 (Windows harness launch), #3016 (no GL-tier `shape_debug` render-verify set), #2361 (occlusion-cull render-verify baseline), #3042 (#2298 cull gates on Metal) — all about *other* gates' baselines; none reads `cull-verify/`, none is blocked or unblocked by this.
- Open PRs: none touch `scripts/cull-verify.py`, `scripts/tests/`, `docs/design/cull-validation-harness.md`, or `shape_debug/test/references/` (checked against the 10 open engine PRs in the fleet cache; `gh pr list --search cull-verify` returns only #3041, whose files are `.fleet/plans/issue-2298.md` + `docs/design/voxel-occlusion-culling.md`).

## Plan: refresh stale canvas_stress render-verify references (post-#1953)

- **Issue:** #1944
- **Model:** sonnet
- **Date:** 2026-06-21
- **Blocked by:** #1953

### Scope
Re-bless the stale `canvas_stress` render-verify reference images (both `macos-debug` and `linux-debug`) so the 6-shot pixel-diff gate passes on `origin/master` again — **after** the #1953 regression-revert lands. Reference-only refresh: no source, manifest, or `STRUCTURAL-BASELINES.md` changes. Compare-shot gating (`compare_yaw0`/`compare_yaw_q`) is **deferred** to a follow-up (it is not a reference refresh — see Gotchas).

### Verified current state
- Repro confirmed in the issue: `python3 scripts/render-verify.py --target IRCanvasStress --no-build` on clean `origin/master` fails 6/6 (match ~97.9–98.1%, max_delta 230) on the `macos-debug` set.
- The 6 gated shots and their per-backend reference PNGs:
  `creations/demos/canvas_stress/test/references/manifest.json` → `shots[]` = `so3_smooth_sweep, so3_offsnap_disc, so3_wide_parity, so3_grid_cross, so3_offsnap_wide, revoxelize_solids`; references live in `creations/demos/canvas_stress/test/references/{macos-debug,linux-debug}/<shot>.png`.
- macos-debug refs last blessed **2026-06-11** (P5, commit 97e74307); linux-debug refs **2026-06-09** — both predate the ~30 render PRs since, so both are stale.
- **Premise correction (verify-before-plan):** the issue body diagnoses the drift as "cumulative *intended* improvement, not a single regression." That is only partly true. Open PR **#1953** (title ends `(#1944)`) reverts #1942's viewport-center default, which it isolates as a **jitter regression** moving every auto-rotating canvas_stress shot by **1.3–3.4%**. So a material part of the current drift is the #1942 regression, not intended improvement. The genuine intended residual (AO de-speckle #1873, world-placed depth #1872, sun-shadow ambient #1874, per-axis scatter #1877/#1878/#1882/#1883, Z-yaw pivot #1926/#1927, etc.) only emerges cleanly **after** #1953 merges.

### Approach (single, committed)
1. **Gate on #1953.** Do not start until #1953 has merged to `origin/master`. Confirm with `git -C <repo> merge-base --is-ancestor <#1953-merge-sha> origin/master` (or `git log origin/master --grep 1953`). Refreshing before #1953 lands would bless the #1942 regression and the gate would go red again the moment #1953 merges.
2. From a clean checkout of current `origin/master` (post-#1953), build + run: `python3 scripts/render-verify.py --target IRCanvasStress`. It will FAIL on the still-drifted shots and emit per-shot diff images (`--diff-out`). Note which shots still differ and by how much.
3. **Confirm the residual drift is benign before blessing** (this is the gate that separates "refresh" from "enshrine a regression"). For each failing shot, view the new capture + the committed reference + the diff image and check:
   - central re-voxelize cubes still **lit** (P4 AO/sun integration), geometry intact;
   - peripheral forward-scatter ring present — no missing faces, black/blank frames, or garbage;
   - deltas localize to lit surfaces / scatter ring / small entity-position shifts that correspond to the known merged render PRs above.
   - **Tripwire — STOP, do not bless:** any shot with missing geometry, a black/blank frame, obviously-wrong lighting, or a delta that does not map to a known merged render change. That is a possible un-reverted regression; escalate per worker step 8a (re-tag `fleet:opus`) or file a bug rather than blessing over it.
4. Re-bless **this host's** backend dir: `python3 scripts/render-verify.py --target IRCanvasStress --update-references --force` (overwrites `…/references/<preset>/*.png`). Re-run without `--update-references` → expect **6/6 PASS**.
5. **Cross-host: refresh the other backend too** (both are stale). The authoring host can only bless its own backend, so after opening the PR add `fleet:needs-linux-smoke` (if authored on macOS) or `fleet:needs-macos-smoke` (if authored on Linux) per [`docs/agents/FLEET-CROSS-HOST-SMOKE.md`](docs/agents/FLEET-CROSS-HOST-SMOKE.md) and the manifest's documented per-host convention. An agent on that host checks out the branch, runs `--update-references --force` for their backend, and pushes the refreshed refs to the same branch. Both reference sets must land together before merge.
6. Open **one** PR `Closes #1944`, with `.fleet/plans/issue-1944.md` (this plan) as the first commit.

### Affected files
- `creations/demos/canvas_stress/test/references/macos-debug/{so3_smooth_sweep,so3_offsnap_disc,so3_wide_parity,so3_grid_cross,so3_offsnap_wide,revoxelize_solids}.png` — re-blessed.
- `creations/demos/canvas_stress/test/references/linux-debug/<same 6>.png` — re-blessed on a Linux host (cross-host handoff).
- `.fleet/plans/issue-1944.md` (new) — plan record (first commit).
- **No** changes to `main.cpp`, `manifest.json`, `STRUCTURAL-BASELINES.md`, or `metrics.json`.

### Acceptance criteria
- On `origin/master` **with #1953 merged**, `python3 scripts/render-verify.py --target IRCanvasStress` → **6/6 PASS** on the authoring host's backend.
- Both `macos-debug` and `linux-debug` reference sets refreshed and committed (cross-host handoff completed before merge).
- No source/manifest/structural-baseline changes; `shadow_overlay_floor` (capture idx 8) and `revox_coverage` (idx 9) indices unchanged.
- A focused follow-up issue filed for compare-shot gating (see Gotchas).

### Gotchas
- **Blocked by #1953 — do not refresh before it merges.** Blessing the #1942 regression re-reds the gate the instant #1953 lands.
- **Never bless blindly.** A reference refresh silently encodes whatever is on screen as "correct" — the step-3 visual check + tripwire is mandatory.
- **HiDPI:** `macos-debug` PNGs are 2× framebuffer (Retina) → ~3× file size; capture each backend's refs on a host representative of where verify runs. A Linux ref captured on a Mac (or vice-versa) won't compare — never cross-bless.
- **Compare-shot gating is NOT a simple insert — deferred to a follow-up.** `compare_yaw0`/`compare_yaw_q` only spawn under `--only compare` (`kGroupCompare`, opt-in), which **suppresses** the main scene groups (so3_*, revoxelize_solids) — so they cannot be captured in the same render-verify run as the 6 gated shots. The manifest has no `run_args` field (render-verify passes demo args only ad-hoc via its CLI, `scripts/render-verify.py:450-452`), so persisting a `--only compare` gate needs a render-verify harness extension (a per-target second run-config). The issue NOTE's "insert contiguously after revoxelize_solids → index shift" concern is **moot**: the compare shots are conditional **end-appends** (absent from the default capture), so `shadow_overlay_floor`/`revox_coverage` indices never move. File a no-label follow-up: *"render-verify: support a per-target second run-config to gate canvas_stress `--only compare` shots (compare_yaw0/compare_yaw_q)"* — it may fold into the #1954 harness-extension work.
- **#1577 overlap:** the older in-flight *"refresh canvas_stress linux-debug references (post re-voxelize)"* (#1577, sonnet) covers the linux-debug half from the re-voxelize era and is ~30 PRs stale. Check its state and coordinate so the linux-debug refresh isn't double-done.
- **#1929 is the `shape_debug` sibling** (same symptom, different demo/refs) — out of scope here; don't touch it.

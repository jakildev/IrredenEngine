# scripts/perf — perf matrix, comparison, and the perf gate

`perf_grid_matrix.sh` drives `IRPerfGrid` across a matrix of cells and writes
`save_files/perf/<sha>[-<label>]/` (one `<cell>.txt` profile report per cell
plus `manifest.json`). `ir-perf-grid` (`engine/tools/bin/`) is the entry point:
it holds `ir-acquire benchmark` around the whole matrix, captures an
`ir_ref_bench` reading, and splices a `calibration` block — `ref_ms` and the
`ir-host-probe` fingerprint (`host_slug`) — into the manifest.
`compare_perf_runs.py` diffs two runs; `check_regression.py` is the gate.

## Verdicts are per fingerprint

`check_regression.py <baseline_root> <head_dir>` resolves
`<baseline_root>/<head host_slug>/`. A same-slug baseline gates (raw deltas,
or load-normalized when the head's `ref_ms` is at least
`LOAD_FACTOR_TRUST_NORMALIZED` times the baseline's). A different slug or no
baseline is informational and exits 0. A perf verdict therefore says nothing
about a host whose slug has no baseline.

Baselines live on the `perf-baseline` branch under
`docs/perf/baseline_latest/<slug>/` (`manifest.json`, `host.json`, one
`<cell>.txt` per cell). List them with
`git ls-tree origin/perf-baseline docs/perf/baseline_latest/`. There are two
sources:

- **Hosted Linux runners** (`linux-x86_64-*`): `perf-gate.yml` writes these on
  every master push that touches the gated paths, using the 2-cell `--quick`
  matrix at 60 frames.
- **The Windows ship host** (`windows-x86_64-ryzen-9-5950x-rtx-3060-ti`: Ryzen
  9 5950X, RTX 3060 Ti, MSYS2 mingw64, `windows-debug`): seeded by hand with
  the procedure below, using the default 12-cell matrix at 300 frames. No CI
  runner has this fingerprint, so only a run on that machine compares against
  it.

## Same-runner Linux diagnostic

When a historical CI comparison cannot isolate a change, dispatch `Perf Gate`
on the candidate branch with `diagnostic_base` set to the full base commit SHA:

```bash
gh workflow run perf-gate.yml --ref <candidate-branch> -f diagnostic_base=<base-sha>
```

A nonempty `diagnostic_base` disables the baseline writer. The job captures
head/base/head quick matrices on one hosted runner, rebuilding each ref and
refreshing runtime assets before measurement. `perf-run-<run-id>` retains all
raw reports plus `paired/` comparisons and full commit, binary and shader
fingerprints. Both total and steady frame means are reported; comparisons are
informational and do not override the PR gate. Repeated head runs expose drift,
but one base run is not a statistical performance guarantee. Hosted Mesa results
do not establish performance on hardware OpenGL or Metal.

`ci_pair.py` only switches refs in a clean GitHub-hosted checkout and restores
the original head on failure. It is not a local-worktree profiling command.
An empty diagnostic input retains the existing baseline-publishing behavior.

## Windows ship-host baseline

A human or the architect runs this on the ship host when cued. A fleet worker
never refreshes the baseline.

**Refresh when:**

- a merged PR labelled `perf:improved` touches `engine/render/` or the GL
  backend;
- the NVIDIA driver is updated. The fingerprint does not change, but shader
  compile and dispatch behaviour do;
- `engine/math/` changes. `ir-perf-grid` recalibrates `ir_ref_bench` then,
  and the old baseline's `ref_ms` no longer matches the new calibration.

**Exclusivity.** `ir-perf-grid` holds `ir-acquire benchmark` (CPU budget−1,
gpu, perf) for the whole matrix, and each cell's `fleet-run` runs inside that
hold, so queued fleet builds and demo runs wait behind it. That is not
enough on the ship host, which is also an interactive desktop. With the
satellite fleet live and desktop apps open, four back-to-back runs of one
master SHA spread 10–43% per cell (median 25%), which is wider than the
10% gate. One run read load factor 1.01 at its start and still ran every
cell 10–40% slow: `ref_ms` is sampled once, before the first cell, and does
not see load that arrives mid-matrix. So: `fleet-down`, close GPU- and
CPU-heavy desktop apps, and publish only a run that passes the self-check
below.

**Wall time** on the ship host: about 220 s per matrix run (12 cells at
~18 s each, plus the calibration probe). A seed plus its self-check is
under 8 minutes.

Steps (every path is relative to a clean, detached checkout of the measured
`origin/master` SHA, never a feature branch or dirty tree — the manifest
records `git_sha` and `git_dirty`):

```bash
git worktree add --detach <scratch>/master origin/master
cd <scratch>/master
cmake --preset windows-debug
ir-build --target ir_ref_bench && ir-build --target IRPerfGrid
ir-host-probe --refresh           # the cached fingerprint never expires on its own
ir-perf-grid --label baseline     # → save_files/perf/<sha>-baseline/
ir-perf-grid --label selfcheck    # same SHA, immediately after
mkdir -p <scratch>/seed && cp -r save_files/perf/<sha>-baseline <scratch>/seed/<slug>
python3 scripts/perf/check_regression.py <scratch>/seed save_files/perf/<sha>-selfcheck
```

Publish only when that self-check exits 0, every cell's `status` in both
manifests is `ok`, and the load factor is below
`LOAD_FACTOR_TRUST_NORMALIZED` (1.20). The load factor is
`calibration.ref_ms` divided by the calibrated median in
`~/.cache/irreden/calibration/<slug>.json`. A failing self-check means the
host was not quiet. Find the load and re-run both matrices. Never pick a
passing pair out of several runs.

Commit the run to `perf-baseline` in the CI writer's layout. That is the
"Update baseline on the perf-baseline branch" step of `perf-gate.yml`:
copy the run minus `*.log` and `.cell_marker`, and write the manifest's
`calibration.host_fingerprint` as `host.json`:

```bash
RUN=save_files/perf/<sha>-baseline
SLUG=windows-x86_64-ryzen-9-5950x-rtx-3060-ti
git fetch origin perf-baseline
git worktree add --detach <scratch>/pb origin/perf-baseline
mkdir -p <scratch>/pb/docs/perf/baseline_latest/$SLUG
cp "$RUN"/manifest.json "$RUN"/*.txt <scratch>/pb/docs/perf/baseline_latest/$SLUG/
python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); json.dump(m["calibration"]["host_fingerprint"], open(sys.argv[2],"w"), indent=2, sort_keys=True)' \
    "$RUN/manifest.json" <scratch>/pb/docs/perf/baseline_latest/$SLUG/host.json
git -C <scratch>/pb add -A docs/perf/baseline_latest/$SLUG
git -C <scratch>/pb commit -m "perf: seed Windows ship-host baseline ($SLUG) <date>@<sha>" \
    -m "Driver <NVIDIA version>; ref_ms <ref_ms>; load factor <lf>."
git -C <scratch>/pb push origin HEAD:refs/heads/perf-baseline   # never --force
```

The branch is the audit trail and is append-only. If the push is refused,
fetch, re-create the worktree on the new tip, and repeat the copy.

## Compare a PR head on the ship host

```bash
git worktree add --detach <scratch>/pr <pr-head-sha>      # build it as above
cd <scratch>/pr && ir-perf-grid --label pr<N>
mkdir -p <scratch>/base
git archive origin/perf-baseline docs/perf/baseline_latest | tar -x -C <scratch>/base
python3 scripts/perf/check_regression.py \
    <scratch>/base/docs/perf/baseline_latest save_files/perf/<sha>-pr<N>
```

Exit 0 with a per-cell table means no cell regressed past `--regress-pct`
(default 10). Exit 1 names the regressed cells. The informational
"seeding new baseline" or "host mismatch" banner means the head's slug did
not match, so nothing was compared. Run the head the same way as the
baseline: same preset, same default matrix, same quiet fleet.

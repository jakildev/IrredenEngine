# Surface receiving and visibility-prepass controls

The four-arm driver is validated, but **neither timing attempt qualifies for
performance comparisons**. Both outer benchmark wrappers exited 76 with a quiet
coverage breach. Battery power was also provisional by explicit user direction;
that permission does not make a failed quiet window valid. No rendering default
or engine code changes in this slice.

## What the native runs establish

The first attempt completed 24 CLEAN Metal Debug runs: two forward/reverse rounds
of surface lighting 0/1 × visibility prepass 0/1 for three cases. Each run recorded
240 frames without screenshots or visibility-stat readbacks. The cases use the
same GRID floor, fixed source-face box, vertical sun, zoom 4 and subdivision 1:
intermediate camera yaw 0.6 with zero or 16 distant coincident casters, plus a
cardinal camera control with 16 casters. The exact
[commands and environments](rejected-window/timing/cases.json) are retained.

All requested environments, runtime artifact identities, completed draw paths
and round counts passed the driver checks. Intermediate sampled receiving uses
compute lighting plus scatter; continuous receiving uses the surface draw
without compute relighting. The per-axis switch does not disable cardinal
surface receiving. Each of the 32 timed reports across both attempts has 240/240
valid GPU frame samples; completing these samples does not certify host isolation.

Three separate stats-enabled diagnostic runs precede the first timing window:

| Surface | Prepass | Visibility diagnostic |
|---|---|---|
| 1 | 1 | 119,310 candidates, 119,310 retained, 0 rejected; 927,048 framebuffer pixels |
| 0 | 1 | No visibility-probe row; sampled receiving route |
| 1 | 0 | No visibility-probe row; continuous receiving route |

The active diagnostic also reports 102,414 indexed cascade queries, 1,162,296
candidate visits, 1,048,977 face misses and 6,914 blocked queries, with `valid=1`.
Its [verdict](rejected-window/diagnostic-verdict.json) and raw compressed logs
preserve those witnesses. Diagnostic timings are not used for comparison.
All timed runs explicitly disable these counters; their logs contain no
visibility-probe row.

This fixture overlaps **casters**, not GRID receiver geometry. Zero rejected
fragments is therefore a useful inactive-benefit control, not evidence that the
prepass cannot help overlapping receivers. The combined surface timer cannot
prove prepass execution by itself. The separate
[receiver polygon evidence](../../pr-screenshots/codex/receiver-shadow-polygon/README.md)
owns visual correctness: its tested intermediate continuous path passes while
the default sampled path still fails. These profiling runs add no new visual
correctness claim.

## Retained attempts and qualification limits

- [First attempt](rejected-window/timing/summary.md): all 24 runs completed;
  the outer [quiet verdict](rejected-window/quiet.txt) is BREACH with two
  uncovered participants. Raw [power](rejected-window/power.json) was battery
  throughout, 66% before diagnostics, 65% before timing and 64% afterward.
- [Shorter retry](rejected-retry/per-axis-zoom4-overlap0/summary.md): all eight
  sparse-case arms completed, then the outer wrapper again reported BREACH.
  The [run record](rejected-retry/runs.json) retains exit 76, its uncovered
  participant and battery at 63%. The driver stopped before the crowded and
  cardinal cases; no incomplete matrix is represented as a finished comparison.

Both attempts remain here without selecting favorable measurements. Per-run
quiet fields are `unknown` because the outer lease is reused; the failed outer
verdicts govern qualification. No speedup, parity, prepass benefit, 60 FPS or
million-entity claim follows. Wall-frame durations include offscreen pacing;
GPU envelope spans submission gaps and is not GPU busy time. Named stage means
are not additive full-frame costs.

The frozen executable SHA-256 is
`ff093ee5053bb699b6a48bbc3dba08fddf8fe85b0a0631b98a4b4d02cebe8206`, with shader
digest `8fa38221b8a91d26d053193df1920148d15509470d6bf0ce81d0b35d002da6bd` and
runtime-script digest
`c3ab47543e58c89b874850315b06881210a2dc81013b1524233269e9f7edad5b`.
It is the same guarded-depth artifact whose
[identity and source overlay](../sun-overflow-production/guarded-depth/timing/README.md#fixture-and-provenance)
are retained upstream. Its artifact manifest was byte-compared with the frozen
copy. Run manifests name the separate runner checkout `229a1ae598391fa862ca70054600f98c77f1ab72`
and its dirty profiling/doc edits; that is not the executable's source identity.
The exact orchestration scripts are retained as `driver.py.txt` in each attempt.
Original report/log bytes are losslessly gzip-compressed; JSON and summaries are
unchanged. The archive manifest hashes all retained files except itself.

## Next gates

Repeat in a covered quiet window before choosing defaults. Add actual
overlapping GRID receivers, wide views, nonvertical sun and subdivision controls;
keep receiver-polygon correctness separate from timing. Preserve the existing
fog exclusion until albedo, fog application and buffer-binding compatibility are
resolved. A default change needs explicit environment opt-out and unchanged
runtime readiness semantics. Decide accurate receiving and prepass defaults
independently. Release and native OpenGL qualification, motion and million-entity
scene budgets remain separate work.

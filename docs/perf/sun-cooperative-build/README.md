# Two-pass production sun hierarchy

The hierarchy build uses two 128-thread cooperative dispatches instead of
fourteen level dispatches. Workgroups first construct disjoint subtrees in 8 KiB
of shared memory; a second group combines their roots and publishes readiness.
The global tree, eight-record leaves, finite geometry predicates and query path
are unchanged. No sorting, CPU readback, intergroup polling, blur or extra
per-frame allocation is introduced.

## Native evidence

[summary.md](summary.md) gives the measured medians; [native.json](native.json)
retains all samples and pipeline properties. The probe compiles expanded actual
production shaders from parent `e766f2bb80f5e06d346fc96a9b3dcf70906fa823` and the
candidate source overlay on `4ef8a31b6607cbcce2c3d7845cbe05c3d6874dfd`. Shader
dependencies and hashes, compiler, power snapshots and command arguments are in
[metadata.json](metadata.json). [base.metal](base.metal), [head.metal](head.metal)
and [probe.mm](probe.mm) preserve the exact native inputs; the executable is not
committed. Raw record inputs and full tree readbacks are retained as `.bin.gz`.

The portable test suites read none of these archived `.bin.gz` files: they
generate their own inputs and validate current production source. Native probe
replays also generate fresh inputs and readbacks. The compressed files retain
the measured run for independent auditing; they are not test fixtures.

All **38 paired builds** match byte-for-byte in fast and safe Metal math modes.
The independent corner/enclosure oracle checks **70,658 used nodes** across
empty, unneeded, exhausted-pool, 1/8/9/1023/1024/1025/65536-record, mixed analytic
and finite, reversed, poisoned, and persistent shrinking-frame controls. Prefix,
suffix and unused-tree guards remain untouched. Analytic records are checked for
conservative unbounded admission; this is not a new analytic-intersection test.

The timing run holds the benchmark lease, burns in each case/math mode for at
least three seconds, then alternates base/head and head/base over eight rounds.
Each sample repeats a build 32 times; readbacks and validation are outside the
timed interval. Empty and unneeded builds are measured too. The retained
[launcher result](launcher-result.json) records exit 0 with no quiet breach;
[observed-quiet-record.json](observed-quiet-record.json) transcribes the live
record after it reached the barrier and released. Under `cmd_finish`, this
combination establishes a guarded run. An explicit report file was not requested
for this first run; the ephemeral original record was cleaned before archival. The host reports AC power and charging in both
recorded power snapshots. This is the GPU command envelope per build, including
dispatch boundaries, **not an engine-frame, Release or Windows speedup claim**.
Repeated static records remain cache-friendly; density, motion, whole-frame
submission cost and hardware OpenGL require separate measurements.

A later repeat passed the same native correctness checks but returned exit 76
and [BREACH](excluded-repeat/quiet-report.txt); its timings are rejected. Its
[metadata](excluded-repeat/metadata.json), [raw samples](excluded-repeat/native.json)
and [log](excluded-repeat/native.log) are retained only for audit. An intervening
sandboxed attempt failed before testing because Metal was unavailable and
produced no measurements.

## Portable and scene controls

`test_render_sun_hierarchy.py` executes both production dialects through phased
CPU adapters: 36 overlap/order controls and 1,147 independent triangle rays per
backend, all-node bounds through full capacity, readiness, poison/shrink, clear,
strict depth windows and independent raster-layer preservation. Twelve shader
mutations per backend and five host-dispatch mutations are rejected. The actual
host dispatch body and Metal threadgroup registry are executed with a mocked
device. Serial phase execution cannot prove GPU synchronization; the native
readbacks provide that bounded hardware check.

`test_sun_hierarchy_build_probe.py` has ten portable checks for source expansion,
dispatch plans, readback validation and the independent oracle's failure cases.
The native engine build passed. All [eleven crowded-shadow screenshots](../../pr-screenshots/codex/sun-cooperative-build/README.md)
are unchanged in full RGBA against the retained 14-pass frames.

Replay the isolated test from this checkout with a fresh output directory:

```bash
ir-acquire gpu -- python3 scripts/perf/sun_hierarchy_build_probe.py --base-ref e766f2bb8 --output /tmp/sun-cooperative-check
```

For measurement, use the benchmark lease and retain its quiet verdict:

```bash
IR_QUIET_REPORT_FILE=/tmp/sun-cooperative-quiet.txt ir-acquire benchmark -- python3 scripts/perf/sun_hierarchy_build_probe.py --base-ref e766f2bb8 --output /tmp/sun-cooperative-timing --measure --rounds 8 --builds-per-sample 32
```

This bounded optimization does not enlarge the 65,536-record pool, solve
raster-only depth loss, or fix the default sampled intermediate-yaw receiver
edge. The historical software-OpenGL regression needs a same-runner base/head
comparison; these Metal measurements cannot close it.

# Per-light falloff cost

The five-pair record in `comparison.json` measured the retired winner-radius
implementation at PR head `4f97f435e`; it is retained as history and is not
acceptance evidence for the radius-class implementation.

The replacement path schedules one shared-falloff pass per radius class. The
radius-32/radius-8 fixture therefore issues 40 propagation iterations per
canvas per frame. Single-class frames still issue exactly their class radius.

## Replacement measurement status

The new-head rerun used Release binaries on Metal (M4 Max, macOS 26.5.2, AC
power) and the same million-control command:

```text
python3 scripts/perf/repeat_profile.py --target IRPerfGrid \
  --output <pair-arm> --repeats 1 --timeout 180 -- \
  --config-preset configs/perf/million.lua --wave-freeze --no-overlay \
  --pivot-origin --yaw 0 --auto-profile 300
```

The first guarded parent arm did not produce a measurement. The benchmark
lease reported two uncovered quiet-breach workloads, terminated the process,
and `fleet-run` reported `RESULT=ALIVE-TIMEOUT` after 180 seconds. Its manifest
recorded `fresh_report=false`, `clean=false`, and `quiet=unknown`, so no number
from that attempt is graded or committed. Five clean interleaved pairs and the
separate two-radius fixture timing remain required on a quiet macOS host.

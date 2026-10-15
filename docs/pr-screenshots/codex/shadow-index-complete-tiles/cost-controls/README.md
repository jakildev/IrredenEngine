# Preliminary bounded-cost controls

Release Metal, Apple M4 Max, 2560×1440 offscreen, battery power. Three runs of
each scene per arm, alternating scene order between rounds. Parent is
`90503255f84bf5046b04fc9a31788bc17c2447ed`; candidate is
`9256ac0473584699c98de0e1f470378224f7fff0`. Each run renders 275 frames,
excluding the first 68 for steady wall statistics. Commands, hashes, reports,
power samples and clean-exit logs are retained under each arm.

| Analytic box span / count | Median steady mean, before / after (ms) | Median shadow-query stage, before / after (ms) |
|---|---:|---:|
| 18 / 1 | 9.54 / 9.56 | 0.062 / 0.063 |
| 128 / 1 | 9.55 / 9.56 | 0.067 / 0.069 |
| 512 / 1 | 11.09 / 11.39 | 0.071 / 0.071 |
| 18 / 65 | 9.56 / 9.57 | 0.061 / 0.063 |

These are bounded attribution controls, **not throughput qualification**.
The driver does not request a quiet-host benchmark window: manifests report
`quiet: unknown`. It also includes screenshot readback and presentation pacing;
the large-span arm varies materially across runs. GPU stage rows include startup
and are not additive. No speedup or performance-equivalence claim follows from
the close small-scene means. The added index allocation is 2,228,228 bytes;
that is capacity, not measured peak GPU residency or process memory.

The separate million-entity moving-camera qualification requires a guarded
quiet window, profiler-off arms, and valid complete repetitions. Its interrupted
run is excluded from performance conclusions.

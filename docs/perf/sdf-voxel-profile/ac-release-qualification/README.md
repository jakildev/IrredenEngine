# AC Release baked-occupancy qualification slice

This retained macOS/Metal slice measures the density, 16-copy overlap and
moving-transform arms added by the baked-occupancy qualification harness. It
is a bounded complement to the earlier battery-powered baseline, not a general
representation winner or the missing Windows/OpenGL result.

## Provenance and controls

- Apple M4 Max (14 CPU cores), macOS 26.5.2, Metal, 36 GB memory.
- Source `8b8b21c93acbe4a96052c0c1637537cb2fa6db19`.
  The Release binary SHA-256 was
  `e3c1edcefe4d0a7d2ed50fbf472246549348384be5257c32064348e33085723f`.
- Every retained arm reports `AC Power`, `Release`, three clean runs and
  `QUIET=guarded`. Each run records 300 frames at yaw 45°, zoom 4, AO off,
  offscreen pacing and GPU stage instrumentation off. The first 75 frames of
  each run are excluded from the steady mean.
- Direct density 1 is the analytic control for both baked densities; density 2
  is not a valid direct-SDF option. Population and motion rows are matched
  direct/baked pairs.
- An earlier attempt began while the host was transitioning from battery to AC.
  Its manifests reported `Battery Power`; those runs are excluded and every
  affected arm was rerun after AC attachment stabilized.

## Results

| Arm | Representation | Frame mean ms | Steady mean ms | GPU envelope ms | Bake µs | Record bytes |
|---|---|---:|---:|---:|---:|---:|
| density 1 | direct | 15.417 | 15.207 | 5.997 | 0 | 0 |
| density 1 | baked | 12.937 | 13.600 | 5.063 | 18.0 | 63,888 |
| density 2 | baked | 15.633 | 17.203 | 5.042 | 100.7 | 444,528 |
| 16 copies, spaced | direct | 18.770 | 19.063 | 5.774 | 0 | 0 |
| 16 copies, spaced | baked | 22.733 | 22.733 | 5.853 | 17.7 | 1,022,208 |
| 16 copies, overlapped | direct | 19.780 | 19.877 | 6.814 | 0 | 0 |
| 16 copies, overlapped | baked | 40.270 | 40.033 | 20.676 | 18.0 | 1,022,208 |
| moving root | direct | 11.827 | 11.530 | 5.374 | 0 | 0 |
| moving root | baked | 13.383 | 13.203 | 5.003 | 17.7 | 63,888 |
| moving parent | direct | 12.080 | 12.023 | 5.498 | 0 | 0 |
| moving parent | baked | 12.347 | 12.807 | 5.123 | 17.7 | 63,888 |

The overlapped 16-copy baked case is the clear stress result in this slice:
its GPU envelope is roughly three times the direct control and its steady frame
mean is roughly twice as high. Spaced copies do not show that GPU amplification.
Density 2 keeps a similar GPU envelope to density 1 here, while increasing the
dense record payload by about seven times and the startup bake witness by about
five and a half times. The moving pairs keep baked GPU envelopes below their
direct controls, but their whole-frame means overlap enough that this three-run
slice does not establish a complete-frame win.

## Limits and remaining qualification

`record_bytes` is the dense occupancy record payload, not process RSS, peak
temporary allocation or Metal driver residency. This slice also does not carry
stage-timing-on pairs, presentation-mode captures, Windows/OpenGL measurements
or instrumentation-off/on comparisons. Those remain required before closing
the investigation. The table above records the accepted manifests and
summaries, not the rejected battery-transition attempt; raw runner outputs are
not checked in.

Reproduction uses the supported Release selector directly:

```bash
python3 scripts/perf/sdf_controls.py --release --output /tmp/sdf-release-ac \
  --rounds 3 --frames 300 --zoom 4
```

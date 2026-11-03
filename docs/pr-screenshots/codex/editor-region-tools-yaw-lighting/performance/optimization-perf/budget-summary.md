Steady samples exclude each report's warmup. The 60 Hz budget is 1000/60 ms; these are measured frames, not a throughput guarantee.

| Case | Runs | Steady p95 / p99 ms | Frames over 60 Hz budget | Overflow max entries / dropped |
|---|---:|---:|---:|---:|
| per-axis-zoom1-baseline | 5 | 24.189 / 26.160 | 929 / 1125 | 629 / 0 |
| per-axis-zoom1-candidate | 5 | 24.625 / 26.602 | 764 / 1125 | 629 / 0 |
| per-axis-zoom4-baseline | 5 | 21.514 / 22.575 | 521 / 1125 | 629 / 0 |
| per-axis-zoom4-candidate | 5 | 21.346 / 23.084 | 451 / 1125 | 629 / 0 |

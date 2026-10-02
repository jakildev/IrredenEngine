Steady samples exclude each report's warmup. The 60 Hz budget is 1000/60 ms; these are measured frames, not a throughput guarantee.

| Case | Runs | Steady p95 / p99 ms | Frames over 60 Hz budget | Overflow max entries / dropped |
|---|---:|---:|---:|---:|
| motion-wave0-zoom1-base1-cardinal | 3 | 9.786 / 10.843 | 0 / 405 | 0 / 0 |
| motion-wave0-zoom1-base1-diagonal | 3 | 10.560 / 11.176 | 0 / 405 | 0 / 0 |
| motion-wave0-zoom1-base1-sweep | 3 | 10.481 / 11.581 | 0 / 405 | 0 / 0 |
| motion-wave0-zoom1-base4-cardinal | 3 | 9.759 / 11.051 | 0 / 405 | 0 / 0 |
| motion-wave0-zoom1-base4-diagonal | 3 | 11.187 / 11.926 | 0 / 405 | 0 / 0 |
| motion-wave0-zoom1-base4-sweep | 3 | 10.878 / 11.708 | 0 / 405 | 0 / 0 |
| motion-wave0-zoom4-base4-cardinal | 3 | 12.565 / 13.173 | 0 / 405 | 0 / 0 |
| motion-wave0-zoom4-base4-diagonal | 3 | 11.037 / 11.783 | 0 / 405 | 0 / 0 |
| motion-wave0-zoom4-base4-sweep | 3 | 10.686 / 14.247 | 2 / 405 | 0 / 0 |
| motion-wave5-zoom1-base1-cardinal | 3 | 9.445 / 9.945 | 0 / 405 | 0 / 0 |
| motion-wave5-zoom1-base1-diagonal | 3 | 18.976 / 19.469 | 405 / 405 | 749109 / 0 |
| motion-wave5-zoom1-base1-sweep | 3 | 20.240 / 20.890 | 396 / 405 | 749109 / 0 |
| motion-wave5-zoom1-base4-cardinal | 3 | 12.569 / 13.469 | 1 / 405 | 0 / 0 |
| motion-wave5-zoom1-base4-diagonal | 3 | 20.853 / 21.846 | 405 / 405 | 749109 / 0 |
| motion-wave5-zoom1-base4-sweep | 3 | 20.353 / 21.394 | 396 / 405 | 749109 / 0 |
| motion-wave5-zoom4-base4-cardinal | 3 | 89.906 / 90.849 | 405 / 405 | 0 / 0 |
| motion-wave5-zoom4-base4-diagonal | 3 | 18.286 / 18.939 | 405 / 405 | 745398 / 0 |
| motion-wave5-zoom4-base4-sweep | 3 | 20.197 / 91.804 | 405 / 405 | 745368 / 0 |

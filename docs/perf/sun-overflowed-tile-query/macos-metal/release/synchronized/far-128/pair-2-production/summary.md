| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 85.780 | 85.780–85.780 | n/a |
| frame p95 | 88.030 | 88.030–88.030 | n/a |
| frame p99 | 93.670 | 93.670–93.670 | n/a |
| steady frame avg | 85.480 | 85.480–85.480 | n/a |
| steady frame p95 | 87.710 | 87.710–87.710 | n/a |
| steady frame p99 | 89.130 | 89.130–89.130 | n/a |
| GPU frame commandBufferSpans | 48.470 | 48.470–48.470 | n/a |
| GPU frame envelope | 73.203 | 73.203–73.203 | n/a |
| GPU bakeSunShadowMap | 0.833 | 0.833–0.833 | n/a |
| GPU buildLightOcclusionGrid | 1.144 | 1.144–1.144 | n/a |
| GPU computeLightVolume | 0.156 | 0.156–0.156 | n/a |
| GPU computeSunShadow | 0.188 | 0.188–0.188 | n/a |
| GPU entityCanvasToFb | 11.955 | 11.955–11.955 | n/a |
| GPU fbToScreen | 0.179 | 0.179–0.179 | n/a |
| GPU resolvePerAxisScreenDepth | 4.707 | 4.707–4.707 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 85.479 | 87.712 | 89.129 | 99.232 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 5.10 | 5.10–5.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 5400 | 0.000 (0.000) | 0 / 0 (0) |

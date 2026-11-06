| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 85.470 | 85.470–85.470 | n/a |
| frame p95 | 86.780 | 86.780–86.780 | n/a |
| frame p99 | 90.710 | 90.710–90.710 | n/a |
| steady frame avg | 85.230 | 85.230–85.230 | n/a |
| steady frame p95 | 86.740 | 86.740–86.740 | n/a |
| steady frame p99 | 87.270 | 87.270–87.270 | n/a |
| GPU frame commandBufferSpans | 48.226 | 48.226–48.226 | n/a |
| GPU frame envelope | 72.900 | 72.900–72.900 | n/a |
| GPU bakeSunShadowMap | 0.830 | 0.830–0.830 | n/a |
| GPU buildLightOcclusionGrid | 1.139 | 1.139–1.139 | n/a |
| GPU computeLightVolume | 0.152 | 0.152–0.152 | n/a |
| GPU computeSunShadow | 0.188 | 0.188–0.188 | n/a |
| GPU entityCanvasToFb | 11.921 | 11.921–11.921 | n/a |
| GPU fbToScreen | 0.183 | 0.183–0.183 | n/a |
| GPU resolvePerAxisScreenDepth | 4.759 | 4.759–4.759 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 85.230 | 86.740 | 87.273 | 88.354 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 5.10 | 5.10–5.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 5400 | 0.000 (0.000) | 0 / 0 (0) |

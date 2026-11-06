| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 82.250 | 82.250–82.250 | n/a |
| frame p95 | 83.780 | 83.780–83.780 | n/a |
| frame p99 | 86.790 | 86.790–86.790 | n/a |
| steady frame avg | 81.940 | 81.940–81.940 | n/a |
| steady frame p95 | 83.110 | 83.110–83.110 | n/a |
| steady frame p99 | 84.290 | 84.290–84.290 | n/a |
| GPU frame commandBufferSpans | 44.662 | 44.662–44.662 | n/a |
| GPU frame envelope | 69.695 | 69.695–69.695 | n/a |
| GPU bakeSunShadowMap | 0.820 | 0.820–0.820 | n/a |
| GPU buildLightOcclusionGrid | 1.177 | 1.177–1.177 | n/a |
| GPU computeLightVolume | 0.157 | 0.157–0.157 | n/a |
| GPU computeSunShadow | 0.173 | 0.173–0.173 | n/a |
| GPU entityCanvasToFb | 8.835 | 8.835–8.835 | n/a |
| GPU fbToScreen | 0.182 | 0.182–0.182 | n/a |
| GPU resolvePerAxisScreenDepth | 4.550 | 4.550–4.550 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 81.938 | 83.107 | 84.291 | 84.919 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 4.90 | 4.90–4.90 | 7 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 5400 | 0.000 (0.000) | 0 / 0 (0) |

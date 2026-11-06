| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 85.440 | 85.440–85.440 | n/a |
| frame p95 | 86.880 | 86.880–86.880 | n/a |
| frame p99 | 88.030 | 88.030–88.030 | n/a |
| steady frame avg | 85.300 | 85.300–85.300 | n/a |
| steady frame p95 | 86.730 | 86.730–86.730 | n/a |
| steady frame p99 | 87.910 | 87.910–87.910 | n/a |
| GPU frame commandBufferSpans | 48.457 | 48.457–48.457 | n/a |
| GPU frame envelope | 72.929 | 72.929–72.929 | n/a |
| GPU bakeSunShadowMap | 0.838 | 0.838–0.838 | n/a |
| GPU buildLightOcclusionGrid | 1.137 | 1.137–1.137 | n/a |
| GPU computeLightVolume | 0.155 | 0.155–0.155 | n/a |
| GPU computeSunShadow | 0.185 | 0.185–0.185 | n/a |
| GPU entityCanvasToFb | 12.032 | 12.032–12.032 | n/a |
| GPU fbToScreen | 0.181 | 0.181–0.181 | n/a |
| GPU resolvePerAxisScreenDepth | 4.646 | 4.646–4.646 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 85.296 | 86.732 | 87.908 | 88.301 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 5.10 | 5.10–5.10 | 7 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 5400 | 0.000 (0.000) | 0 / 0 (0) |

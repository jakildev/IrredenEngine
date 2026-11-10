| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 85.190 | 85.190–85.190 | n/a |
| frame p95 | 86.960 | 86.960–86.960 | n/a |
| frame p99 | 91.070 | 91.070–91.070 | n/a |
| steady frame avg | 84.940 | 84.940–84.940 | n/a |
| steady frame p95 | 86.590 | 86.590–86.590 | n/a |
| steady frame p99 | 87.660 | 87.660–87.660 | n/a |
| GPU frame commandBufferSpans | 47.470 | 47.470–47.470 | n/a |
| GPU frame envelope | 72.648 | 72.648–72.648 | n/a |
| GPU bakeSunShadowMap | 0.686 | 0.686–0.686 | n/a |
| GPU buildLightOcclusionGrid | 1.192 | 1.192–1.192 | n/a |
| GPU computeLightVolume | 0.155 | 0.155–0.155 | n/a |
| GPU computeSunShadow | 0.179 | 0.179–0.179 | n/a |
| GPU entityCanvasToFb | 11.919 | 11.919–11.919 | n/a |
| GPU fbToScreen | 0.184 | 0.184–0.184 | n/a |
| GPU resolvePerAxisScreenDepth | 4.588 | 4.588–4.588 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 84.944 | 86.589 | 87.656 | 91.067 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 5.10 | 5.10–5.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 5400 | 0.000 (0.000) | 0 / 0 (0) |

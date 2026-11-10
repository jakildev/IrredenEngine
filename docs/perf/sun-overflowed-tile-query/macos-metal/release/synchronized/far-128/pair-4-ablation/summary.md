| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 82.240 | 82.240–82.240 | n/a |
| frame p95 | 83.350 | 83.350–83.350 | n/a |
| frame p99 | 85.070 | 85.070–85.070 | n/a |
| steady frame avg | 82.190 | 82.190–82.190 | n/a |
| steady frame p95 | 83.440 | 83.440–83.440 | n/a |
| steady frame p99 | 85.070 | 85.070–85.070 | n/a |
| GPU frame commandBufferSpans | 45.150 | 45.150–45.150 | n/a |
| GPU frame envelope | 69.829 | 69.829–69.829 | n/a |
| GPU bakeSunShadowMap | 0.837 | 0.837–0.837 | n/a |
| GPU buildLightOcclusionGrid | 1.128 | 1.128–1.128 | n/a |
| GPU computeLightVolume | 0.155 | 0.155–0.155 | n/a |
| GPU computeSunShadow | 0.175 | 0.175–0.175 | n/a |
| GPU entityCanvasToFb | 8.848 | 8.848–8.848 | n/a |
| GPU fbToScreen | 0.179 | 0.179–0.179 | n/a |
| GPU resolvePerAxisScreenDepth | 4.680 | 4.680–4.680 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 82.187 | 83.439 | 85.070 | 86.245 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 4.90 | 4.90–4.90 | 7 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 5400 | 0.000 (0.000) | 0 / 0 (0) |

| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 20.825 | 19.610–22.040 |
| frame p95 | 20.730 | 20.550–20.910 |
| frame p99 | 32.655 | 32.520–32.790 |
| steady frame avg | 15.835 | 15.830–15.840 |
| steady frame p95 | 20.255 | 19.740–20.770 |
| steady frame p99 | 26.845 | 21.170–32.520 |
| GPU frame commandBufferSpans | 4.625 | 4.414–4.836 |
| GPU frame envelope | 4.625 | 4.414–4.836 |
| GPU canvasClear | 0.032 | 0.022–0.042 |
| GPU computeLightVolume | 2.016 | 1.980–2.053 |
| GPU computeSunShadow | 0.035 | 0.029–0.040 |
| GPU computeVoxelAO | 0.038 | 0.035–0.040 |
| GPU fbToScreen | 0.081 | 0.076–0.085 |
| GPU fogToTrixel | 0.019 | 0.019–0.020 |
| GPU lightingToTrixel | 0.037 | 0.035–0.039 |
| GPU trixelToFb | 0.335 | 0.330–0.340 |
| GPU voxelCardinalElect | 0.253 | 0.242–0.264 |
| GPU voxelCompact | 0.098 | 0.092–0.104 |
| GPU voxelStage1 | 0.309 | 0.305–0.313 |
| GPU voxelStage2 | 0.270 | 0.265–0.274 |
| GPU voxelSunFaces | 0.538 | 0.534–0.542 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 15.836 | 20.655 | 22.001 | 32.523 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.95 | 0.90–1.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |

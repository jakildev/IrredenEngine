| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 35.540 | 35.540–35.540 | n/a |
| frame p95 | 29.320 | 29.320–29.320 | n/a |
| frame p99 | 671.000 | 671.000–671.000 | n/a |
| steady frame avg | 24.620 | 24.620–24.620 | n/a |
| steady frame p95 | 27.650 | 27.650–27.650 | n/a |
| steady frame p99 | 29.320 | 29.320–29.320 | n/a |
| GPU frame commandBufferSpans | 7.495 | 7.495–7.495 | n/a |
| GPU frame envelope | 7.495 | 7.495–7.495 | n/a |
| GPU bakeSunShadowMap | 0.053 | 0.053–0.053 | n/a |
| GPU canvasClear | 0.037 | 0.037–0.037 | n/a |
| GPU computeLightVolume | 5.988 | 5.988–5.988 | n/a |
| GPU computeSunShadow | 0.111 | 0.111–0.111 | n/a |
| GPU computeVoxelAO | 0.052 | 0.052–0.052 | n/a |
| GPU fbToScreen | 0.037 | 0.037–0.037 | n/a |
| GPU fogToTrixel | 0.016 | 0.016–0.016 | n/a |
| GPU lightingToTrixel | 0.044 | 0.044–0.044 | n/a |
| GPU textToTrixel | 1.051 | 1.051–1.051 | n/a |
| GPU trixelToFb | 0.929 | 0.929–0.929 | n/a |
| GPU voxelCardinalElect | 1.792 | 1.792–1.792 | n/a |
| GPU voxelCompact | 0.799 | 0.799–0.799 | n/a |
| GPU voxelStage1 | 1.882 | 1.882–1.882 | n/a |
| GPU voxelStage2 | 1.791 | 1.791–1.791 | n/a |
| GPU voxelSunFaces | 1.013 | 1.013–1.013 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.619 | 27.648 | 29.320 | 29.320 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |

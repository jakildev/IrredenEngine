| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 31.420 | 31.420–31.420 | n/a |
| frame p95 | 32.660 | 32.660–32.660 | n/a |
| frame p99 | 682.780 | 682.780–682.780 | n/a |
| steady frame avg | 22.570 | 22.570–22.570 | n/a |
| steady frame p95 | 31.000 | 31.000–31.000 | n/a |
| steady frame p99 | 32.660 | 32.660–32.660 | n/a |
| GPU frame commandBufferSpans | 5.011 | 5.011–5.011 | n/a |
| GPU frame envelope | 5.011 | 5.011–5.011 | n/a |
| GPU bakeSunShadowMap | 0.333 | 0.333–0.333 | n/a |
| GPU canvasClear | 0.057 | 0.057–0.057 | n/a |
| GPU computeLightVolume | 3.009 | 3.009–3.009 | n/a |
| GPU computeSunShadow | 0.113 | 0.113–0.113 | n/a |
| GPU computeVoxelAO | 0.048 | 0.048–0.048 | n/a |
| GPU fbToScreen | 0.107 | 0.107–0.107 | n/a |
| GPU fogToTrixel | 0.026 | 0.026–0.026 | n/a |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 | n/a |
| GPU textToTrixel | 1.472 | 1.472–1.472 | n/a |
| GPU trixelToFb | 0.759 | 0.759–0.759 | n/a |
| GPU voxelCardinalElect | 0.408 | 0.408–0.408 | n/a |
| GPU voxelCompact | 0.420 | 0.420–0.420 | n/a |
| GPU voxelStage1 | 0.465 | 0.465–0.465 | n/a |
| GPU voxelStage2 | 0.404 | 0.404–0.404 | n/a |
| GPU voxelSunFaces | 1.010 | 1.010–1.010 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 22.567 | 31.000 | 32.662 | 32.662 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |

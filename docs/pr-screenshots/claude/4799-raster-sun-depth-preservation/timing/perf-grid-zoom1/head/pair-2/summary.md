| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 33.570 | 33.570–33.570 | n/a |
| frame p95 | 28.250 | 28.250–28.250 | n/a |
| frame p99 | 726.450 | 726.450–726.450 | n/a |
| steady frame avg | 22.400 | 22.400–22.400 | n/a |
| steady frame p95 | 25.010 | 25.010–25.010 | n/a |
| steady frame p99 | 28.250 | 28.250–28.250 | n/a |
| GPU frame commandBufferSpans | 5.830 | 5.830–5.830 | n/a |
| GPU frame envelope | 5.830 | 5.830–5.830 | n/a |
| GPU bakeSunShadowMap | 0.091 | 0.091–0.091 | n/a |
| GPU canvasClear | 0.079 | 0.079–0.079 | n/a |
| GPU computeLightVolume | 2.686 | 2.686–2.686 | n/a |
| GPU computeSunShadow | 0.068 | 0.068–0.068 | n/a |
| GPU computeVoxelAO | 0.058 | 0.058–0.058 | n/a |
| GPU fbToScreen | 0.123 | 0.123–0.123 | n/a |
| GPU fogToTrixel | 0.032 | 0.032–0.032 | n/a |
| GPU lightingToTrixel | 0.057 | 0.057–0.057 | n/a |
| GPU textToTrixel | 1.201 | 1.201–1.201 | n/a |
| GPU trixelToFb | 1.154 | 1.154–1.154 | n/a |
| GPU voxelCardinalElect | 0.467 | 0.467–0.467 | n/a |
| GPU voxelCompact | 0.294 | 0.294–0.294 | n/a |
| GPU voxelStage1 | 0.578 | 0.578–0.578 | n/a |
| GPU voxelStage2 | 0.465 | 0.465–0.465 | n/a |
| GPU voxelSunFaces | 1.088 | 1.088–1.088 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 22.402 | 25.009 | 28.252 | 28.252 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |

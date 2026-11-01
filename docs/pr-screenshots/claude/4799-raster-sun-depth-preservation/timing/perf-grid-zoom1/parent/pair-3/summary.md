| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 31.710 | 31.710–31.710 | n/a |
| frame p95 | 23.540 | 23.540–23.540 | n/a |
| frame p99 | 642.460 | 642.460–642.460 | n/a |
| steady frame avg | 21.190 | 21.190–21.190 | n/a |
| steady frame p95 | 23.170 | 23.170–23.170 | n/a |
| steady frame p99 | 23.540 | 23.540–23.540 | n/a |
| GPU frame commandBufferSpans | 5.404 | 5.404–5.404 | n/a |
| GPU frame envelope | 5.404 | 5.404–5.404 | n/a |
| GPU bakeSunShadowMap | 0.116 | 0.116–0.116 | n/a |
| GPU canvasClear | 0.125 | 0.125–0.125 | n/a |
| GPU computeLightVolume | 2.792 | 2.792–2.792 | n/a |
| GPU computeSunShadow | 0.128 | 0.128–0.128 | n/a |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 | n/a |
| GPU fbToScreen | 0.124 | 0.124–0.124 | n/a |
| GPU fogToTrixel | 0.032 | 0.032–0.032 | n/a |
| GPU lightingToTrixel | 0.057 | 0.057–0.057 | n/a |
| GPU textToTrixel | 1.659 | 1.659–1.659 | n/a |
| GPU trixelToFb | 1.221 | 1.221–1.221 | n/a |
| GPU voxelCardinalElect | 0.468 | 0.468–0.468 | n/a |
| GPU voxelCompact | 0.833 | 0.833–0.833 | n/a |
| GPU voxelStage1 | 0.576 | 0.576–0.576 | n/a |
| GPU voxelStage2 | 0.464 | 0.464–0.464 | n/a |
| GPU voxelSunFaces | 1.245 | 1.245–1.245 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 21.195 | 23.168 | 23.544 | 23.544 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |

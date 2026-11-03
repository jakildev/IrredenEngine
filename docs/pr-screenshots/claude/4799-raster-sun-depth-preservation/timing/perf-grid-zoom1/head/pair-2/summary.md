| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.230 | 32.230–32.230 | n/a |
| frame p95 | 24.050 | 24.050–24.050 | n/a |
| frame p99 | 640.680 | 640.680–640.680 | n/a |
| steady frame avg | 21.780 | 21.780–21.780 | n/a |
| steady frame p95 | 23.970 | 23.970–23.970 | n/a |
| steady frame p99 | 24.050 | 24.050–24.050 | n/a |
| GPU frame commandBufferSpans | 5.439 | 5.439–5.439 | n/a |
| GPU frame envelope | 5.439 | 5.439–5.439 | n/a |
| GPU bakeSunShadowMap | 0.121 | 0.121–0.121 | n/a |
| GPU canvasClear | 0.128 | 0.128–0.128 | n/a |
| GPU computeLightVolume | 2.796 | 2.796–2.796 | n/a |
| GPU computeSunShadow | 0.131 | 0.131–0.131 | n/a |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 | n/a |
| GPU fbToScreen | 0.125 | 0.125–0.125 | n/a |
| GPU fogToTrixel | 0.033 | 0.033–0.033 | n/a |
| GPU lightingToTrixel | 0.059 | 0.059–0.059 | n/a |
| GPU textToTrixel | 1.690 | 1.690–1.690 | n/a |
| GPU trixelToFb | 1.201 | 1.201–1.201 | n/a |
| GPU voxelCardinalElect | 0.470 | 0.470–0.470 | n/a |
| GPU voxelCompact | 0.880 | 0.880–0.880 | n/a |
| GPU voxelStage1 | 0.584 | 0.584–0.584 | n/a |
| GPU voxelStage2 | 0.466 | 0.466–0.466 | n/a |
| GPU voxelSunFaces | 1.254 | 1.254–1.254 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 21.780 | 23.970 | 24.050 | 24.050 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |

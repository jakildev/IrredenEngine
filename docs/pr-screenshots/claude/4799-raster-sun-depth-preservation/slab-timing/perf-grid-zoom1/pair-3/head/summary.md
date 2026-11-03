| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.200 | 32.200–32.200 | n/a |
| frame p95 | 23.920 | 23.920–23.920 | n/a |
| frame p99 | 668.180 | 668.180–668.180 | n/a |
| steady frame avg | 21.160 | 21.160–21.160 | n/a |
| steady frame p95 | 23.200 | 23.200–23.200 | n/a |
| steady frame p99 | 23.920 | 23.920–23.920 | n/a |
| GPU frame commandBufferSpans | 5.415 | 5.415–5.415 | n/a |
| GPU frame envelope | 5.415 | 5.415–5.415 | n/a |
| GPU bakeSunShadowMap | 0.121 | 0.121–0.121 | n/a |
| GPU canvasClear | 0.128 | 0.128–0.128 | n/a |
| GPU computeLightVolume | 2.791 | 2.791–2.791 | n/a |
| GPU computeSunShadow | 0.131 | 0.131–0.131 | n/a |
| GPU computeVoxelAO | 0.055 | 0.055–0.055 | n/a |
| GPU fbToScreen | 0.121 | 0.121–0.121 | n/a |
| GPU fogToTrixel | 0.034 | 0.034–0.034 | n/a |
| GPU lightingToTrixel | 0.058 | 0.058–0.058 | n/a |
| GPU textToTrixel | 1.687 | 1.687–1.687 | n/a |
| GPU trixelToFb | 1.180 | 1.180–1.180 | n/a |
| GPU voxelCardinalElect | 0.461 | 0.461–0.461 | n/a |
| GPU voxelCompact | 0.773 | 0.773–0.773 | n/a |
| GPU voxelStage1 | 0.560 | 0.560–0.560 | n/a |
| GPU voxelStage2 | 0.457 | 0.457–0.457 | n/a |
| GPU voxelSunFaces | 1.161 | 1.161–1.161 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 21.161 | 23.201 | 23.920 | 23.920 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |

| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.150 | 32.150–32.150 | n/a |
| frame p95 | 31.830 | 31.830–31.830 | n/a |
| frame p99 | 665.240 | 665.240–665.240 | n/a |
| steady frame avg | 21.420 | 21.420–21.420 | n/a |
| steady frame p95 | 23.540 | 23.540–23.540 | n/a |
| steady frame p99 | 31.830 | 31.830–31.830 | n/a |
| GPU frame commandBufferSpans | 5.446 | 5.446–5.446 | n/a |
| GPU frame envelope | 5.446 | 5.446–5.446 | n/a |
| GPU bakeSunShadowMap | 0.117 | 0.117–0.117 | n/a |
| GPU canvasClear | 0.123 | 0.123–0.123 | n/a |
| GPU computeLightVolume | 2.809 | 2.809–2.809 | n/a |
| GPU computeSunShadow | 0.133 | 0.133–0.133 | n/a |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 | n/a |
| GPU fbToScreen | 0.123 | 0.123–0.123 | n/a |
| GPU fogToTrixel | 0.034 | 0.034–0.034 | n/a |
| GPU lightingToTrixel | 0.057 | 0.057–0.057 | n/a |
| GPU textToTrixel | 1.691 | 1.691–1.691 | n/a |
| GPU trixelToFb | 1.221 | 1.221–1.221 | n/a |
| GPU voxelCardinalElect | 0.467 | 0.467–0.467 | n/a |
| GPU voxelCompact | 0.808 | 0.808–0.808 | n/a |
| GPU voxelStage1 | 0.565 | 0.565–0.565 | n/a |
| GPU voxelStage2 | 0.463 | 0.463–0.463 | n/a |
| GPU voxelSunFaces | 1.181 | 1.181–1.181 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 21.419 | 23.544 | 31.826 | 31.826 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |

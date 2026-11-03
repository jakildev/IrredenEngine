| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.610 | 32.610–32.610 | n/a |
| frame p95 | 24.690 | 24.690–24.690 | n/a |
| frame p99 | 646.570 | 646.570–646.570 | n/a |
| steady frame avg | 22.210 | 22.210–22.210 | n/a |
| steady frame p95 | 24.300 | 24.300–24.300 | n/a |
| steady frame p99 | 24.690 | 24.690–24.690 | n/a |
| GPU frame commandBufferSpans | 5.499 | 5.499–5.499 | n/a |
| GPU frame envelope | 5.499 | 5.499–5.499 | n/a |
| GPU bakeSunShadowMap | 0.120 | 0.120–0.120 | n/a |
| GPU canvasClear | 0.123 | 0.123–0.123 | n/a |
| GPU computeLightVolume | 2.851 | 2.851–2.851 | n/a |
| GPU computeSunShadow | 0.134 | 0.134–0.134 | n/a |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 | n/a |
| GPU fbToScreen | 0.125 | 0.125–0.125 | n/a |
| GPU fogToTrixel | 0.034 | 0.034–0.034 | n/a |
| GPU lightingToTrixel | 0.061 | 0.061–0.061 | n/a |
| GPU textToTrixel | 1.694 | 1.694–1.694 | n/a |
| GPU trixelToFb | 1.211 | 1.211–1.211 | n/a |
| GPU voxelCardinalElect | 0.471 | 0.471–0.471 | n/a |
| GPU voxelCompact | 0.894 | 0.894–0.894 | n/a |
| GPU voxelStage1 | 0.588 | 0.588–0.588 | n/a |
| GPU voxelStage2 | 0.468 | 0.468–0.468 | n/a |
| GPU voxelSunFaces | 1.253 | 1.253–1.253 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 22.209 | 24.300 | 24.687 | 24.687 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |

| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.480 | 32.480–32.480 | n/a |
| frame p95 | 34.110 | 34.110–34.110 | n/a |
| frame p99 | 671.940 | 671.940–671.940 | n/a |
| steady frame avg | 24.430 | 24.430–24.430 | n/a |
| steady frame p95 | 31.330 | 31.330–31.330 | n/a |
| steady frame p99 | 34.110 | 34.110–34.110 | n/a |
| GPU frame commandBufferSpans | 4.551 | 4.551–4.551 | n/a |
| GPU frame envelope | 4.551 | 4.551–4.551 | n/a |
| GPU bakeSunShadowMap | 0.318 | 0.318–0.318 | n/a |
| GPU canvasClear | 0.087 | 0.087–0.087 | n/a |
| GPU computeLightVolume | 2.533 | 2.533–2.533 | n/a |
| GPU computeSunShadow | 0.104 | 0.104–0.104 | n/a |
| GPU computeVoxelAO | 0.045 | 0.045–0.045 | n/a |
| GPU fbToScreen | 0.099 | 0.099–0.099 | n/a |
| GPU fogToTrixel | 0.025 | 0.025–0.025 | n/a |
| GPU lightingToTrixel | 0.044 | 0.044–0.044 | n/a |
| GPU textToTrixel | 1.279 | 1.279–1.279 | n/a |
| GPU trixelToFb | 1.095 | 1.095–1.095 | n/a |
| GPU voxelCardinalElect | 0.415 | 0.415–0.415 | n/a |
| GPU voxelCompact | 0.717 | 0.717–0.717 | n/a |
| GPU voxelStage1 | 0.443 | 0.443–0.443 | n/a |
| GPU voxelStage2 | 0.372 | 0.372–0.372 | n/a |
| GPU voxelSunFaces | 0.979 | 0.979–0.979 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.433 | 31.329 | 34.110 | 34.110 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |

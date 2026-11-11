| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 34.660 | 34.660–34.660 | n/a |
| frame p95 | 32.700 | 32.700–32.700 | n/a |
| frame p99 | 680.400 | 680.400–680.400 | n/a |
| steady frame avg | 24.590 | 24.590–24.590 | n/a |
| steady frame p95 | 30.810 | 30.810–30.810 | n/a |
| steady frame p99 | 32.700 | 32.700–32.700 | n/a |
| GPU frame commandBufferSpans | 4.622 | 4.622–4.622 | n/a |
| GPU frame envelope | 4.622 | 4.622–4.622 | n/a |
| GPU bakeSunShadowMap | 0.320 | 0.320–0.320 | n/a |
| GPU canvasClear | 0.098 | 0.098–0.098 | n/a |
| GPU computeLightVolume | 2.455 | 2.455–2.455 | n/a |
| GPU computeSunShadow | 0.102 | 0.102–0.102 | n/a |
| GPU computeVoxelAO | 0.045 | 0.045–0.045 | n/a |
| GPU fbToScreen | 0.097 | 0.097–0.097 | n/a |
| GPU fogToTrixel | 0.025 | 0.025–0.025 | n/a |
| GPU lightingToTrixel | 0.047 | 0.047–0.047 | n/a |
| GPU textToTrixel | 1.303 | 1.303–1.303 | n/a |
| GPU trixelToFb | 1.115 | 1.115–1.115 | n/a |
| GPU voxelCardinalElect | 0.371 | 0.371–0.371 | n/a |
| GPU voxelCompact | 0.792 | 0.792–0.792 | n/a |
| GPU voxelStage1 | 0.499 | 0.499–0.499 | n/a |
| GPU voxelStage2 | 0.361 | 0.361–0.361 | n/a |
| GPU voxelSunFaces | 0.975 | 0.975–0.975 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.588 | 30.808 | 32.695 | 32.695 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.50 | 1.50–1.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |

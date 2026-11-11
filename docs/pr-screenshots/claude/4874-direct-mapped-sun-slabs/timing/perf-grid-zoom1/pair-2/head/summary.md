| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 34.530 | 34.530–34.530 | n/a |
| frame p95 | 36.840 | 36.840–36.840 | n/a |
| frame p99 | 673.480 | 673.480–673.480 | n/a |
| steady frame avg | 24.860 | 24.860–24.860 | n/a |
| steady frame p95 | 31.190 | 31.190–31.190 | n/a |
| steady frame p99 | 36.840 | 36.840–36.840 | n/a |
| GPU frame commandBufferSpans | 4.827 | 4.827–4.827 | n/a |
| GPU frame envelope | 4.827 | 4.827–4.827 | n/a |
| GPU bakeSunShadowMap | 0.318 | 0.318–0.318 | n/a |
| GPU canvasClear | 0.058 | 0.058–0.058 | n/a |
| GPU computeLightVolume | 2.895 | 2.895–2.895 | n/a |
| GPU computeSunShadow | 0.113 | 0.113–0.113 | n/a |
| GPU computeVoxelAO | 0.048 | 0.048–0.048 | n/a |
| GPU fbToScreen | 0.104 | 0.104–0.104 | n/a |
| GPU fogToTrixel | 0.025 | 0.025–0.025 | n/a |
| GPU lightingToTrixel | 0.045 | 0.045–0.045 | n/a |
| GPU textToTrixel | 1.380 | 1.380–1.380 | n/a |
| GPU trixelToFb | 0.757 | 0.757–0.757 | n/a |
| GPU voxelCardinalElect | 0.414 | 0.414–0.414 | n/a |
| GPU voxelCompact | 0.416 | 0.416–0.416 | n/a |
| GPU voxelStage1 | 0.472 | 0.472–0.472 | n/a |
| GPU voxelStage2 | 0.395 | 0.395–0.395 | n/a |
| GPU voxelSunFaces | 0.933 | 0.933–0.933 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.864 | 31.188 | 36.836 | 36.836 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.50 | 1.50–1.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |

| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.270 | 32.270–32.270 | n/a |
| frame p95 | 23.900 | 23.900–23.900 | n/a |
| frame p99 | 651.670 | 651.670–651.670 | n/a |
| steady frame avg | 21.840 | 21.840–21.840 | n/a |
| steady frame p95 | 23.690 | 23.690–23.690 | n/a |
| steady frame p99 | 23.900 | 23.900–23.900 | n/a |
| GPU frame commandBufferSpans | 5.417 | 5.417–5.417 | n/a |
| GPU frame envelope | 5.417 | 5.417–5.417 | n/a |
| GPU bakeSunShadowMap | 0.119 | 0.119–0.119 | n/a |
| GPU canvasClear | 0.129 | 0.129–0.129 | n/a |
| GPU computeLightVolume | 2.832 | 2.832–2.832 | n/a |
| GPU computeSunShadow | 0.130 | 0.130–0.130 | n/a |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 | n/a |
| GPU fbToScreen | 0.125 | 0.125–0.125 | n/a |
| GPU fogToTrixel | 0.033 | 0.033–0.033 | n/a |
| GPU lightingToTrixel | 0.057 | 0.057–0.057 | n/a |
| GPU textToTrixel | 1.702 | 1.702–1.702 | n/a |
| GPU trixelToFb | 1.236 | 1.236–1.236 | n/a |
| GPU voxelCardinalElect | 0.470 | 0.470–0.470 | n/a |
| GPU voxelCompact | 0.842 | 0.842–0.842 | n/a |
| GPU voxelStage1 | 0.580 | 0.580–0.580 | n/a |
| GPU voxelStage2 | 0.467 | 0.467–0.467 | n/a |
| GPU voxelSunFaces | 1.244 | 1.244–1.244 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 21.842 | 23.687 | 23.901 | 23.901 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |

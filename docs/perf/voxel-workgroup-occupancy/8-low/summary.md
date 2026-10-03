| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 16.460 | 16.130–16.790 |
| frame p95 | 12.995 | 12.780–13.210 |
| frame p99 | 21.180 | 20.340–22.020 |
| steady frame avg | 10.815 | 10.550–11.080 |
| steady frame p95 | 12.545 | 12.300–12.790 |
| steady frame p99 | 13.720 | 13.270–14.170 |
| GPU frame commandBufferSpans | 7.748 | 7.604–7.892 |
| GPU frame envelope | 7.748 | 7.604–7.892 |
| GPU canvasClear | 0.012 | 0.012–0.012 |
| GPU computeLightVolume | 4.550 | 4.493–4.606 |
| GPU computeSunShadow | 0.025 | 0.024–0.025 |
| GPU computeVoxelAO | 0.029 | 0.029–0.030 |
| GPU fbToScreen | 0.058 | 0.058–0.059 |
| GPU fogToTrixel | 0.013 | 0.013–0.013 |
| GPU lightingToTrixel | 0.025 | 0.025–0.025 |
| GPU trixelToFb | 0.296 | 0.287–0.305 |
| GPU voxelCardinalElect | 1.742 | 1.653–1.831 |
| GPU voxelCompact | 0.125 | 0.123–0.127 |
| GPU voxelStage1 | 2.788 | 2.710–2.866 |
| GPU voxelStage2 | 0.899 | 0.896–0.903 |
| GPU voxelSunFaces | 0.494 | 0.481–0.507 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 10.815 | 12.752 | 13.868 | 14.175 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.70 | 0.70–0.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |
| 2 | True | 120 / 120 | 0 | 120 | 0.000 (0.000) | 0 / 0 (0) |

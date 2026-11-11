| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 33.270 | 33.270–33.270 | n/a |
| frame p95 | 33.190 | 33.190–33.190 | n/a |
| frame p99 | 671.420 | 671.420–671.420 | n/a |
| steady frame avg | 24.930 | 24.930–24.930 | n/a |
| steady frame p95 | 31.960 | 31.960–31.960 | n/a |
| steady frame p99 | 33.190 | 33.190–33.190 | n/a |
| GPU frame commandBufferSpans | 4.318 | 4.318–4.318 | n/a |
| GPU frame envelope | 4.318 | 4.318–4.318 | n/a |
| GPU bakeSunShadowMap | 0.301 | 0.301–0.301 | n/a |
| GPU canvasClear | 0.082 | 0.082–0.082 | n/a |
| GPU computeLightVolume | 2.346 | 2.346–2.346 | n/a |
| GPU computeSunShadow | 0.093 | 0.093–0.093 | n/a |
| GPU computeVoxelAO | 0.062 | 0.062–0.062 | n/a |
| GPU fbToScreen | 0.090 | 0.090–0.090 | n/a |
| GPU fogToTrixel | 0.023 | 0.023–0.023 | n/a |
| GPU lightingToTrixel | 0.042 | 0.042–0.042 | n/a |
| GPU textToTrixel | 1.197 | 1.197–1.197 | n/a |
| GPU trixelToFb | 1.063 | 1.063–1.063 | n/a |
| GPU voxelCardinalElect | 0.354 | 0.354–0.354 | n/a |
| GPU voxelCompact | 0.639 | 0.639–0.639 | n/a |
| GPU voxelStage1 | 0.449 | 0.449–0.449 | n/a |
| GPU voxelStage2 | 0.350 | 0.350–0.350 | n/a |
| GPU voxelSunFaces | 0.919 | 0.919–0.919 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.929 | 31.959 | 33.188 | 33.188 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |

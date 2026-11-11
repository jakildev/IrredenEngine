| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 11.370 | 11.370–11.370 | n/a |
| frame p95 | 18.690 | 18.690–18.690 | n/a |
| frame p99 | 22.570 | 22.570–22.570 | n/a |
| steady frame avg | 11.280 | 11.280–11.280 | n/a |
| steady frame p95 | 18.680 | 18.680–18.680 | n/a |
| steady frame p99 | 20.850 | 20.850–20.850 | n/a |
| GPU frame commandBufferSpans | 2.457 | 2.457–2.457 | n/a |
| GPU frame envelope | 2.457 | 2.457–2.457 | n/a |
| GPU bakeSunShadowMap | 1.420 | 1.420–1.420 | n/a |
| GPU canvasClear | 0.031 | 0.031–0.031 | n/a |
| GPU cardinalSurfaceFb | 1.899 | 1.899–1.899 | n/a |
| GPU computeLightVolume | 0.354 | 0.354–0.354 | n/a |
| GPU computeSunShadow | 0.052 | 0.052–0.052 | n/a |
| GPU computeVoxelAO | 0.099 | 0.099–0.099 | n/a |
| GPU fbToScreen | 0.128 | 0.128–0.128 | n/a |
| GPU lightingToTrixel | 0.059 | 0.059–0.059 | n/a |
| GPU trixelToFb | 1.653 | 1.653–1.653 | n/a |
| GPU voxelCardinalElect | 0.212 | 0.212–0.212 | n/a |
| GPU voxelCompact | 0.126 | 0.126–0.126 | n/a |
| GPU voxelStage1 | 0.101 | 0.101–0.101 | n/a |
| GPU voxelStage2 | 0.206 | 0.206–0.206 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 11.285 | 18.684 | 20.849 | 22.639 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.70 | 0.70–0.70 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 0.000 (0.000) | 0 / 0 (0) |

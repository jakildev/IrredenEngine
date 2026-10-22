| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 68.890 | 68.890–68.890 | n/a |
| frame p95 | 72.180 | 72.180–72.180 | n/a |
| frame p99 | 77.320 | 77.320–77.320 | n/a |
| steady frame avg | 67.910 | 67.910–67.910 | n/a |
| steady frame p95 | 70.380 | 70.380–70.380 | n/a |
| steady frame p99 | 71.680 | 71.680–71.680 | n/a |
| GPU frame commandBufferSpans | 42.035 | 42.035–42.035 | n/a |
| GPU frame envelope | 42.035 | 42.035–42.035 | n/a |
| GPU canvasClear | 0.021 | 0.021–0.021 | n/a |
| GPU computeLightVolume | 0.036 | 0.036–0.036 | n/a |
| GPU computeSunShadow | 0.071 | 0.071–0.071 | n/a |
| GPU computeVoxelAO | 0.027 | 0.027–0.027 | n/a |
| GPU computeVoxelAoPerAxis | 0.147 | 0.147–0.147 | n/a |
| GPU entityCanvasToFb | 3.846 | 3.846–3.846 | n/a |
| GPU fbToScreen | 0.029 | 0.029–0.029 | n/a |
| GPU lightingToTrixel | 0.011 | 0.011–0.011 | n/a |
| GPU perAxisCellCompact | 0.112 | 0.112–0.112 | n/a |
| GPU perAxisSurfaceFb | 0.463 | 0.463–0.463 | n/a |
| GPU resolvePerAxisScreenDepth | 0.061 | 0.061–0.061 | n/a |
| GPU trixelToFb | 0.046 | 0.046–0.046 | n/a |
| GPU voxelCompact | 0.045 | 0.045–0.045 | n/a |
| GPU voxelPerAxisFinalize | 0.072 | 0.072–0.072 | n/a |
| GPU voxelPerAxisOverflow | 0.026 | 0.026–0.026 | n/a |
| GPU voxelPerAxisStore | 0.754 | 0.754–0.754 | n/a |
| GPU voxelStage1 | 0.020 | 0.020–0.020 | n/a |
| GPU voxelStage2 | 0.009 | 0.009–0.009 | n/a |
| GPU voxelSunFaces | 0.076 | 0.076–0.076 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 67.911 | 70.384 | 71.684 | 72.409 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 4.10 | 4.10–4.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 360 | 34.377 (0.000) | 0 / 0 (360) |

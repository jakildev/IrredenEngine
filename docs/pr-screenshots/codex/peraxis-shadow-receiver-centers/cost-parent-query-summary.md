| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 8.710 | 8.680–8.740 |
| frame p95 | 9.447 | 9.190–9.800 |
| frame p99 | 10.583 | 10.530–10.660 |
| steady frame avg | 8.687 | 8.670–8.720 |
| steady frame p95 | 9.537 | 9.270–9.960 |
| steady frame p99 | 10.633 | 10.530–10.780 |
| GPU frame commandBufferSpans | 3.722 | 3.170–4.229 |
| GPU frame envelope | 3.993 | 3.455–4.499 |
| GPU canvasClear | 0.159 | 0.103–0.218 |
| GPU computeLightVolume | 0.086 | 0.081–0.089 |
| GPU computeSunShadow | 0.077 | 0.074–0.082 |
| GPU computeVoxelAO | 0.061 | 0.051–0.069 |
| GPU computeVoxelAoPerAxis | 1.418 | 1.165–1.554 |
| GPU fbToScreen | 0.090 | 0.084–0.098 |
| GPU lightingOverflow | 0.756 | 0.748–0.767 |
| GPU lightingPerAxis | 0.063 | 0.059–0.066 |
| GPU lightingToTrixel | 0.025 | 0.023–0.028 |
| GPU perAxisCellCompact | 0.162 | 0.157–0.165 |
| GPU perAxisScatter | 0.120 | 0.114–0.126 |
| GPU resolvePerAxisScreenDepth | 0.058 | 0.056–0.060 |
| GPU trixelToFb | 0.467 | 0.379–0.532 |
| GPU voxelCompact | 0.080 | 0.053–0.106 |
| GPU voxelPerAxisFinalize | 0.193 | 0.187–0.197 |
| GPU voxelPerAxisOverflow | 0.239 | 0.216–0.252 |
| GPU voxelPerAxisStore | 1.833 | 1.146–2.499 |
| GPU voxelSunFaces | 0.122 | 0.096–0.151 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 242 of each run excluded | 2187 | 8.687 | 9.605 | 10.663 | 164.082 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 971 / 971 | 0 | 973 | 22.500 (0.000) | 7 / 0 (971) |
| 2 | True | 971 / 971 | 0 | 973 | 22.500 (0.000) | 7 / 0 (971) |
| 3 | True | 971 / 971 | 0 | 973 | 22.500 (0.000) | 7 / 0 (971) |

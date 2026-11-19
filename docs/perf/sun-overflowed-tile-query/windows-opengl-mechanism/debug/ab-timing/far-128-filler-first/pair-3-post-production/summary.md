| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 116.560 | 116.560–116.560 | n/a |
| frame p95 | 206.060 | 206.060–206.060 | n/a |
| frame p99 | 247.070 | 247.070–247.070 | n/a |
| steady frame avg | 105.890 | 105.890–105.890 | n/a |
| steady frame p95 | 141.220 | 141.220–141.220 | n/a |
| steady frame p99 | 162.160 | 162.160–162.160 | n/a |
| frame split INPUT | 0.347 | 0.347–0.347 | n/a |
| frame split UPDATE | 72.430 | 72.430–72.430 | n/a |
| frame split RENDER | 42.114 | 42.114–42.114 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.669 | 1.669–1.669 | n/a |
| GPU exclusive stage sum | 39.331 | 39.331–39.331 | n/a |
| GPU bakeSunShadowMap | 0.128 | 0.128–0.128 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 0.122 | 0.122–0.122 | n/a |
| GPU computeLightVolume | 0.028 | 0.028–0.028 | n/a |
| GPU computeSunShadow | 0.044 | 0.044–0.044 | n/a |
| GPU computeVoxelAO | 0.008 | 0.008–0.008 | n/a |
| GPU entityCanvasToFb | 38.751 | 38.751–38.751 | n/a |
| GPU fbToScreen | 0.020 | 0.020–0.020 | n/a |
| GPU lightingToTrixel | 0.034 | 0.034–0.034 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.056 | 0.056–0.056 | n/a |
| GPU voxelStage2 | 0.019 | 0.019–0.019 | n/a |
| GPU voxelSunFaces | 0.060 | 0.060–0.060 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 105.893 | 141.220 | 162.156 | 167.374 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.50 | 6.50–6.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.

| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 103.150 | 103.150–103.150 | n/a |
| frame p95 | 112.900 | 112.900–112.900 | n/a |
| frame p99 | 116.060 | 116.060–116.060 | n/a |
| steady frame avg | 102.020 | 102.020–102.020 | n/a |
| steady frame p95 | 111.720 | 111.720–111.720 | n/a |
| steady frame p99 | 115.570 | 115.570–115.570 | n/a |
| frame split INPUT | 0.659 | 0.659–0.659 | n/a |
| frame split UPDATE | 7.047 | 7.047–7.047 | n/a |
| frame split RENDER | 15.453 | 15.453–15.453 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 79.988 | 79.988–79.988 | n/a |
| GPU exclusive stage sum | 64.687 | 64.687–64.687 | n/a |
| GPU bakeSunShadowMap | 0.085 | 0.085–0.085 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.010 | 0.010–0.010 | n/a |
| GPU cardinalSurfaceFb | 0.133 | 0.133–0.133 | n/a |
| GPU computeLightVolume | 0.035 | 0.035–0.035 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.031 | 0.031–0.031 | n/a |
| GPU entityCanvasToFb | 64.185 | 64.185–64.185 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.034 | 0.034–0.034 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.026 | 0.026–0.026 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.025 | 0.025–0.025 | n/a |
| GPU voxelStage2 | 0.014 | 0.014–0.014 | n/a |
| GPU voxelSunFaces | 0.059 | 0.059–0.059 | n/a |
| input_to_present mean | 197.549 | 197.549–197.549 | n/a |
| input_to_present p99 | 216.747 | 216.747–216.747 | n/a |
| cursor_to_present mean | 196.420 | 196.420–196.420 | n/a |
| cursor_to_present p99 | 215.527 | 215.527–215.527 | n/a |
| frame_start_to_submit mean | 20.912 | 20.912–20.912 | n/a |
| frame_start_to_submit p99 | 30.308 | 30.308–30.308 | n/a |
| submit_to_present mean | 182.998 | 182.998–182.998 | n/a |
| submit_to_present p99 | 199.561 | 199.561–199.561 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 102.016 | 111.722 | 115.569 | 116.063 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.10 | 6.10–6.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.1138 |

Render device counters: unsupported by this backend.

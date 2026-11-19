| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 36.020 | 36.020–36.020 | n/a |
| frame p95 | 39.860 | 39.860–39.860 | n/a |
| frame p99 | 73.450 | 73.450–73.450 | n/a |
| steady frame avg | 35.080 | 35.080–35.080 | n/a |
| steady frame p95 | 39.200 | 39.200–39.200 | n/a |
| steady frame p99 | 40.170 | 40.170–40.170 | n/a |
| frame split INPUT | 0.398 | 0.398–0.398 | n/a |
| frame split UPDATE | 2.676 | 2.676–2.676 | n/a |
| frame split RENDER | 14.962 | 14.962–14.962 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 17.981 | 17.981–17.981 | n/a |
| GPU exclusive stage sum | 0.787 | 0.787–0.787 | n/a |
| GPU bakeSunShadowMap | 0.086 | 0.086–0.086 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.009 | 0.009–0.009 | n/a |
| GPU cardinalSurfaceFb | 0.085 | 0.085–0.085 | n/a |
| GPU computeLightVolume | 0.029 | 0.029–0.029 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.022 | 0.022–0.022 | n/a |
| GPU entityCanvasToFb | 0.367 | 0.367–0.367 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.024 | 0.024–0.024 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.022 | 0.022–0.022 | n/a |
| GPU voxelStage2 | 0.012 | 0.012–0.012 | n/a |
| GPU voxelSunFaces | 0.060 | 0.060–0.060 | n/a |
| input_to_present mean | 68.457 | 68.457–68.457 | n/a |
| input_to_present p99 | 76.496 | 76.496–76.496 | n/a |
| cursor_to_present mean | 67.197 | 67.197–67.197 | n/a |
| cursor_to_present p99 | 75.225 | 75.225–75.225 | n/a |
| frame_start_to_submit mean | 15.749 | 15.749–15.749 | n/a |
| frame_start_to_submit p99 | 23.376 | 23.376–23.376 | n/a |
| submit_to_present mean | 54.325 | 54.325–54.325 | n/a |
| submit_to_present p99 | 64.036 | 64.036–64.036 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 35.080 | 39.201 | 40.173 | 40.842 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.20 | 2.20–2.20 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.1141 |

Render device counters: unsupported by this backend.

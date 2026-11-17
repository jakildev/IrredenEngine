| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 11.370 | 11.370–11.370 | n/a |
| frame p95 | 11.800 | 11.800–11.800 | n/a |
| frame p99 | 12.630 | 12.630–12.630 | n/a |
| steady frame avg | 11.380 | 11.380–11.380 | n/a |
| steady frame p95 | 11.860 | 11.860–11.860 | n/a |
| steady frame p99 | 12.640 | 12.640–12.640 | n/a |
| frame split INPUT | 0.002 | 0.002–0.002 | n/a |
| frame split UPDATE | 0.014 | 0.014–0.014 | n/a |
| frame split RENDER | 0.853 | 0.853–0.853 | n/a |
| frame split GPU | 10.264 | 10.264–10.264 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder | 0.241 | 0.241–0.241 | n/a |
| GPU frame commandBufferSpans | 10.264 | 10.264–10.264 | n/a |
| GPU frame envelope | 10.264 | 10.264–10.264 | n/a |
| GPU exclusive stage sum | 10.326 | 10.326–10.326 | n/a |
| GPU bakeSunShadowMap | 0.107 | 0.107–0.107 | n/a |
| GPU canvasClear | 0.047 | 0.047–0.047 | n/a |
| GPU computeLightVolume | 0.245 | 0.245–0.245 | n/a |
| GPU computeSunShadow | 0.554 | 0.554–0.554 | n/a |
| GPU computeVoxelAO | 0.016 | 0.016–0.016 | n/a |
| GPU computeVoxelAoPerAxis | 0.048 | 0.048–0.048 | n/a |
| GPU fbToScreen | 0.032 | 0.032–0.032 | n/a |
| GPU fogOverflow | 0.010 | 0.010–0.010 | n/a |
| GPU fogPerAxis | 0.024 | 0.024–0.024 | n/a |
| GPU fogToTrixel | 0.011 | 0.011–0.011 | n/a |
| GPU lightingOverflow | 0.122 | 0.122–0.122 | n/a |
| GPU lightingPerAxis | 0.038 | 0.038–0.038 | n/a |
| GPU lightingToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU perAxisCellCompact | 0.079 | 0.079–0.079 | n/a |
| GPU perAxisScatter | 0.651 | 0.651–0.651 | n/a |
| GPU trixelToFb | 0.868 | 0.868–0.868 | n/a |
| GPU voxelCompact | 0.103 | 0.103–0.103 | n/a |
| GPU voxelPerAxisFinalize | 1.370 | 1.370–1.370 | n/a |
| GPU voxelPerAxisOverflow | 2.438 | 2.438–2.438 | n/a |
| GPU voxelPerAxisStore | 0.744 | 0.744–0.744 | n/a |
| GPU voxelSunFaces | 2.803 | 2.803–2.803 | n/a |
| input_to_present mean | 14.807 | 14.807–14.807 | n/a |
| input_to_present p99 | 23.671 | 23.671–23.671 | n/a |
| cursor_to_present mean | 11.217 | 11.217–11.217 | n/a |
| cursor_to_present p99 | 12.459 | 12.459–12.459 | n/a |
| frame_start_to_submit mean | 0.908 | 0.908–0.908 | n/a |
| frame_start_to_submit p99 | 1.689 | 1.689–1.689 | n/a |
| submit_to_present mean | 10.339 | 10.339–10.339 | n/a |
| submit_to_present p99 | 10.972 | 10.972–10.972 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 226 | 11.384 | 11.855 | 12.644 | 13.175 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.70 | 0.70–0.70 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 301 / 301 | 0 | 301 | 45.000 (0.000) | 51719 / 0 (301) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 226 / 226 | 0 | 0.001 |

| Render device counter | Steady per frame, mean over runs | Run min–max | Max in a frame | Run total, mean |
|---|---:|---:|---:|---:|
| bufferPartialWriteAllocations | 0.000 | 0.000–0.000 | 0 | 0.0 |
| bufferPartialWriteAllocatedBytes | 0.000 | 0.000–0.000 | 0 | 0.0 |
| bufferStagedPatches | 57.000 | 57.000–57.000 | 57 | 17157.0 |
| bufferStagedBytes | 16412.000 | 16412.000–16412.000 | 16412 | 4940012.0 |
| bufferFullReplacements | 27.000 | 27.000–27.000 | 27 | 8127.0 |
| bufferMidFrameWaits | 0.000 | 0.000–0.000 | 0 | 0.0 |

| GPU process allocation peak | Mean MiB | Run min–max MiB |
|---|---:|---:|
| sampled at each frame end | 735.8 | 735.8–735.8 |

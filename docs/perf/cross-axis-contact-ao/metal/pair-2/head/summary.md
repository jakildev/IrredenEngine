| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 11.340 | 11.340–11.340 | n/a |
| frame p95 | 11.680 | 11.680–11.680 | n/a |
| frame p99 | 12.930 | 12.930–12.930 | n/a |
| steady frame avg | 11.370 | 11.370–11.370 | n/a |
| steady frame p95 | 11.710 | 11.710–11.710 | n/a |
| steady frame p99 | 12.930 | 12.930–12.930 | n/a |
| frame split INPUT | 0.002 | 0.002–0.002 | n/a |
| frame split UPDATE | 0.014 | 0.014–0.014 | n/a |
| frame split RENDER | 0.826 | 0.826–0.826 | n/a |
| frame split GPU | 10.265 | 10.265–10.265 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder | 0.236 | 0.236–0.236 | n/a |
| GPU frame commandBufferSpans | 10.265 | 10.265–10.265 | n/a |
| GPU frame envelope | 10.265 | 10.265–10.265 | n/a |
| GPU exclusive stage sum | 10.340 | 10.340–10.340 | n/a |
| GPU bakeSunShadowMap | 0.110 | 0.110–0.110 | n/a |
| GPU canvasClear | 0.047 | 0.047–0.047 | n/a |
| GPU computeLightVolume | 0.246 | 0.246–0.246 | n/a |
| GPU computeSunShadow | 0.556 | 0.556–0.556 | n/a |
| GPU computeVoxelAO | 0.016 | 0.016–0.016 | n/a |
| GPU computeVoxelAoPerAxis | 0.046 | 0.046–0.046 | n/a |
| GPU fbToScreen | 0.031 | 0.031–0.031 | n/a |
| GPU fogOverflow | 0.010 | 0.010–0.010 | n/a |
| GPU fogPerAxis | 0.024 | 0.024–0.024 | n/a |
| GPU fogToTrixel | 0.012 | 0.012–0.012 | n/a |
| GPU lightingOverflow | 0.121 | 0.121–0.121 | n/a |
| GPU lightingPerAxis | 0.036 | 0.036–0.036 | n/a |
| GPU lightingToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU perAxisCellCompact | 0.079 | 0.079–0.079 | n/a |
| GPU perAxisScatter | 0.649 | 0.649–0.649 | n/a |
| GPU trixelToFb | 0.894 | 0.894–0.894 | n/a |
| GPU voxelCompact | 0.098 | 0.098–0.098 | n/a |
| GPU voxelPerAxisFinalize | 1.369 | 1.369–1.369 | n/a |
| GPU voxelPerAxisOverflow | 2.434 | 2.434–2.434 | n/a |
| GPU voxelPerAxisStore | 0.745 | 0.745–0.745 | n/a |
| GPU voxelSunFaces | 2.801 | 2.801–2.801 | n/a |
| input_to_present mean | 14.849 | 14.849–14.849 | n/a |
| input_to_present p99 | 23.530 | 23.530–23.530 | n/a |
| cursor_to_present mean | 11.198 | 11.198–11.198 | n/a |
| cursor_to_present p99 | 12.717 | 12.717–12.717 | n/a |
| frame_start_to_submit mean | 0.879 | 0.879–0.879 | n/a |
| frame_start_to_submit p99 | 1.712 | 1.712–1.712 | n/a |
| submit_to_present mean | 10.348 | 10.348–10.348 | n/a |
| submit_to_present p99 | 11.051 | 11.051–11.051 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 11.368 | 11.709 | 12.932 | 13.370 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.70 | 0.70–0.70 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 45.000 (0.000) | 51719 / 0 (300) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0011 |

| Render device counter | Steady per frame, mean over runs | Run min–max | Max in a frame | Run total, mean |
|---|---:|---:|---:|---:|
| bufferPartialWriteAllocations | 0.000 | 0.000–0.000 | 0 | 0.0 |
| bufferPartialWriteAllocatedBytes | 0.000 | 0.000–0.000 | 0 | 0.0 |
| bufferStagedPatches | 57.000 | 57.000–57.000 | 57 | 17100.0 |
| bufferStagedBytes | 16412.000 | 16412.000–16412.000 | 16412 | 4923600.0 |
| bufferFullReplacements | 27.000 | 27.000–27.000 | 27 | 8100.0 |
| bufferMidFrameWaits | 0.000 | 0.000–0.000 | 0 | 0.0 |

| GPU process allocation peak | Mean MiB | Run min–max MiB |
|---|---:|---:|
| sampled at each frame end | 735.8 | 735.8–735.8 |

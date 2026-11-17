| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 11.290 | 11.290–11.290 | n/a |
| frame p95 | 11.530 | 11.530–11.530 | n/a |
| frame p99 | 11.680 | 11.680–11.680 | n/a |
| steady frame avg | 11.280 | 11.280–11.280 | n/a |
| steady frame p95 | 11.530 | 11.530–11.530 | n/a |
| steady frame p99 | 11.680 | 11.680–11.680 | n/a |
| frame split INPUT | 0.001 | 0.001–0.001 | n/a |
| frame split UPDATE | 0.012 | 0.012–0.012 | n/a |
| frame split RENDER | 0.768 | 0.768–0.768 | n/a |
| frame split GPU | 10.278 | 10.278–10.278 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder | 0.232 | 0.232–0.232 | n/a |
| GPU frame commandBufferSpans | 10.278 | 10.278–10.278 | n/a |
| GPU frame envelope | 10.278 | 10.278–10.278 | n/a |
| GPU exclusive stage sum | 10.334 | 10.334–10.334 | n/a |
| GPU bakeSunShadowMap | 0.108 | 0.108–0.108 | n/a |
| GPU canvasClear | 0.048 | 0.048–0.048 | n/a |
| GPU computeLightVolume | 0.251 | 0.251–0.251 | n/a |
| GPU computeSunShadow | 0.558 | 0.558–0.558 | n/a |
| GPU computeVoxelAO | 0.015 | 0.015–0.015 | n/a |
| GPU computeVoxelAoPerAxis | 0.032 | 0.032–0.032 | n/a |
| GPU fbToScreen | 0.031 | 0.031–0.031 | n/a |
| GPU fogOverflow | 0.009 | 0.009–0.009 | n/a |
| GPU fogPerAxis | 0.024 | 0.024–0.024 | n/a |
| GPU fogToTrixel | 0.011 | 0.011–0.011 | n/a |
| GPU lightingOverflow | 0.115 | 0.115–0.115 | n/a |
| GPU lightingPerAxis | 0.036 | 0.036–0.036 | n/a |
| GPU lightingToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU perAxisCellCompact | 0.080 | 0.080–0.080 | n/a |
| GPU perAxisScatter | 0.650 | 0.650–0.650 | n/a |
| GPU trixelToFb | 0.892 | 0.892–0.892 | n/a |
| GPU voxelCompact | 0.100 | 0.100–0.100 | n/a |
| GPU voxelPerAxisFinalize | 1.372 | 1.372–1.372 | n/a |
| GPU voxelPerAxisOverflow | 2.438 | 2.438–2.438 | n/a |
| GPU voxelPerAxisStore | 0.745 | 0.745–0.745 | n/a |
| GPU voxelSunFaces | 2.804 | 2.804–2.804 | n/a |
| input_to_present mean | 14.761 | 14.761–14.761 | n/a |
| input_to_present p99 | 22.830 | 22.830–22.830 | n/a |
| cursor_to_present mean | 11.106 | 11.106–11.106 | n/a |
| cursor_to_present p99 | 11.520 | 11.520–11.520 | n/a |
| frame_start_to_submit mean | 0.797 | 0.797–0.797 | n/a |
| frame_start_to_submit p99 | 0.913 | 0.913–0.913 | n/a |
| submit_to_present mean | 10.332 | 10.332–10.332 | n/a |
| submit_to_present p99 | 10.755 | 10.755–10.755 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 226 | 11.278 | 11.533 | 11.683 | 11.732 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.70 | 0.70–0.70 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 301 / 301 | 0 | 301 | 45.000 (0.000) | 51719 / 0 (301) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 226 / 226 | 0 | 0.0003 |

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

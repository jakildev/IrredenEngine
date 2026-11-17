| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 8.400 | 8.400–8.400 | n/a |
| frame p95 | 8.730 | 8.730–8.730 | n/a |
| frame p99 | 9.830 | 9.830–9.830 | n/a |
| steady frame avg | 8.340 | 8.340–8.340 | n/a |
| steady frame p95 | 8.670 | 8.670–8.670 | n/a |
| steady frame p99 | 8.980 | 8.980–8.980 | n/a |
| frame split INPUT | 0.020 | 0.020–0.020 | n/a |
| frame split UPDATE | 0.336 | 0.336–0.336 | n/a |
| frame split RENDER | 1.573 | 1.573–1.573 | n/a |
| frame split GPU | 3.445 | 3.445–3.445 | n/a |
| frame split Pacing | 2.458 | 2.458–2.458 | n/a |
| frame split Remainder | 0.572 | 0.572–0.572 | n/a |
| GPU frame commandBufferSpans | 3.445 | 3.445–3.445 | n/a |
| GPU frame envelope | 3.445 | 3.445–3.445 | n/a |
| input_to_present mean | 9.884 | 9.884–9.884 | n/a |
| input_to_present p99 | 15.701 | 15.701–15.701 | n/a |
| cursor_to_present mean | 5.329 | 5.329–5.329 | n/a |
| cursor_to_present p99 | 7.613 | 7.613–7.613 | n/a |
| frame_start_to_submit mean | 2.180 | 2.180–2.180 | n/a |
| frame_start_to_submit p99 | 3.792 | 3.792–3.792 | n/a |
| submit_to_present mean | 3.611 | 3.611–3.611 | n/a |
| submit_to_present p99 | 5.927 | 5.927–5.927 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 8.342 | 8.674 | 8.983 | 11.483 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.50 | 0.50–0.50 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0008 |

| Render device counter | Steady per frame, mean over runs | Run min–max | Max in a frame | Run total, mean |
|---|---:|---:|---:|---:|
| bufferPartialWriteAllocations | 0.000 | 0.000–0.000 | 1 | 1.0 |
| bufferPartialWriteAllocatedBytes | 0.000 | 0.000–0.000 | 4194304 | 4194304.0 |
| bufferStagedPatches | 10.996 | 10.996–10.996 | 14 | 3304.0 |
| bufferStagedBytes | 109297.422 | 109297.422–109297.422 | 282804 | 33217700.0 |
| bufferFullReplacements | 15.000 | 15.000–15.000 | 15 | 4500.0 |
| bufferMidFrameWaits | 0.000 | 0.000–0.000 | 0 | 0.0 |

| GPU process allocation peak | Mean MiB | Run min–max MiB |
|---|---:|---:|
| sampled at each frame end | 182.9 | 182.9–182.9 |

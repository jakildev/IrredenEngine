| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 29.660 | 29.660–29.660 | n/a |
| frame p95 | 32.730 | 32.730–32.730 | n/a |
| frame p99 | 33.540 | 33.540–33.540 | n/a |
| steady frame avg | 29.720 | 29.720–29.720 | n/a |
| steady frame p95 | 32.830 | 32.830–32.830 | n/a |
| steady frame p99 | 33.420 | 33.420–33.420 | n/a |
| frame split INPUT | 0.088 | 0.088–0.088 | n/a |
| frame split UPDATE | 1.132 | 1.132–1.132 | n/a |
| frame split RENDER | 2.197 | 2.197–2.197 | n/a |
| frame split GPU | 4.356 | 4.356–4.356 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder | 21.885 | 21.885–21.885 | n/a |
| GPU frame commandBufferSpans | 4.356 | 4.356–4.356 | n/a |
| GPU frame envelope | 4.356 | 4.356–4.356 | n/a |
| input_to_present mean | 28.722 | 28.722–28.722 | n/a |
| input_to_present p99 | 31.769 | 31.769–31.769 | n/a |
| cursor_to_present mean | 7.113 | 7.113–7.113 | n/a |
| cursor_to_present p99 | 9.618 | 9.618–9.618 | n/a |
| submit_to_present mean | 4.817 | 4.817–4.817 | n/a |
| submit_to_present p99 | 6.350 | 6.350–6.350 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 29.718 | 32.825 | 33.420 | 34.534 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.80 | 1.80–1.80 | 2 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0014 |

| Render device counter | Steady per frame, mean over runs | Run min–max | Max in a frame | Run total, mean |
|---|---:|---:|---:|---:|
| bufferPartialWriteAllocations | 0.000 | 0.000–0.000 | 1 | 1.0 |
| bufferPartialWriteAllocatedBytes | 0.000 | 0.000–0.000 | 4194304 | 4194304.0 |
| bufferStagedPatches | 12.000 | 12.000–12.000 | 14 | 3602.0 |
| bufferStagedBytes | 219264.000 | 219264.000–219264.000 | 282804 | 65842740.0 |
| bufferFullReplacements | 15.000 | 15.000–15.000 | 15 | 4500.0 |
| bufferMidFrameWaits | 0.000 | 0.000–0.000 | 0 | 0.0 |

| GPU process allocation peak | Mean MiB | Run min–max MiB |
|---|---:|---:|
| sampled at each frame end | 182.9 | 182.9–182.9 |

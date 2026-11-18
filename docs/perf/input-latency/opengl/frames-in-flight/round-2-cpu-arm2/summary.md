| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 289.690 | 289.690–289.690 | n/a |
| frame p95 | 317.350 | 317.350–317.350 | n/a |
| frame p99 | 327.800 | 327.800–327.800 | n/a |
| steady frame avg | 289.290 | 289.290–289.290 | n/a |
| steady frame p95 | 315.840 | 315.840–315.840 | n/a |
| steady frame p99 | 323.770 | 323.770–323.770 | n/a |
| frame split INPUT | 0.046 | 0.046–0.046 | n/a |
| frame split UPDATE | 280.239 | 280.239–280.239 | n/a |
| frame split RENDER | 8.271 | 8.271–8.271 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.131 | 1.131–1.131 | n/a |
| input_to_present mean | 99.110 | 99.110–99.110 | n/a |
| input_to_present p99 | 113.686 | 113.686–113.686 | n/a |
| cursor_to_present mean | 63.944 | 63.944–63.944 | n/a |
| cursor_to_present p99 | 70.466 | 70.466–70.466 | n/a |
| frame_start_to_submit mean | 289.300 | 289.300–289.300 | n/a |
| frame_start_to_submit p99 | 323.742 | 323.742–323.742 | n/a |
| submit_to_present mean | 55.853 | 55.853–55.853 | n/a |
| submit_to_present p99 | 61.441 | 61.441–61.441 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 289.292 | 315.839 | 323.771 | 333.601 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 8.00 | 8.00–8.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.0824 |

Render device counters: unsupported by this backend.

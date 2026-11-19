| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 289.800 | 289.800–289.800 | n/a |
| frame p95 | 319.370 | 319.370–319.370 | n/a |
| frame p99 | 337.160 | 337.160–337.160 | n/a |
| steady frame avg | 289.970 | 289.970–289.970 | n/a |
| steady frame p95 | 319.370 | 319.370–319.370 | n/a |
| steady frame p99 | 332.500 | 332.500–332.500 | n/a |
| frame split INPUT | 0.051 | 0.051–0.051 | n/a |
| frame split UPDATE | 280.204 | 280.204–280.204 | n/a |
| frame split RENDER | 8.270 | 8.270–8.270 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.271 | 1.271–1.271 | n/a |
| input_to_present mean | 98.893 | 98.893–98.893 | n/a |
| input_to_present p99 | 112.174 | 112.174–112.174 | n/a |
| cursor_to_present mean | 63.635 | 63.635–63.635 | n/a |
| cursor_to_present p99 | 70.828 | 70.828–70.828 | n/a |
| frame_start_to_submit mean | 289.935 | 289.935–289.935 | n/a |
| frame_start_to_submit p99 | 332.470 | 332.470–332.470 | n/a |
| submit_to_present mean | 55.572 | 55.572–55.572 | n/a |
| submit_to_present p99 | 62.071 | 62.071–62.071 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 289.972 | 319.368 | 332.503 | 348.522 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 8.00 | 8.00–8.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.0811 |

Render device counters: unsupported by this backend.

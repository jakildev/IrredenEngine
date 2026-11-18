# opengl Debug — interleaved binary / variant A/B

Source `45a220089c6b843cab5fb09007ae34356b2df275`; post binary `8c3e90d1541a`.

| Case | Measurement | Contrast | Pairs | After mean (min–max) | Before mean (min–max) | Paired delta | One-sided 95% lower / upper | Delta / before | Upper / before |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| far-128 | GPU entityCanvasToFb ms | query term, post | 5 | 47.952 (47.790–48.396) | 0.640 (0.536–0.731) | +47.311 | +47.001 / +47.622 | 7387.8% | 7436.2% |
| far-128 | GPU entityCanvasToFb ms | early tests vs production, post | 5 | 2.796 (2.763–2.855) | 47.952 (47.790–48.396) | -45.156 | -45.369 / -44.944 | -94.2% | -93.7% |
| far-128 | GPU entityCanvasToFb ms | early tests vs ablation, post | 5 | 2.796 (2.763–2.855) | 0.640 (0.536–0.731) | +2.155 | +2.046 / +2.264 | 336.5% | 353.5% |
| far-128 | steady frame avg ms | query term, post | 5 | 78.118 (77.910–78.680) | 35.168 (32.680–38.390) | +42.950 | +40.643 / +45.257 | 122.1% | 128.7% |
| far-128 | steady frame avg ms | early tests vs production, post | 5 | 38.446 (33.150–57.810) | 78.118 (77.910–78.680) | -39.672 | -49.698 / -29.646 | -50.8% | -38.0% |
| far-128 | steady frame avg ms | early tests vs ablation, post | 5 | 38.446 (33.150–57.810) | 35.168 (32.680–38.390) | +3.278 | -8.155 / +14.711 | 9.3% | 41.8% |

Columns read after = the first arm named in the contrast, before = the second. Rows are sampled invocation spans or frame averages; they are never summed.

# opengl Release — interleaved binary / variant A/B

Source `45a220089c6b843cab5fb09007ae34356b2df275`; post binary `71387cb90d78`.

| Case | Measurement | Contrast | Pairs | After mean (min–max) | Before mean (min–max) | Paired delta | One-sided 95% lower / upper | Delta / before | Upper / before |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| far-128 | GPU entityCanvasToFb ms | query term, post | 5 | 48.777 (47.941–49.543) | 0.290 (0.259–0.325) | +48.486 | +47.783 / +49.190 | 16696.4% | 16938.7% |
| far-128 | GPU entityCanvasToFb ms | early tests vs production, post | 5 | 2.753 (2.735–2.769) | 48.777 (47.941–49.543) | -46.024 | -46.727 / -45.322 | -94.4% | -92.9% |
| far-128 | GPU entityCanvasToFb ms | early tests vs ablation, post | 5 | 2.753 (2.735–2.769) | 0.290 (0.259–0.325) | +2.462 | +2.448 / +2.477 | 847.9% | 852.8% |
| far-128 | steady frame avg ms | query term, post | 5 | 79.094 (78.200–80.600) | 30.936 (30.800–31.080) | +48.158 | +47.282 / +49.034 | 155.7% | 158.5% |
| far-128 | steady frame avg ms | early tests vs production, post | 5 | 33.698 (33.390–34.200) | 79.094 (78.200–80.600) | -45.396 | -46.147 / -44.645 | -57.4% | -56.4% |
| far-128 | steady frame avg ms | early tests vs ablation, post | 5 | 33.698 (33.390–34.200) | 30.936 (30.800–31.080) | +2.762 | +2.560 / +2.964 | 8.9% | 9.6% |

Columns read after = the first arm named in the contrast, before = the second. Rows are sampled invocation spans or frame averages; they are never summed.

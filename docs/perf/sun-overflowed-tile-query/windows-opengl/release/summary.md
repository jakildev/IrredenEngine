# opengl Release — production vs queryHierarchy=false

Source `5d93af97cd1aa625995883d8fe641f8330f2e711`, binary `ec5a847db78b`, pristine sampler `4bb016193aeb`.

| Mode | Case | Measurement | Pairs | Production mean (min–max) | Ablation mean (min–max) | Paired delta | Delta sd | One-sided 95% lower / upper | Share | Samples / run |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| counters | far-128 | GPU entityCanvasToFb ms | 5 | 67.728 (61.383–80.680) | 0.234 (0.212–0.248) | +67.494 | 7.687 | +60.164 / +74.823 | 99.7% | 357 |
| counters | far-128 | steady frame avg ms | 5 | 93.764 (87.130–108.890) | 27.718 (26.180–29.820) | +66.046 | 8.005 | +58.414 / +73.678 | 70.4% |  |
| counters | far-128 | frame avg ms | 5 | 95.038 (87.700–109.460) | 28.626 (27.150–30.600) | +66.412 | 7.789 | +58.985 / +73.839 | 69.9% |  |
| counters | far-128-filler-first | GPU entityCanvasToFb ms | 5 | 50.239 (43.542–59.314) | 0.718 (0.691–0.751) | +49.521 | 6.134 | +43.673 / +55.369 | 98.6% | 357/359 |
| counters | far-128-filler-first | steady frame avg ms | 5 | 81.474 (73.060–91.650) | 39.632 (37.920–42.280) | +41.842 | 6.962 | +35.204 / +48.480 | 51.4% |  |
| counters | far-128-filler-first | frame avg ms | 5 | 82.352 (74.350–92.130) | 44.412 (42.310–47.440) | +37.940 | 6.942 | +31.321 / +44.559 | 46.1% |  |
| counters | far-128-filler-first-frozen | GPU entityCanvasToFb ms | 5 | 0.732 (0.697–0.789) | 0.669 (0.632–0.720) | +0.063 | 0.010 | +0.053 / +0.072 | 8.6% | 359 |
| counters | far-128-filler-first-frozen | steady frame avg ms | 5 | 38.638 (37.550–40.220) | 38.786 (35.490–41.040) | -0.148 | 1.475 | -1.554 / +1.258 | -0.4% |  |
| counters | far-128-filler-first-frozen | frame avg ms | 5 | 43.664 (42.130–45.970) | 43.716 (40.690–46.150) | -0.052 | 1.194 | -1.190 / +1.086 | -0.1% |  |
| counters | far-128-filler-last | GPU entityCanvasToFb ms | 5 | 53.098 (46.868–61.817) | 0.730 (0.684–0.815) | +52.368 | 7.602 | +45.120 / +59.617 | 98.6% | 357/358/359 |
| counters | far-128-filler-last | steady frame avg ms | 5 | 83.312 (75.720–94.130) | 39.568 (36.770–43.620) | +43.744 | 8.362 | +35.772 / +51.716 | 52.5% |  |
| counters | far-128-filler-last | frame avg ms | 5 | 84.512 (76.830–95.360) | 44.634 (41.090–48.300) | +39.878 | 8.388 | +31.881 / +47.875 | 47.2% |  |
| counters | far-128-filler-last-frozen | GPU entityCanvasToFb ms | 5 | 54.158 (47.007–61.376) | 0.667 (0.617–0.731) | +53.491 | 6.821 | +46.987 / +59.994 | 98.8% | 357/359 |
| counters | far-128-filler-last-frozen | steady frame avg ms | 5 | 85.144 (75.830–92.960) | 39.042 (37.240–43.690) | +46.102 | 7.542 | +38.911 / +53.293 | 54.1% |  |
| counters | far-128-filler-last-frozen | frame avg ms | 5 | 85.878 (76.970–93.760) | 44.074 (42.040–49.720) | +41.804 | 7.077 | +35.057 / +48.551 | 48.7% |  |
| synchronized | far-128 | GPU entityCanvasToFb ms | 5 | 69.415 (61.393–82.205) | 1.341 (1.277–1.430) | +68.075 | 9.343 | +59.166 / +76.983 | 98.1% | 360 |
| synchronized | far-128 | steady frame avg ms | 5 | 107.152 (97.820–123.070) | 36.156 (34.160–39.110) | +70.996 | 10.318 | +61.159 / +80.833 | 66.3% |  |
| synchronized | far-128 | frame avg ms | 5 | 108.808 (98.960–124.040) | 36.710 (34.710–39.710) | +72.098 | 10.886 | +61.718 / +82.478 | 66.3% |  |

Rows are sampled invocation spans or frame averages; they overlap and are never summed. Synchronized rows are finish-bracketed wall intervals.

# Composite draw witness

Medians over frames 60–64. Owned samples are the framebuffer samples a draw holds when the composite ends; invocations count every fragment-shader run, including those later hidden.

| Tree | Case | Canvas group | Canvases | Source invocations | Owned | Invocations / owned | Cross-canvas share of hidden | Source GPU ms, production | Source GPU ms, ablation | Query term ms | Early-tests invocations / owned | Source GPU ms, early tests |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| post | far-128-filler-first-frozen | `-158.0,385.0 2048.0x1024.0 d-96` | 1 | 98566 | 98566 | 1.00 | n/a | 0.629 | 0.556 | +0.073 | n/a | n/a |
| post | far-128-filler-first-frozen | `642.0,529.0 128.0x64.0 d-672` | 128 | 106 | 106 | 1.00 | n/a | 1.020 | 1.048 | -0.028 | n/a | n/a |
| post | far-128-filler-first-frozen | `642.0,385.0 256.0x128.0 d-96` | 1 | 1200 | 1200 | 1.00 | n/a | 0.008 | 0.008 | +0.000 | n/a | n/a |
| post | far-128-filler-first | `642.0,529.0 128.0x64.0 d-672` | 128 | 106 | 106 | 1.00 | n/a | 3.804 | 1.380 | +2.424 | n/a | n/a |
| post | far-128-filler-first | `-158.0,385.0 2048.0x1024.0 d-96` | 1 | 98566 | 98566 | 1.00 | n/a | 0.653 | 0.577 | +0.077 | n/a | n/a |
| post | far-128-filler-first | `642.0,385.0 256.0x128.0 d-96` | 1 | 1200 | 1200 | 1.00 | n/a | 0.038 | 0.032 | +0.006 | n/a | n/a |
| post | far-128-filler-last-frozen | `642.0,529.0 128.0x64.0 d-672` | 128 | 106 | 106 | 1.00 | n/a | 4.126 | 1.002 | +3.123 | n/a | n/a |
| post | far-128-filler-last-frozen | `-158.0,385.0 2048.0x1024.0 d-96` | 1 | 98566 | 98566 | 1.00 | n/a | 0.526 | 0.470 | +0.056 | n/a | n/a |
| post | far-128-filler-last-frozen | `642.0,385.0 256.0x128.0 d-96` | 1 | 1200 | 1200 | 1.00 | n/a | 0.050 | 0.044 | +0.006 | n/a | n/a |
| post | far-128-filler-last | `642.0,529.0 128.0x64.0 d-672` | 128 | 106 | 106 | 1.00 | n/a | 4.379 | 1.625 | +2.754 | n/a | n/a |
| post | far-128-filler-last | `-158.0,385.0 2048.0x1024.0 d-96` | 1 | 98566 | 98566 | 1.00 | n/a | 0.795 | 0.532 | +0.262 | n/a | n/a |
| post | far-128-filler-last | `642.0,385.0 256.0x128.0 d-96` | 1 | 1200 | 1200 | 1.00 | n/a | 0.065 | 0.044 | +0.020 | n/a | n/a |
| post | far-128 | `642.0,529.0 128.0x64.0 d-672` | 128 | 106 | 106 | 1.00 | n/a | 4.618 | 0.995 | +3.623 | n/a | n/a |
| post | far-128 | `642.0,385.0 256.0x128.0 d-96` | 1 | 1200 | 1200 | 1.00 | n/a | 0.043 | 0.038 | +0.005 | n/a | n/a |
| pre | far-128-filler-first-frozen | `642.0,529.0 128.0x64.0 d-672` | 128 | 13568 | 106 | 128.00 | 100.0% | 1.075 | 1.018 | +0.057 | n/a | n/a |
| pre | far-128-filler-first-frozen | `-158.0,385.0 2048.0x1024.0 d-96` | 1 | 98566 | 98566 | 1.00 | n/a | 0.621 | 0.549 | +0.072 | n/a | n/a |
| pre | far-128-filler-first-frozen | `642.0,385.0 256.0x128.0 d-96` | 1 | 1200 | 1200 | 1.00 | n/a | 0.007 | 0.007 | +0.000 | n/a | n/a |
| pre | far-128-filler-first | `642.0,529.0 128.0x64.0 d-672` | 128 | 13568 | 106 | 128.00 | 100.0% | 49.002 | 1.080 | +47.922 | n/a | n/a |
| pre | far-128-filler-first | `-158.0,385.0 2048.0x1024.0 d-96` | 1 | 98566 | 98566 | 1.00 | n/a | 0.651 | 0.558 | +0.093 | n/a | n/a |
| pre | far-128-filler-first | `642.0,385.0 256.0x128.0 d-96` | 1 | 1200 | 1200 | 1.00 | n/a | 0.037 | 0.032 | +0.005 | n/a | n/a |
| pre | far-128-filler-last-frozen | `642.0,529.0 128.0x64.0 d-672` | 128 | 13568 | 106 | 128.00 | 100.0% | 49.076 | 0.989 | +48.087 | n/a | n/a |
| pre | far-128-filler-last-frozen | `-158.0,385.0 2048.0x1024.0 d-96` | 1 | 98566 | 98566 | 1.00 | n/a | 0.516 | 0.467 | +0.049 | n/a | n/a |
| pre | far-128-filler-last-frozen | `642.0,385.0 256.0x128.0 d-96` | 1 | 1200 | 1200 | 1.00 | n/a | 0.050 | 0.043 | +0.007 | n/a | n/a |
| pre | far-128-filler-last | `642.0,529.0 128.0x64.0 d-672` | 128 | 13568 | 106 | 128.00 | 100.0% | 49.755 | 1.399 | +48.356 | n/a | n/a |
| pre | far-128-filler-last | `-158.0,385.0 2048.0x1024.0 d-96` | 1 | 98566 | 98566 | 1.00 | n/a | 0.597 | 0.512 | +0.085 | n/a | n/a |
| pre | far-128-filler-last | `642.0,385.0 256.0x128.0 d-96` | 1 | 1200 | 1200 | 1.00 | n/a | 0.052 | 0.044 | +0.008 | n/a | n/a |
| pre | far-128 | `642.0,529.0 128.0x64.0 d-672` | 128 | 13568 | 106 | 128.00 | 100.0% | 59.818 | 1.012 | +58.806 | n/a | n/a |
| pre | far-128 | `642.0,385.0 256.0x128.0 d-96` | 1 | 1200 | 1200 | 1.00 | n/a | 0.044 | 0.038 | +0.006 | n/a | n/a |

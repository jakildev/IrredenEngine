| cell | metric | base mean (min..max, sd) | arm | arm mean (min..max) | arm / base | paired delta mean (sd) | lower 95% | limit | verdict |
|---|---|---|---|---|---|---|---|---|---|
| perf-grid-zoom4 | cpu:BakeSunShadowMap | 0.051 (0.043..0.057, 0.006) | per-pixel | 0.050 (0.037..0.058) | 0.977x | -0.001 (0.011) | -0.012 | reported | - |
| perf-grid-zoom4 | cpu:CanvasToFramebuffer | 0.091 (0.082..0.100, 0.007) | per-pixel | 0.089 (0.075..0.098) | 0.987x | -0.001 (0.011) | -0.012 | reported | - |
| perf-grid-zoom4 | cpu:ComputeSunShadow | 0.011 (0.009..0.012, 0.001) | per-pixel | 0.012 (0.009..0.013) | 1.074x | +0.001 (0.002) | -0.001 | reported | - |
| perf-grid-zoom4 | cpu:LightingToTrixel | 0.029 (0.025..0.032, 0.003) | per-pixel | 0.029 (0.022..0.032) | 0.993x | -0.000 (0.006) | -0.006 | reported | - |
| perf-grid-zoom4 | cpu:SingleVoxelToCanvasFirst | 2.133 (1.809..2.389, 0.251) | per-pixel | 2.075 (1.536..2.450) | 0.973x | -0.058 (0.449) | -0.486 | reported | - |
| perf-grid-zoom4 | gpu:bakeSunShadowMap | 0.131 (0.123..0.144, 0.008) | per-pixel | 0.122 (0.115..0.129) | 0.930x | -0.009 (0.012) | -0.020 | reported | - |
| perf-grid-zoom4 | gpu:canvasClear | 0.043 (0.041..0.044, 0.001) | per-pixel | 0.082 (0.081..0.085) | 1.930x | +0.040 (0.002) | +0.038 | reported | - |
| perf-grid-zoom4 | gpu:computeLightVolume | 6.081 (5.808..6.428, 0.258) | per-pixel | 5.719 (5.598..5.956) | 0.941x | -0.362 (0.360) | -0.705 | reported | - |
| perf-grid-zoom4 | gpu:computeSunShadow | 0.065 (0.061..0.075, 0.006) | per-pixel | 0.062 (0.058..0.072) | 0.963x | -0.002 (0.009) | -0.011 | reported | - |
| perf-grid-zoom4 | gpu:computeVoxelAO | 0.051 (0.049..0.055, 0.003) | per-pixel | 0.048 (0.046..0.051) | 0.934x | -0.003 (0.004) | -0.007 | reported | - |
| perf-grid-zoom4 | gpu:fbToScreen | 0.036 (0.034..0.038, 0.002) | per-pixel | 0.034 (0.032..0.036) | 0.950x | -0.002 (0.003) | -0.004 | reported | - |
| perf-grid-zoom4 | gpu:fogToTrixel | 0.015 (0.014..0.016, 0.001) | per-pixel | 0.014 (0.012..0.016) | 0.973x | -0.000 (0.002) | -0.002 | reported | - |
| perf-grid-zoom4 | gpu:lightingToTrixel | 0.040 (0.039..0.043, 0.002) | per-pixel | 0.041 (0.039..0.043) | 1.010x | +0.000 (0.003) | -0.002 | reported | - |
| perf-grid-zoom4 | gpu:textToTrixel | 0.950 (0.823..1.045, 0.081) | per-pixel | 1.114 (1.084..1.137) | 1.172x | +0.164 (0.101) | +0.068 | reported | - |
| perf-grid-zoom4 | gpu:trixelToFb | 0.769 (0.729..0.806, 0.032) | per-pixel | 0.609 (0.581..0.681) | 0.792x | -0.160 (0.066) | -0.223 | reported | - |
| perf-grid-zoom4 | gpu:voxelCardinalElect | 1.813 (1.724..1.909, 0.078) | per-pixel | 1.691 (1.657..1.755) | 0.933x | -0.122 (0.107) | -0.224 | reported | - |
| perf-grid-zoom4 | gpu:voxelCompact | 0.737 (0.701..0.771, 0.027) | per-pixel | 0.521 (0.485..0.569) | 0.707x | -0.216 (0.062) | -0.275 | reported | - |
| perf-grid-zoom4 | gpu:voxelStage1 | 1.929 (1.853..2.026, 0.081) | per-pixel | 1.817 (1.778..1.887) | 0.942x | -0.112 (0.107) | -0.214 | reported | - |
| perf-grid-zoom4 | gpu:voxelStage2 | 1.783 (1.711..1.892, 0.076) | per-pixel | 1.668 (1.627..1.752) | 0.935x | -0.115 (0.109) | -0.219 | reported | - |
| perf-grid-zoom4 | gpu:voxelSunFaces | 1.001 (0.961..1.048, 0.037) | per-pixel | 0.960 (0.950..0.984) | 0.959x | -0.041 (0.048) | -0.086 | reported | - |
| perf-grid-zoom4 | gpu_envelope_ms | 7.510 (7.184..7.904, 0.297) | per-pixel | 7.417 (7.267..7.720) | 0.988x | -0.092 (0.433) | -0.505 | reported | - |
| perf-grid-zoom4 | steady_mean_ms | 26.526 (24.170..28.070, 1.883) | per-pixel | 25.814 (23.530..27.730) | 0.973x | -0.712 (3.155) | -3.720 | ratio <= 1.10x | pass |
| perf-grid-zoom4 | steady_p99_ms | 34.930 (32.240..35.870, 1.526) | per-pixel | 34.384 (33.340..36.900) | 0.984x | -0.546 (2.915) | -3.326 | ratio <= 1.10x | pass |
| perf-grid-zoom4 | cpu:BakeSunShadowMap | 0.051 (0.043..0.057, 0.006) | fills | 0.049 (0.046..0.055) | 0.953x | -0.002 (0.006) | -0.008 | reported | - |
| perf-grid-zoom4 | cpu:CanvasToFramebuffer | 0.091 (0.082..0.100, 0.007) | fills | 0.086 (0.082..0.092) | 0.947x | -0.005 (0.009) | -0.014 | reported | - |
| perf-grid-zoom4 | cpu:ComputeSunShadow | 0.011 (0.009..0.012, 0.001) | fills | 0.011 (0.011..0.012) | 1.037x | +0.000 (0.001) | -0.001 | reported | - |
| perf-grid-zoom4 | cpu:LightingToTrixel | 0.029 (0.025..0.032, 0.003) | fills | 0.028 (0.027..0.031) | 0.986x | -0.000 (0.004) | -0.004 | reported | - |
| perf-grid-zoom4 | cpu:SingleVoxelToCanvasFirst | 2.133 (1.809..2.389, 0.251) | fills | 2.078 (1.945..2.362) | 0.974x | -0.055 (0.251) | -0.294 | reported | - |
| perf-grid-zoom4 | gpu:bakeSunShadowMap | 0.131 (0.123..0.144, 0.008) | fills | 0.122 (0.118..0.126) | 0.930x | -0.009 (0.006) | -0.014 | reported | - |
| perf-grid-zoom4 | gpu:canvasClear | 0.043 (0.041..0.044, 0.001) | fills | 0.045 (0.043..0.046) | 1.056x | +0.002 (0.002) | +0.001 | reported | - |
| perf-grid-zoom4 | gpu:computeLightVolume | 6.081 (5.808..6.428, 0.258) | fills | 5.949 (5.783..6.414) | 0.978x | -0.131 (0.243) | -0.363 | reported | - |
| perf-grid-zoom4 | gpu:computeSunShadow | 0.065 (0.061..0.075, 0.006) | fills | 0.064 (0.059..0.072) | 0.985x | -0.001 (0.009) | -0.009 | reported | - |
| perf-grid-zoom4 | gpu:computeVoxelAO | 0.051 (0.049..0.055, 0.003) | fills | 0.050 (0.048..0.052) | 0.965x | -0.002 (0.002) | -0.004 | reported | - |
| perf-grid-zoom4 | gpu:fbToScreen | 0.036 (0.034..0.038, 0.002) | fills | 0.035 (0.034..0.036) | 0.967x | -0.001 (0.001) | -0.003 | reported | - |
| perf-grid-zoom4 | gpu:fogToTrixel | 0.015 (0.014..0.016, 0.001) | fills | 0.014 (0.014..0.015) | 0.973x | -0.000 (0.001) | -0.001 | reported | - |
| perf-grid-zoom4 | gpu:lightingToTrixel | 0.040 (0.039..0.043, 0.002) | fills | 0.041 (0.040..0.043) | 1.015x | +0.001 (0.002) | -0.001 | reported | - |
| perf-grid-zoom4 | gpu:textToTrixel | 0.950 (0.823..1.045, 0.081) | fills | 1.080 (1.057..1.146) | 1.136x | +0.129 (0.065) | +0.067 | reported | - |
| perf-grid-zoom4 | gpu:trixelToFb | 0.769 (0.729..0.806, 0.032) | fills | 0.424 (0.385..0.455) | 0.551x | -0.345 (0.038) | -0.382 | reported | - |
| perf-grid-zoom4 | gpu:voxelCardinalElect | 1.813 (1.724..1.909, 0.078) | fills | 1.746 (1.690..1.907) | 0.963x | -0.067 (0.076) | -0.140 | reported | - |
| perf-grid-zoom4 | gpu:voxelCompact | 0.737 (0.701..0.771, 0.027) | fills | 0.271 (0.250..0.299) | 0.368x | -0.466 (0.030) | -0.495 | reported | - |
| perf-grid-zoom4 | gpu:voxelStage1 | 1.929 (1.853..2.026, 0.081) | fills | 1.852 (1.798..1.967) | 0.960x | -0.076 (0.060) | -0.133 | reported | - |
| perf-grid-zoom4 | gpu:voxelStage2 | 1.783 (1.711..1.892, 0.076) | fills | 1.712 (1.652..1.846) | 0.960x | -0.071 (0.078) | -0.146 | reported | - |
| perf-grid-zoom4 | gpu:voxelSunFaces | 1.001 (0.961..1.048, 0.037) | fills | 0.963 (0.947..1.018) | 0.961x | -0.039 (0.033) | -0.070 | reported | - |
| perf-grid-zoom4 | gpu_envelope_ms | 7.510 (7.184..7.904, 0.297) | fills | 7.426 (7.228..7.906) | 0.989x | -0.083 (0.267) | -0.338 | reported | - |
| perf-grid-zoom4 | steady_mean_ms | 26.526 (24.170..28.070, 1.883) | fills | 25.724 (24.210..27.650) | 0.970x | -0.802 (1.594) | -2.322 | ratio <= 1.10x | pass |
| perf-grid-zoom4 | steady_p99_ms | 34.930 (32.240..35.870, 1.526) | fills | 34.734 (32.440..36.180) | 0.994x | -0.196 (0.646) | -0.812 | ratio <= 1.10x | pass |
| perf-grid-zoom4 | cpu:BakeSunShadowMap | 0.051 (0.043..0.057, 0.006) | head | 0.045 (0.036..0.056) | 0.879x | -0.006 (0.013) | -0.019 | reported | - |
| perf-grid-zoom4 | cpu:CanvasToFramebuffer | 0.091 (0.082..0.100, 0.007) | head | 0.084 (0.075..0.098) | 0.927x | -0.007 (0.012) | -0.018 | reported | - |
| perf-grid-zoom4 | cpu:ComputeSunShadow | 0.011 (0.009..0.012, 0.001) | head | 0.011 (0.009..0.013) | 1.000x | +0.000 (0.002) | -0.002 | reported | - |
| perf-grid-zoom4 | cpu:LightingToTrixel | 0.029 (0.025..0.032, 0.003) | head | 0.026 (0.021..0.032) | 0.896x | -0.003 (0.007) | -0.009 | reported | - |
| perf-grid-zoom4 | cpu:SingleVoxelToCanvasFirst | 2.133 (1.809..2.389, 0.251) | head | 1.886 (1.516..2.358) | 0.884x | -0.247 (0.531) | -0.754 | reported | - |
| perf-grid-zoom4 | gpu:bakeSunShadowMap | 0.131 (0.123..0.144, 0.008) | head | 0.125 (0.119..0.132) | 0.957x | -0.006 (0.013) | -0.018 | reported | - |
| perf-grid-zoom4 | gpu:canvasClear | 0.043 (0.041..0.044, 0.001) | head | 0.045 (0.041..0.047) | 1.052x | +0.002 (0.004) | -0.001 | reported | - |
| perf-grid-zoom4 | gpu:computeLightVolume | 6.081 (5.808..6.428, 0.258) | head | 5.952 (5.755..6.139) | 0.979x | -0.128 (0.397) | -0.507 | reported | - |
| perf-grid-zoom4 | gpu:computeSunShadow | 0.065 (0.061..0.075, 0.006) | head | 0.060 (0.056..0.067) | 0.935x | -0.004 (0.006) | -0.010 | reported | - |
| perf-grid-zoom4 | gpu:computeVoxelAO | 0.051 (0.049..0.055, 0.003) | head | 0.050 (0.048..0.052) | 0.973x | -0.001 (0.004) | -0.005 | reported | - |
| perf-grid-zoom4 | gpu:fbToScreen | 0.036 (0.034..0.038, 0.002) | head | 0.035 (0.034..0.036) | 0.967x | -0.001 (0.002) | -0.003 | reported | - |
| perf-grid-zoom4 | gpu:fogToTrixel | 0.015 (0.014..0.016, 0.001) | head | 0.014 (0.013..0.015) | 0.986x | -0.000 (0.002) | -0.002 | reported | - |
| perf-grid-zoom4 | gpu:lightingToTrixel | 0.040 (0.039..0.043, 0.002) | head | 0.041 (0.040..0.043) | 1.025x | +0.001 (0.002) | -0.001 | reported | - |
| perf-grid-zoom4 | gpu:textToTrixel | 0.950 (0.823..1.045, 0.081) | head | 1.093 (1.071..1.115) | 1.150x | +0.142 (0.079) | +0.067 | reported | - |
| perf-grid-zoom4 | gpu:trixelToFb | 0.769 (0.729..0.806, 0.032) | head | 0.447 (0.380..0.481) | 0.582x | -0.322 (0.041) | -0.361 | reported | - |
| perf-grid-zoom4 | gpu:voxelCardinalElect | 1.813 (1.724..1.909, 0.078) | head | 1.740 (1.708..1.773) | 0.960x | -0.073 (0.106) | -0.174 | reported | - |
| perf-grid-zoom4 | gpu:voxelCompact | 0.737 (0.701..0.771, 0.027) | head | 0.279 (0.235..0.318) | 0.379x | -0.458 (0.035) | -0.491 | reported | - |
| perf-grid-zoom4 | gpu:voxelStage1 | 1.929 (1.853..2.026, 0.081) | head | 1.848 (1.780..1.886) | 0.958x | -0.081 (0.116) | -0.191 | reported | - |
| perf-grid-zoom4 | gpu:voxelStage2 | 1.783 (1.711..1.892, 0.076) | head | 1.719 (1.684..1.759) | 0.964x | -0.065 (0.103) | -0.163 | reported | - |
| perf-grid-zoom4 | gpu:voxelSunFaces | 1.001 (0.961..1.048, 0.037) | head | 0.969 (0.938..0.991) | 0.967x | -0.033 (0.053) | -0.084 | reported | - |
| perf-grid-zoom4 | gpu_envelope_ms | 7.510 (7.184..7.904, 0.297) | head | 7.446 (7.236..7.582) | 0.992x | -0.063 (0.423) | -0.466 | reported | - |
| perf-grid-zoom4 | steady_mean_ms | 26.526 (24.170..28.070, 1.883) | head | 24.434 (22.340..27.000) | 0.921x | -2.092 (3.588) | -5.513 | ratio <= 1.10x | pass |
| perf-grid-zoom4 | steady_p99_ms | 34.930 (32.240..35.870, 1.526) | head | 32.648 (29.680..34.670) | 0.935x | -2.282 (3.019) | -5.161 | ratio <= 1.10x | pass |

| cell | arm | steady frame-time bins (share of frames) |
|---|---|---|
| perf-grid-zoom4 | baseline | 24..25 ms 12%, 26..27 ms 11%, 27..28 ms 14%, 28..29 ms 9% |
| perf-grid-zoom4 | per-pixel | 23..24 ms 9%, 24..25 ms 10%, 25..26 ms 11%, 27..28 ms 15% |
| perf-grid-zoom4 | fills | 23..24 ms 12%, 24..25 ms 12%, 25..26 ms 11%, 27..28 ms 11% |
| perf-grid-zoom4 | head | 20..21 ms 17%, 21..22 ms 12%, 22..23 ms 10%, 23..24 ms 12% |

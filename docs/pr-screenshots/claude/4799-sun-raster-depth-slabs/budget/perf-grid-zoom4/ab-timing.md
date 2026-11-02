| cell | metric | base mean (min..max, sd) | arm | arm mean (min..max) | arm / base | paired delta mean (sd) | lower 95% | limit | verdict |
|---|---|---|---|---|---|---|---|---|---|
| perf-grid-zoom4 | cpu:BakeSunShadowMap | 0.053 (0.045..0.059, 0.007) | per-pixel | 0.055 (0.041..0.058) | 1.030x | +0.002 (0.006) | -0.004 | reported | - |
| perf-grid-zoom4 | cpu:CanvasToFramebuffer | 0.093 (0.084..0.102, 0.008) | per-pixel | 0.096 (0.081..0.102) | 1.032x | +0.003 (0.007) | -0.003 | reported | - |
| perf-grid-zoom4 | cpu:ComputeSunShadow | 0.012 (0.010..0.013, 0.002) | per-pixel | 0.012 (0.010..0.013) | 1.069x | +0.001 (0.001) | -0.000 | reported | - |
| perf-grid-zoom4 | cpu:LightingToTrixel | 0.031 (0.027..0.034, 0.004) | per-pixel | 0.032 (0.024..0.034) | 1.026x | +0.001 (0.003) | -0.002 | reported | - |
| perf-grid-zoom4 | cpu:SingleVoxelToCanvasFirst | 2.185 (1.824..2.431, 0.282) | per-pixel | 2.264 (1.714..2.439) | 1.036x | +0.079 (0.221) | -0.131 | reported | - |
| perf-grid-zoom4 | gpu:bakeSunShadowMap | 0.137 (0.125..0.150, 0.010) | per-pixel | 0.133 (0.123..0.142) | 0.971x | -0.004 (0.013) | -0.016 | reported | - |
| perf-grid-zoom4 | gpu:canvasClear | 0.055 (0.046..0.072, 0.011) | per-pixel | 0.086 (0.072..0.095) | 1.560x | +0.031 (0.016) | +0.016 | reported | - |
| perf-grid-zoom4 | gpu:computeLightVolume | 6.073 (5.728..6.548, 0.305) | per-pixel | 5.937 (5.628..6.233) | 0.978x | -0.136 (0.314) | -0.436 | reported | - |
| perf-grid-zoom4 | gpu:computeSunShadow | 0.061 (0.055..0.065, 0.004) | per-pixel | 0.062 (0.060..0.069) | 1.020x | +0.001 (0.004) | -0.003 | reported | - |
| perf-grid-zoom4 | gpu:computeVoxelAO | 0.052 (0.048..0.059, 0.004) | per-pixel | 0.050 (0.047..0.053) | 0.965x | -0.002 (0.004) | -0.005 | reported | - |
| perf-grid-zoom4 | gpu:fbToScreen | 0.036 (0.034..0.040, 0.002) | per-pixel | 0.035 (0.034..0.038) | 0.983x | -0.001 (0.004) | -0.004 | reported | - |
| perf-grid-zoom4 | gpu:fogToTrixel | 0.016 (0.015..0.019, 0.002) | per-pixel | 0.014 (0.013..0.015) | 0.887x | -0.002 (0.002) | -0.004 | reported | - |
| perf-grid-zoom4 | gpu:lightingToTrixel | 0.044 (0.043..0.047, 0.002) | per-pixel | 0.043 (0.041..0.044) | 0.968x | -0.001 (0.001) | -0.002 | reported | - |
| perf-grid-zoom4 | gpu:textToTrixel | 0.625 (0.509..0.765, 0.104) | per-pixel | 0.890 (0.785..1.026) | 1.424x | +0.265 (0.046) | +0.221 | reported | - |
| perf-grid-zoom4 | gpu:trixelToFb | 0.847 (0.783..0.915, 0.056) | per-pixel | 0.717 (0.671..0.805) | 0.846x | -0.130 (0.037) | -0.165 | reported | - |
| perf-grid-zoom4 | gpu:voxelCardinalElect | 1.817 (1.719..1.952, 0.087) | per-pixel | 1.768 (1.660..1.885) | 0.973x | -0.049 (0.104) | -0.148 | reported | - |
| perf-grid-zoom4 | gpu:voxelCompact | 0.669 (0.628..0.713, 0.039) | per-pixel | 0.421 (0.397..0.462) | 0.630x | -0.248 (0.053) | -0.299 | reported | - |
| perf-grid-zoom4 | gpu:voxelStage1 | 1.937 (1.806..2.115, 0.114) | per-pixel | 1.886 (1.793..1.980) | 0.974x | -0.051 (0.096) | -0.142 | reported | - |
| perf-grid-zoom4 | gpu:voxelStage2 | 1.779 (1.674..1.919, 0.094) | per-pixel | 1.725 (1.629..1.841) | 0.970x | -0.053 (0.121) | -0.169 | reported | - |
| perf-grid-zoom4 | gpu:voxelSunFaces | 1.018 (0.967..1.084, 0.045) | per-pixel | 0.999 (0.964..1.042) | 0.982x | -0.019 (0.040) | -0.057 | reported | - |
| perf-grid-zoom4 | gpu_envelope_ms | 7.542 (7.133..8.125, 0.370) | per-pixel | 7.666 (7.320..8.048) | 1.016x | +0.123 (0.389) | -0.248 | reported | - |
| perf-grid-zoom4 | steady_mean_ms | 27.536 (25.110..29.570, 2.172) | per-pixel | 28.028 (25.640..28.900) | 1.018x | +0.492 (1.816) | -1.239 | ratio <= 1.10x | pass |
| perf-grid-zoom4 | steady_p99_ms | 38.396 (36.720..39.810, 1.112) | per-pixel | 36.120 (32.390..39.330) | 0.941x | -2.276 (3.224) | -5.350 | ratio <= 1.10x | pass |
| perf-grid-zoom4 | cpu:BakeSunShadowMap | 0.053 (0.045..0.059, 0.007) | head | 0.057 (0.054..0.058) | 1.072x | +0.004 (0.007) | -0.003 | reported | - |
| perf-grid-zoom4 | cpu:CanvasToFramebuffer | 0.093 (0.084..0.102, 0.008) | head | 0.098 (0.094..0.103) | 1.054x | +0.005 (0.008) | -0.003 | reported | - |
| perf-grid-zoom4 | cpu:ComputeSunShadow | 0.012 (0.010..0.013, 0.002) | head | 0.013 (0.012..0.013) | 1.103x | +0.001 (0.002) | -0.000 | reported | - |
| perf-grid-zoom4 | cpu:LightingToTrixel | 0.031 (0.027..0.034, 0.004) | head | 0.034 (0.033..0.035) | 1.097x | +0.003 (0.004) | -0.001 | reported | - |
| perf-grid-zoom4 | cpu:SingleVoxelToCanvasFirst | 2.185 (1.824..2.431, 0.282) | head | 2.379 (2.258..2.440) | 1.089x | +0.194 (0.306) | -0.098 | reported | - |
| perf-grid-zoom4 | gpu:bakeSunShadowMap | 0.137 (0.125..0.150, 0.010) | head | 0.135 (0.128..0.143) | 0.983x | -0.002 (0.011) | -0.013 | reported | - |
| perf-grid-zoom4 | gpu:canvasClear | 0.055 (0.046..0.072, 0.011) | head | 0.040 (0.030..0.045) | 0.729x | -0.015 (0.014) | -0.028 | reported | - |
| perf-grid-zoom4 | gpu:computeLightVolume | 6.073 (5.728..6.548, 0.305) | head | 6.044 (5.826..6.205) | 0.995x | -0.029 (0.256) | -0.273 | reported | - |
| perf-grid-zoom4 | gpu:computeSunShadow | 0.061 (0.055..0.065, 0.004) | head | 0.063 (0.058..0.074) | 1.036x | +0.002 (0.007) | -0.004 | reported | - |
| perf-grid-zoom4 | gpu:computeVoxelAO | 0.052 (0.048..0.059, 0.004) | head | 0.050 (0.049..0.051) | 0.961x | -0.002 (0.004) | -0.006 | reported | - |
| perf-grid-zoom4 | gpu:fbToScreen | 0.036 (0.034..0.040, 0.002) | head | 0.035 (0.033..0.036) | 0.961x | -0.001 (0.002) | -0.003 | reported | - |
| perf-grid-zoom4 | gpu:fogToTrixel | 0.016 (0.015..0.019, 0.002) | head | 0.015 (0.014..0.019) | 0.963x | -0.001 (0.003) | -0.004 | reported | - |
| perf-grid-zoom4 | gpu:lightingToTrixel | 0.044 (0.043..0.047, 0.002) | head | 0.043 (0.040..0.046) | 0.977x | -0.001 (0.003) | -0.004 | reported | - |
| perf-grid-zoom4 | gpu:textToTrixel | 0.625 (0.509..0.765, 0.104) | head | 0.801 (0.715..0.885) | 1.282x | +0.176 (0.079) | +0.101 | reported | - |
| perf-grid-zoom4 | gpu:trixelToFb | 0.847 (0.783..0.915, 0.056) | head | 0.765 (0.719..0.797) | 0.903x | -0.082 (0.081) | -0.159 | reported | - |
| perf-grid-zoom4 | gpu:voxelCardinalElect | 1.817 (1.719..1.952, 0.087) | head | 1.778 (1.703..1.846) | 0.979x | -0.039 (0.089) | -0.124 | reported | - |
| perf-grid-zoom4 | gpu:voxelCompact | 0.669 (0.628..0.713, 0.039) | head | 0.288 (0.254..0.325) | 0.431x | -0.381 (0.048) | -0.426 | reported | - |
| perf-grid-zoom4 | gpu:voxelStage1 | 1.937 (1.806..2.115, 0.114) | head | 1.887 (1.829..1.936) | 0.974x | -0.050 (0.102) | -0.148 | reported | - |
| perf-grid-zoom4 | gpu:voxelStage2 | 1.779 (1.674..1.919, 0.094) | head | 1.733 (1.675..1.786) | 0.974x | -0.045 (0.084) | -0.125 | reported | - |
| perf-grid-zoom4 | gpu:voxelSunFaces | 1.018 (0.967..1.084, 0.045) | head | 0.989 (0.961..1.013) | 0.972x | -0.029 (0.041) | -0.068 | reported | - |
| perf-grid-zoom4 | gpu_envelope_ms | 7.542 (7.133..8.125, 0.370) | head | 7.561 (7.320..7.757) | 1.002x | +0.019 (0.358) | -0.323 | reported | - |
| perf-grid-zoom4 | steady_mean_ms | 27.536 (25.110..29.570, 2.172) | head | 28.844 (27.970..29.770) | 1.048x | +1.308 (2.125) | -0.718 | ratio <= 1.10x | pass |
| perf-grid-zoom4 | steady_p99_ms | 38.396 (36.720..39.810, 1.112) | head | 39.168 (34.850..45.240) | 1.020x | +0.772 (3.583) | -2.644 | ratio <= 1.10x | pass |

| cell | arm | steady frame-time bins (share of frames) |
|---|---|---|
| perf-grid-zoom4 | baseline | 21..22 ms 8%, 27..28 ms 12%, 28..29 ms 9%, 29..30 ms 10% |
| perf-grid-zoom4 | per-pixel | 24..25 ms 12%, 26..27 ms 11%, 27..28 ms 14%, 28..29 ms 16% |
| perf-grid-zoom4 | head | 26..27 ms 10%, 27..28 ms 12%, 28..29 ms 19%, 29..30 ms 10% |

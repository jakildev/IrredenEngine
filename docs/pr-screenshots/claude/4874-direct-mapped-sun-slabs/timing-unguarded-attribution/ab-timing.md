| cell | metric | base mean (min..max, sd) | arm | arm mean (min..max) | arm / base | paired delta mean (sd) | lower 95% | limit | verdict |
|---|---|---|---|---|---|---|---|---|---|
| canvas-lower | cpu:BakeSunShadowMap | 0.017 (0.014..0.020, 0.002) | associative | 0.023 (0.020..0.027) | 1.360x | +0.006 (0.005) | +0.001 | reported | - |
| canvas-lower | cpu:CanvasToFramebuffer | 0.058 (0.048..0.070, 0.009) | associative | 0.058 (0.051..0.065) | 1.000x | -0.000 (0.013) | -0.013 | reported | - |
| canvas-lower | cpu:ComputeSunShadow | 0.007 (0.006..0.008, 0.001) | associative | 0.007 (0.006..0.009) | 1.000x | -0.000 (0.002) | -0.002 | reported | - |
| canvas-lower | cpu:LightingToTrixel | 0.018 (0.015..0.022, 0.003) | associative | 0.018 (0.016..0.021) | 1.000x | +0.000 (0.005) | -0.005 | reported | - |
| canvas-lower | cpu:SingleVoxelToCanvasFirst | 0.126 (0.110..0.151, 0.018) | associative | 0.129 (0.114..0.146) | 1.025x | +0.003 (0.033) | -0.028 | reported | - |
| canvas-lower | gpu:bakeSunShadowMap | 0.695 (0.443..0.895, 0.173) | associative | 1.409 (1.113..1.753) | 2.028x | +0.714 (0.311) | +0.418 | delta <= 0.064 ms | FAIL |
| canvas-lower | gpu:canvasClear | 0.056 (0.008..0.115, 0.039) | associative | 0.033 (0.009..0.060) | 0.586x | -0.023 (0.053) | -0.074 | reported | - |
| canvas-lower | gpu:cardinalSurfaceFb | 1.404 (0.653..1.892, 0.471) | associative | 1.875 (1.144..2.548) | 1.336x | +0.472 (0.845) | -0.334 | reported | - |
| canvas-lower | gpu:computeLightVolume | 0.123 (0.090..0.150, 0.023) | associative | 0.386 (0.180..0.614) | 3.148x | +0.263 (0.223) | +0.051 | reported | - |
| canvas-lower | gpu:computeSunShadow | 0.077 (0.028..0.108, 0.034) | associative | 0.079 (0.019..0.170) | 1.026x | +0.002 (0.075) | -0.070 | reported | - |
| canvas-lower | gpu:computeVoxelAO | 0.069 (0.032..0.093, 0.024) | associative | 0.074 (0.057..0.105) | 1.072x | +0.005 (0.029) | -0.022 | reported | - |
| canvas-lower | gpu:fbToScreen | 0.081 (0.043..0.106, 0.025) | associative | 0.063 (0.031..0.098) | 0.771x | -0.019 (0.042) | -0.058 | reported | - |
| canvas-lower | gpu:lightingToTrixel | 0.076 (0.026..0.123, 0.040) | associative | 0.044 (0.016..0.072) | 0.587x | -0.031 (0.055) | -0.084 | reported | - |
| canvas-lower | gpu:trixelToFb | 1.103 (0.542..1.479, 0.354) | associative | 1.489 (1.001..1.964) | 1.350x | +0.386 (0.614) | -0.200 | reported | - |
| canvas-lower | gpu:voxelCardinalElect | 0.042 (0.035..0.054, 0.008) | associative | 0.046 (0.031..0.059) | 1.075x | +0.003 (0.018) | -0.014 | reported | - |
| canvas-lower | gpu:voxelCompact | 0.089 (0.041..0.166, 0.049) | associative | 0.054 (0.035..0.082) | 0.605x | -0.035 (0.050) | -0.083 | reported | - |
| canvas-lower | gpu:voxelStage1 | 0.201 (0.031..0.278, 0.106) | associative | 0.173 (0.037..0.392) | 0.860x | -0.028 (0.205) | -0.224 | reported | - |
| canvas-lower | gpu:voxelStage2 | 0.047 (0.041..0.058, 0.007) | associative | 0.059 (0.039..0.085) | 1.255x | +0.012 (0.028) | -0.015 | reported | - |
| canvas-lower | gpu_envelope_ms | 1.738 (0.861..2.290, 0.546) | associative | 2.237 (1.433..2.894) | 1.287x | +0.499 (0.877) | -0.337 | reported | - |
| canvas-lower | gpu_frame:commandBufferSpans | 1.738 (0.861..2.290, 0.546) | associative | 2.237 (1.433..2.894) | 1.287x | +0.499 (0.877) | -0.337 | reported | - |
| canvas-lower | gpu_frame:envelope | 1.738 (0.861..2.290, 0.546) | associative | 2.237 (1.433..2.894) | 1.287x | +0.499 (0.877) | -0.337 | reported | - |
| canvas-lower | gpu_frame_attempted | 360.000 (360.000..360.000, 0.000) | associative | 360.000 (360.000..360.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_frame_command_buffers | 360.000 (360.000..360.000, 0.000) | associative | 360.000 (360.000..360.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_frame_valid | 360.000 (360.000..360.000, 0.000) | associative | 360.000 (360.000..360.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:bakeSunShadowMap | 359.000 (359.000..359.000, 0.000) | associative | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:canvasClear | 359.000 (359.000..359.000, 0.000) | associative | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:cardinalSurfaceFb | 359.000 (359.000..359.000, 0.000) | associative | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:computeLightVolume | 359.000 (359.000..359.000, 0.000) | associative | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:computeSunShadow | 359.000 (359.000..359.000, 0.000) | associative | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:computeVoxelAO | 359.000 (359.000..359.000, 0.000) | associative | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:fbToScreen | 359.000 (359.000..359.000, 0.000) | associative | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:lightingToTrixel | 359.000 (359.000..359.000, 0.000) | associative | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:trixelToFb | 359.000 (359.000..359.000, 0.000) | associative | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:voxelCardinalElect | 359.000 (359.000..359.000, 0.000) | associative | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:voxelCompact | 359.000 (359.000..359.000, 0.000) | associative | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:voxelStage1 | 359.000 (359.000..359.000, 0.000) | associative | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:voxelStage2 | 359.000 (359.000..359.000, 0.000) | associative | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | recorded_frames | 360.000 (360.000..360.000, 0.000) | associative | 360.000 (360.000..360.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | steady_mean_ms | 8.330 (8.330..8.330, 0.000) | associative | 8.330 (8.330..8.330) | 1.000x | +0.000 (0.000) | +0.000 | ratio <= 1.10x | pass |
| canvas-lower | steady_p99_ms | 8.988 (8.790..9.170, 0.156) | associative | 8.902 (8.770..9.070) | 0.990x | -0.086 (0.255) | -0.330 | ratio <= 1.10x | pass |
| canvas-lower | warmup_frames | 90.000 (90.000..90.000, 0.000) | associative | 90.000 (90.000..90.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | cpu:BakeSunShadowMap | 0.017 (0.014..0.020, 0.002) | head | 0.040 (0.022..0.105) | 2.314x | +0.023 (0.037) | -0.013 | reported | - |
| canvas-lower | cpu:CanvasToFramebuffer | 0.058 (0.048..0.070, 0.009) | head | 0.214 (0.052..0.822) | 3.719x | +0.157 (0.346) | -0.173 | reported | - |
| canvas-lower | cpu:ComputeSunShadow | 0.007 (0.006..0.008, 0.001) | head | 0.350 (0.007..1.718) | 47.243x | +0.342 (0.765) | -0.387 | reported | - |
| canvas-lower | cpu:LightingToTrixel | 0.018 (0.015..0.022, 0.003) | head | 0.221 (0.016..1.036) | 12.022x | +0.203 (0.456) | -0.232 | reported | - |
| canvas-lower | cpu:SingleVoxelToCanvasFirst | 0.126 (0.110..0.151, 0.018) | head | 0.125 (0.111..0.167) | 0.990x | -0.001 (0.036) | -0.036 | reported | - |
| canvas-lower | gpu:bakeSunShadowMap | 0.695 (0.443..0.895, 0.173) | head | 1.188 (0.932..1.568) | 1.709x | +0.493 (0.325) | +0.183 | delta <= 0.064 ms | FAIL |
| canvas-lower | gpu:canvasClear | 0.056 (0.008..0.115, 0.039) | head | 0.036 (0.008..0.079) | 0.650x | -0.020 (0.058) | -0.074 | reported | - |
| canvas-lower | gpu:cardinalSurfaceFb | 1.404 (0.653..1.892, 0.471) | head | 1.663 (0.913..2.501) | 1.185x | +0.259 (0.964) | -0.660 | reported | - |
| canvas-lower | gpu:computeLightVolume | 0.123 (0.090..0.150, 0.023) | head | 0.193 (0.099..0.330) | 1.577x | +0.071 (0.089) | -0.015 | reported | - |
| canvas-lower | gpu:computeSunShadow | 0.077 (0.028..0.108, 0.034) | head | 0.070 (0.016..0.144) | 0.914x | -0.007 (0.080) | -0.082 | reported | - |
| canvas-lower | gpu:computeVoxelAO | 0.069 (0.032..0.093, 0.024) | head | 0.074 (0.046..0.110) | 1.072x | +0.005 (0.030) | -0.024 | reported | - |
| canvas-lower | gpu:fbToScreen | 0.081 (0.043..0.106, 0.025) | head | 0.060 (0.030..0.098) | 0.737x | -0.021 (0.045) | -0.064 | reported | - |
| canvas-lower | gpu:lightingToTrixel | 0.076 (0.026..0.123, 0.040) | head | 0.047 (0.015..0.091) | 0.616x | -0.029 (0.062) | -0.088 | reported | - |
| canvas-lower | gpu:trixelToFb | 1.103 (0.542..1.479, 0.354) | head | 1.332 (0.769..2.025) | 1.207x | +0.229 (0.733) | -0.471 | reported | - |
| canvas-lower | gpu:voxelCardinalElect | 0.042 (0.035..0.054, 0.008) | head | 0.049 (0.036..0.064) | 1.165x | +0.007 (0.015) | -0.008 | reported | - |
| canvas-lower | gpu:voxelCompact | 0.089 (0.041..0.166, 0.049) | head | 0.075 (0.037..0.140) | 0.847x | -0.014 (0.077) | -0.087 | reported | - |
| canvas-lower | gpu:voxelStage1 | 0.201 (0.031..0.278, 0.106) | head | 0.192 (0.027..0.367) | 0.956x | -0.009 (0.220) | -0.219 | reported | - |
| canvas-lower | gpu:voxelStage2 | 0.047 (0.041..0.058, 0.007) | head | 0.064 (0.040..0.089) | 1.353x | +0.017 (0.025) | -0.007 | reported | - |
| canvas-lower | gpu_envelope_ms | 1.738 (0.861..2.290, 0.546) | head | 2.010 (1.251..2.896) | 1.157x | +0.272 (1.037) | -0.716 | reported | - |
| canvas-lower | gpu_frame:commandBufferSpans | 1.738 (0.861..2.290, 0.546) | head | 2.010 (1.251..2.896) | 1.157x | +0.272 (1.037) | -0.716 | reported | - |
| canvas-lower | gpu_frame:envelope | 1.738 (0.861..2.290, 0.546) | head | 2.010 (1.251..2.896) | 1.157x | +0.272 (1.037) | -0.716 | reported | - |
| canvas-lower | gpu_frame_attempted | 360.000 (360.000..360.000, 0.000) | head | 360.000 (360.000..360.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_frame_command_buffers | 360.000 (360.000..360.000, 0.000) | head | 360.000 (360.000..360.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_frame_valid | 360.000 (360.000..360.000, 0.000) | head | 360.000 (360.000..360.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:bakeSunShadowMap | 359.000 (359.000..359.000, 0.000) | head | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:canvasClear | 359.000 (359.000..359.000, 0.000) | head | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:cardinalSurfaceFb | 359.000 (359.000..359.000, 0.000) | head | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:computeLightVolume | 359.000 (359.000..359.000, 0.000) | head | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:computeSunShadow | 359.000 (359.000..359.000, 0.000) | head | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:computeVoxelAO | 359.000 (359.000..359.000, 0.000) | head | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:fbToScreen | 359.000 (359.000..359.000, 0.000) | head | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:lightingToTrixel | 359.000 (359.000..359.000, 0.000) | head | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:trixelToFb | 359.000 (359.000..359.000, 0.000) | head | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:voxelCardinalElect | 359.000 (359.000..359.000, 0.000) | head | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:voxelCompact | 359.000 (359.000..359.000, 0.000) | head | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:voxelStage1 | 359.000 (359.000..359.000, 0.000) | head | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu_samples:voxelStage2 | 359.000 (359.000..359.000, 0.000) | head | 359.000 (359.000..359.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | recorded_frames | 360.000 (360.000..360.000, 0.000) | head | 360.000 (360.000..360.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | steady_mean_ms | 8.330 (8.330..8.330, 0.000) | head | 8.336 (8.330..8.360) | 1.001x | +0.006 (0.013) | -0.007 | ratio <= 1.10x | pass |
| canvas-lower | steady_p99_ms | 8.988 (8.790..9.170, 0.156) | head | 8.928 (8.850..8.990) | 0.993x | -0.060 (0.133) | -0.187 | ratio <= 1.10x | pass |
| canvas-lower | warmup_frames | 90.000 (90.000..90.000, 0.000) | head | 90.000 (90.000..90.000) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |

| cell | arm | steady frame-time bins (share of frames) |
|---|---|---|
| canvas-lower | baseline | 7..8 ms 7%, 8..9 ms 92%, 9..10 ms 1% |
| canvas-lower | associative | 7..8 ms 5%, 8..9 ms 94%, 9..10 ms 1% |
| canvas-lower | head | 7..8 ms 4%, 8..9 ms 96%, 9..10 ms 0%, 17..18 ms 0% |

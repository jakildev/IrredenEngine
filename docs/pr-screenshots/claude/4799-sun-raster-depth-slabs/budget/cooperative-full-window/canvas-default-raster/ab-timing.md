| cell | metric | base mean (min..max, sd) | arm | arm mean (min..max) | arm / base | paired delta mean (sd) | lower 95% | limit | verdict |
|---|---|---|---|---|---|---|---|---|---|
| canvas-default-raster | cpu:BakeSunShadowMap | 0.117 (0.113..0.120, 0.004) | per-pixel | 0.118 (0.115..0.122) | 1.002x | +0.000 (0.004) | -0.004 | reported | - |
| canvas-default-raster | cpu:CanvasToFramebuffer | 0.049 (0.044..0.058, 0.007) | per-pixel | 0.049 (0.042..0.055) | 0.996x | -0.000 (0.005) | -0.005 | reported | - |
| canvas-default-raster | cpu:ComputeSunShadow | 0.006 (0.006..0.006, 0.000) | per-pixel | 0.006 (0.006..0.006) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-default-raster | cpu:LightingToTrixel | 0.134 (0.129..0.139, 0.005) | per-pixel | 0.133 (0.131..0.139) | 0.996x | -0.001 (0.006) | -0.006 | reported | - |
| canvas-default-raster | cpu:SingleVoxelToCanvasFirst | 1.823 (1.762..1.842, 0.034) | per-pixel | 1.817 (1.770..1.867) | 0.997x | -0.006 (0.058) | -0.061 | reported | - |
| canvas-default-raster | gpu:bakeSunShadowMap | 2.224 (2.057..2.299, 0.096) | per-pixel | 3.046 (3.018..3.126) | 1.370x | +0.822 (0.140) | +0.688 | reported | - |
| canvas-default-raster | gpu:canvasClear | 0.013 (0.012..0.014, 0.001) | per-pixel | 0.008 (0.008..0.009) | 0.631x | -0.005 (0.001) | -0.006 | reported | - |
| canvas-default-raster | gpu:cardinalSurfaceFb | 2.503 (2.313..2.597, 0.110) | per-pixel | 3.230 (3.197..3.314) | 1.290x | +0.727 (0.157) | +0.577 | reported | - |
| canvas-default-raster | gpu:computeLightVolume | 0.062 (0.051..0.069, 0.009) | per-pixel | 0.195 (0.149..0.248) | 3.166x | +0.133 (0.043) | +0.093 | reported | - |
| canvas-default-raster | gpu:computeSunShadow | 0.034 (0.028..0.039, 0.005) | per-pixel | 0.029 (0.027..0.032) | 0.865x | -0.005 (0.004) | -0.008 | reported | - |
| canvas-default-raster | gpu:computeVoxelAO | 0.164 (0.146..0.179, 0.013) | per-pixel | 0.292 (0.287..0.297) | 1.780x | +0.128 (0.011) | +0.117 | reported | - |
| canvas-default-raster | gpu:entityCanvasToFb | 3.194 (2.943..3.304, 0.144) | per-pixel | 3.768 (3.723..3.854) | 1.180x | +0.574 (0.193) | +0.390 | reported | - |
| canvas-default-raster | gpu:fbToScreen | 0.054 (0.053..0.058, 0.002) | per-pixel | 0.039 (0.037..0.040) | 0.710x | -0.016 (0.002) | -0.018 | reported | - |
| canvas-default-raster | gpu:lightingToTrixel | 0.027 (0.022..0.030, 0.003) | per-pixel | 0.020 (0.018..0.023) | 0.767x | -0.006 (0.003) | -0.009 | reported | - |
| canvas-default-raster | gpu:shapeDepth | 0.145 (0.136..0.158, 0.008) | per-pixel | 0.104 (0.099..0.109) | 0.718x | -0.041 (0.009) | -0.050 | reported | - |
| canvas-default-raster | gpu:shapeOwnerClear | 0.021 (0.017..0.031, 0.006) | per-pixel | 0.005 (0.004..0.005) | 0.221x | -0.016 (0.006) | -0.022 | reported | - |
| canvas-default-raster | gpu:shapeOwnerElect | 0.144 (0.137..0.155, 0.007) | per-pixel | 0.104 (0.099..0.109) | 0.719x | -0.040 (0.008) | -0.048 | reported | - |
| canvas-default-raster | gpu:shapePublish | 0.148 (0.140..0.161, 0.008) | per-pixel | 0.105 (0.100..0.110) | 0.705x | -0.044 (0.009) | -0.052 | reported | - |
| canvas-default-raster | gpu:trixelToFb | 1.752 (1.643..1.867, 0.080) | per-pixel | 2.672 (2.636..2.731) | 1.525x | +0.920 (0.114) | +0.812 | reported | - |
| canvas-default-raster | gpu:voxelCardinalElect | 0.019 (0.018..0.020, 0.001) | per-pixel | 0.014 (0.014..0.015) | 0.740x | -0.005 (0.001) | -0.006 | reported | - |
| canvas-default-raster | gpu:voxelCompact | 0.043 (0.040..0.044, 0.002) | per-pixel | 0.039 (0.039..0.040) | 0.920x | -0.003 (0.002) | -0.005 | reported | - |
| canvas-default-raster | gpu:voxelStage1 | 0.043 (0.041..0.046, 0.002) | per-pixel | 0.032 (0.031..0.033) | 0.737x | -0.011 (0.002) | -0.014 | reported | - |
| canvas-default-raster | gpu:voxelStage2 | 0.021 (0.019..0.021, 0.001) | per-pixel | 0.016 (0.016..0.016) | 0.777x | -0.005 (0.001) | -0.005 | reported | - |
| canvas-default-raster | gpu_envelope_ms | 7.277 (7.112..7.414, 0.113) | per-pixel | 7.456 (7.346..7.584) | 1.025x | +0.178 (0.165) | +0.021 | reported | - |
| canvas-default-raster | steady_mean_ms | 21.872 (21.370..22.090, 0.291) | per-pixel | 22.084 (21.930..22.380) | 1.010x | +0.212 (0.349) | -0.121 | ratio <= 1.10x | pass |
| canvas-default-raster | steady_p99_ms | 27.928 (26.720..29.500, 1.313) | per-pixel | 27.214 (25.570..29.910) | 0.974x | -0.714 (2.466) | -3.066 | ratio <= 1.10x | pass |
| canvas-default-raster | cpu:BakeSunShadowMap | 0.117 (0.113..0.120, 0.004) | fills | 0.124 (0.117..0.129) | 1.058x | +0.007 (0.006) | +0.001 | reported | - |
| canvas-default-raster | cpu:CanvasToFramebuffer | 0.049 (0.044..0.058, 0.007) | fills | 0.047 (0.041..0.056) | 0.943x | -0.003 (0.007) | -0.010 | reported | - |
| canvas-default-raster | cpu:ComputeSunShadow | 0.006 (0.006..0.006, 0.000) | fills | 0.006 (0.006..0.006) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-default-raster | cpu:LightingToTrixel | 0.134 (0.129..0.139, 0.005) | fills | 0.134 (0.127..0.139) | 1.004x | +0.001 (0.006) | -0.005 | reported | - |
| canvas-default-raster | cpu:SingleVoxelToCanvasFirst | 1.823 (1.762..1.842, 0.034) | fills | 1.831 (1.769..1.879) | 1.005x | +0.008 (0.073) | -0.061 | reported | - |
| canvas-default-raster | gpu:bakeSunShadowMap | 2.224 (2.057..2.299, 0.096) | fills | 2.988 (2.886..3.043) | 1.343x | +0.764 (0.136) | +0.634 | reported | - |
| canvas-default-raster | gpu:canvasClear | 0.013 (0.012..0.014, 0.001) | fills | 0.008 (0.008..0.009) | 0.646x | -0.005 (0.001) | -0.006 | reported | - |
| canvas-default-raster | gpu:cardinalSurfaceFb | 2.503 (2.313..2.597, 0.110) | fills | 3.168 (3.027..3.233) | 1.266x | +0.665 (0.164) | +0.509 | reported | - |
| canvas-default-raster | gpu:computeLightVolume | 0.062 (0.051..0.069, 0.009) | fills | 0.167 (0.052..0.236) | 2.711x | +0.105 (0.083) | +0.026 | reported | - |
| canvas-default-raster | gpu:computeSunShadow | 0.034 (0.028..0.039, 0.005) | fills | 0.030 (0.026..0.033) | 0.894x | -0.004 (0.005) | -0.008 | reported | - |
| canvas-default-raster | gpu:computeVoxelAO | 0.164 (0.146..0.179, 0.013) | fills | 0.088 (0.086..0.089) | 0.535x | -0.076 (0.014) | -0.089 | reported | - |
| canvas-default-raster | gpu:entityCanvasToFb | 3.194 (2.943..3.304, 0.144) | fills | 3.722 (3.529..3.811) | 1.165x | +0.528 (0.214) | +0.324 | reported | - |
| canvas-default-raster | gpu:fbToScreen | 0.054 (0.053..0.058, 0.002) | fills | 0.039 (0.037..0.040) | 0.713x | -0.016 (0.003) | -0.019 | reported | - |
| canvas-default-raster | gpu:lightingToTrixel | 0.027 (0.022..0.030, 0.003) | fills | 0.022 (0.018..0.024) | 0.812x | -0.005 (0.004) | -0.009 | reported | - |
| canvas-default-raster | gpu:shapeDepth | 0.145 (0.136..0.158, 0.008) | fills | 0.104 (0.100..0.108) | 0.718x | -0.041 (0.010) | -0.051 | reported | - |
| canvas-default-raster | gpu:shapeOwnerClear | 0.021 (0.017..0.031, 0.006) | fills | 0.004 (0.004..0.005) | 0.212x | -0.016 (0.006) | -0.022 | reported | - |
| canvas-default-raster | gpu:shapeOwnerElect | 0.144 (0.137..0.155, 0.007) | fills | 0.103 (0.099..0.107) | 0.718x | -0.041 (0.009) | -0.049 | reported | - |
| canvas-default-raster | gpu:shapePublish | 0.148 (0.140..0.161, 0.008) | fills | 0.106 (0.102..0.110) | 0.716x | -0.042 (0.010) | -0.052 | reported | - |
| canvas-default-raster | gpu:trixelToFb | 1.752 (1.643..1.867, 0.080) | fills | 2.604 (2.433..2.669) | 1.487x | +0.853 (0.170) | +0.690 | reported | - |
| canvas-default-raster | gpu:voxelCardinalElect | 0.019 (0.018..0.020, 0.001) | fills | 0.015 (0.014..0.015) | 0.771x | -0.004 (0.001) | -0.005 | reported | - |
| canvas-default-raster | gpu:voxelCompact | 0.043 (0.040..0.044, 0.002) | fills | 0.040 (0.039..0.040) | 0.930x | -0.003 (0.002) | -0.005 | reported | - |
| canvas-default-raster | gpu:voxelStage1 | 0.043 (0.041..0.046, 0.002) | fills | 0.033 (0.032..0.033) | 0.751x | -0.011 (0.002) | -0.013 | reported | - |
| canvas-default-raster | gpu:voxelStage2 | 0.021 (0.019..0.021, 0.001) | fills | 0.016 (0.015..0.016) | 0.767x | -0.005 (0.001) | -0.006 | reported | - |
| canvas-default-raster | gpu_envelope_ms | 7.277 (7.112..7.414, 0.113) | fills | 7.452 (7.421..7.483) | 1.024x | +0.175 (0.101) | +0.078 | reported | - |
| canvas-default-raster | steady_mean_ms | 21.872 (21.370..22.090, 0.291) | fills | 22.120 (21.720..22.370) | 1.011x | +0.248 (0.512) | -0.241 | ratio <= 1.10x | pass |
| canvas-default-raster | steady_p99_ms | 27.928 (26.720..29.500, 1.313) | fills | 25.268 (23.850..27.170) | 0.905x | -2.660 (1.900) | -4.472 | ratio <= 1.10x | pass |
| canvas-default-raster | cpu:BakeSunShadowMap | 0.117 (0.113..0.120, 0.004) | head | 0.126 (0.121..0.131) | 1.077x | +0.009 (0.007) | +0.002 | reported | - |
| canvas-default-raster | cpu:CanvasToFramebuffer | 0.049 (0.044..0.058, 0.007) | head | 0.045 (0.043..0.046) | 0.907x | -0.005 (0.006) | -0.011 | reported | - |
| canvas-default-raster | cpu:ComputeSunShadow | 0.006 (0.006..0.006, 0.000) | head | 0.006 (0.006..0.006) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-default-raster | cpu:LightingToTrixel | 0.134 (0.129..0.139, 0.005) | head | 0.137 (0.133..0.141) | 1.027x | +0.004 (0.007) | -0.003 | reported | - |
| canvas-default-raster | cpu:SingleVoxelToCanvasFirst | 1.823 (1.762..1.842, 0.034) | head | 1.847 (1.757..1.901) | 1.013x | +0.024 (0.084) | -0.056 | reported | - |
| canvas-default-raster | gpu:bakeSunShadowMap | 2.224 (2.057..2.299, 0.096) | head | 3.837 (3.795..3.910) | 1.725x | +1.612 (0.093) | +1.523 | reported | - |
| canvas-default-raster | gpu:canvasClear | 0.013 (0.012..0.014, 0.001) | head | 0.007 (0.007..0.007) | 0.538x | -0.006 (0.001) | -0.007 | reported | - |
| canvas-default-raster | gpu:cardinalSurfaceFb | 2.503 (2.313..2.597, 0.110) | head | 3.884 (3.838..3.958) | 1.552x | +1.381 (0.101) | +1.284 | reported | - |
| canvas-default-raster | gpu:computeLightVolume | 0.062 (0.051..0.069, 0.009) | head | 0.134 (0.101..0.151) | 2.172x | +0.072 (0.027) | +0.046 | reported | - |
| canvas-default-raster | gpu:computeSunShadow | 0.034 (0.028..0.039, 0.005) | head | 0.024 (0.024..0.024) | 0.706x | -0.010 (0.005) | -0.015 | reported | - |
| canvas-default-raster | gpu:computeVoxelAO | 0.164 (0.146..0.179, 0.013) | head | 0.084 (0.083..0.085) | 0.514x | -0.080 (0.013) | -0.092 | reported | - |
| canvas-default-raster | gpu:entityCanvasToFb | 3.194 (2.943..3.304, 0.144) | head | 4.316 (4.269..4.396) | 1.351x | +1.122 (0.133) | +0.995 | reported | - |
| canvas-default-raster | gpu:fbToScreen | 0.054 (0.053..0.058, 0.002) | head | 0.029 (0.029..0.029) | 0.533x | -0.025 (0.002) | -0.027 | reported | - |
| canvas-default-raster | gpu:lightingToTrixel | 0.027 (0.022..0.030, 0.003) | head | 0.017 (0.016..0.017) | 0.632x | -0.010 (0.003) | -0.013 | reported | - |
| canvas-default-raster | gpu:shapeDepth | 0.145 (0.136..0.158, 0.008) | head | 0.075 (0.073..0.076) | 0.514x | -0.071 (0.008) | -0.078 | reported | - |
| canvas-default-raster | gpu:shapeOwnerClear | 0.021 (0.017..0.031, 0.006) | head | 0.004 (0.004..0.004) | 0.192x | -0.017 (0.006) | -0.022 | reported | - |
| canvas-default-raster | gpu:shapeOwnerElect | 0.144 (0.137..0.155, 0.007) | head | 0.075 (0.074..0.076) | 0.518x | -0.069 (0.006) | -0.075 | reported | - |
| canvas-default-raster | gpu:shapePublish | 0.148 (0.140..0.161, 0.008) | head | 0.077 (0.076..0.078) | 0.516x | -0.072 (0.007) | -0.078 | reported | - |
| canvas-default-raster | gpu:trixelToFb | 1.752 (1.643..1.867, 0.080) | head | 2.637 (2.593..2.717) | 1.506x | +0.886 (0.055) | +0.834 | reported | - |
| canvas-default-raster | gpu:voxelCardinalElect | 0.019 (0.018..0.020, 0.001) | head | 0.013 (0.013..0.013) | 0.677x | -0.006 (0.001) | -0.007 | reported | - |
| canvas-default-raster | gpu:voxelCompact | 0.043 (0.040..0.044, 0.002) | head | 0.037 (0.037..0.038) | 0.878x | -0.005 (0.002) | -0.007 | reported | - |
| canvas-default-raster | gpu:voxelStage1 | 0.043 (0.041..0.046, 0.002) | head | 0.028 (0.028..0.028) | 0.645x | -0.015 (0.002) | -0.017 | reported | - |
| canvas-default-raster | gpu:voxelStage2 | 0.021 (0.019..0.021, 0.001) | head | 0.014 (0.014..0.014) | 0.680x | -0.007 (0.001) | -0.007 | reported | - |
| canvas-default-raster | gpu_envelope_ms | 7.277 (7.112..7.414, 0.113) | head | 7.891 (7.809..7.946) | 1.084x | +0.614 (0.125) | +0.494 | reported | - |
| canvas-default-raster | steady_mean_ms | 21.872 (21.370..22.090, 0.291) | head | 22.820 (22.560..22.930) | 1.043x | +0.948 (0.358) | +0.607 | ratio <= 1.10x | pass |
| canvas-default-raster | steady_p99_ms | 27.928 (26.720..29.500, 1.313) | head | 24.992 (24.690..25.300) | 0.895x | -2.936 (1.299) | -4.175 | ratio <= 1.10x | pass |

| cell | arm | steady frame-time bins (share of frames) |
|---|---|---|
| canvas-default-raster | baseline | 20..21 ms 21%, 21..22 ms 33%, 22..23 ms 21%, 23..24 ms 10% |
| canvas-default-raster | per-pixel | 20..21 ms 15%, 21..22 ms 39%, 22..23 ms 28%, 23..24 ms 12% |
| canvas-default-raster | fills | 20..21 ms 12%, 21..22 ms 39%, 22..23 ms 29%, 23..24 ms 15% |
| canvas-default-raster | head | 21..22 ms 21%, 22..23 ms 40%, 23..24 ms 23%, 24..25 ms 14% |

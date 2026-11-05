| cell | metric | base mean (min..max, sd) | arm | arm mean (min..max) | arm / base | paired delta mean (sd) | lower 95% | limit | verdict |
|---|---|---|---|---|---|---|---|---|---|
| canvas-default-raster | cpu:BakeSunShadowMap | 0.126 (0.124..0.129, 0.002) | per-pixel | 0.122 (0.116..0.129) | 0.967x | -0.004 (0.007) | -0.011 | reported | - |
| canvas-default-raster | cpu:CanvasToFramebuffer | 0.046 (0.045..0.048, 0.001) | per-pixel | 0.048 (0.043..0.054) | 1.039x | +0.002 (0.005) | -0.003 | reported | - |
| canvas-default-raster | cpu:ComputeSunShadow | 0.006 (0.006..0.006, 0.000) | per-pixel | 0.006 (0.006..0.007) | 1.033x | +0.000 (0.000) | -0.000 | reported | - |
| canvas-default-raster | cpu:LightingToTrixel | 0.143 (0.140..0.145, 0.002) | per-pixel | 0.138 (0.131..0.146) | 0.965x | -0.005 (0.007) | -0.012 | reported | - |
| canvas-default-raster | cpu:SingleVoxelToCanvasFirst | 1.941 (1.891..2.067, 0.073) | per-pixel | 1.847 (1.778..1.938) | 0.951x | -0.094 (0.124) | -0.213 | reported | - |
| canvas-default-raster | gpu:bakeSunShadowMap | 2.401 (2.308..2.459, 0.067) | per-pixel | 3.160 (3.123..3.203) | 1.316x | +0.759 (0.061) | +0.701 | reported | - |
| canvas-default-raster | gpu:canvasClear | 0.014 (0.013..0.015, 0.001) | per-pixel | 0.008 (0.008..0.009) | 0.618x | -0.005 (0.001) | -0.006 | reported | - |
| canvas-default-raster | gpu:cardinalSurfaceFb | 2.552 (2.235..2.677, 0.179) | per-pixel | 3.357 (3.309..3.403) | 1.316x | +0.806 (0.203) | +0.612 | reported | - |
| canvas-default-raster | gpu:computeLightVolume | 0.073 (0.061..0.078, 0.007) | per-pixel | 0.257 (0.217..0.278) | 3.536x | +0.185 (0.025) | +0.161 | reported | - |
| canvas-default-raster | gpu:computeSunShadow | 0.038 (0.031..0.040, 0.004) | per-pixel | 0.034 (0.033..0.035) | 0.884x | -0.004 (0.004) | -0.008 | reported | - |
| canvas-default-raster | gpu:computeVoxelAO | 0.178 (0.166..0.195, 0.011) | per-pixel | 0.288 (0.272..0.304) | 1.617x | +0.110 (0.021) | +0.090 | reported | - |
| canvas-default-raster | gpu:entityCanvasToFb | 3.283 (2.977..3.403, 0.173) | per-pixel | 3.927 (3.883..3.976) | 1.196x | +0.644 (0.199) | +0.455 | reported | - |
| canvas-default-raster | gpu:fbToScreen | 0.056 (0.054..0.060, 0.002) | per-pixel | 0.040 (0.039..0.040) | 0.705x | -0.017 (0.002) | -0.019 | reported | - |
| canvas-default-raster | gpu:lightingToTrixel | 0.030 (0.026..0.031, 0.002) | per-pixel | 0.023 (0.022..0.024) | 0.770x | -0.007 (0.002) | -0.009 | reported | - |
| canvas-default-raster | gpu:shapeDepth | 0.153 (0.147..0.167, 0.008) | per-pixel | 0.106 (0.105..0.108) | 0.695x | -0.047 (0.008) | -0.054 | reported | - |
| canvas-default-raster | gpu:shapeOwnerClear | 0.022 (0.020..0.025, 0.002) | per-pixel | 0.006 (0.006..0.007) | 0.279x | -0.016 (0.002) | -0.018 | reported | - |
| canvas-default-raster | gpu:shapeOwnerElect | 0.152 (0.146..0.166, 0.008) | per-pixel | 0.106 (0.104..0.109) | 0.696x | -0.046 (0.006) | -0.052 | reported | - |
| canvas-default-raster | gpu:shapePublish | 0.157 (0.151..0.170, 0.008) | per-pixel | 0.107 (0.106..0.108) | 0.681x | -0.050 (0.007) | -0.057 | reported | - |
| canvas-default-raster | gpu:trixelToFb | 1.750 (1.519..1.848, 0.132) | per-pixel | 2.783 (2.754..2.839) | 1.591x | +1.033 (0.122) | +0.917 | reported | - |
| canvas-default-raster | gpu:voxelCardinalElect | 0.020 (0.020..0.021, 0.001) | per-pixel | 0.015 (0.015..0.015) | 0.735x | -0.005 (0.001) | -0.006 | reported | - |
| canvas-default-raster | gpu:voxelCompact | 0.047 (0.046..0.048, 0.001) | per-pixel | 0.040 (0.039..0.040) | 0.839x | -0.008 (0.001) | -0.008 | reported | - |
| canvas-default-raster | gpu:voxelStage1 | 0.046 (0.044..0.048, 0.002) | per-pixel | 0.033 (0.032..0.033) | 0.716x | -0.013 (0.002) | -0.015 | reported | - |
| canvas-default-raster | gpu:voxelStage2 | 0.022 (0.021..0.023, 0.001) | per-pixel | 0.017 (0.016..0.017) | 0.755x | -0.005 (0.001) | -0.006 | reported | - |
| canvas-default-raster | gpu_envelope_ms | 7.447 (7.301..7.551, 0.104) | per-pixel | 7.544 (7.433..7.666) | 1.013x | +0.096 (0.140) | -0.037 | reported | - |
| canvas-default-raster | steady_mean_ms | 22.640 (22.360..22.930, 0.223) | per-pixel | 22.358 (22.030..22.810) | 0.988x | -0.282 (0.439) | -0.701 | ratio <= 1.10x | pass |
| canvas-default-raster | steady_p99_ms | 28.784 (27.400..32.620, 2.163) | per-pixel | 25.990 (24.600..27.970) | 0.903x | -2.794 (3.125) | -5.774 | ratio <= 1.10x | pass |
| canvas-default-raster | cpu:BakeSunShadowMap | 0.126 (0.124..0.129, 0.002) | fills | 0.125 (0.121..0.129) | 0.997x | -0.000 (0.003) | -0.003 | reported | - |
| canvas-default-raster | cpu:CanvasToFramebuffer | 0.046 (0.045..0.048, 0.001) | fills | 0.046 (0.043..0.048) | 0.987x | -0.001 (0.003) | -0.003 | reported | - |
| canvas-default-raster | cpu:ComputeSunShadow | 0.006 (0.006..0.006, 0.000) | fills | 0.006 (0.006..0.006) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-default-raster | cpu:LightingToTrixel | 0.143 (0.140..0.145, 0.002) | fills | 0.136 (0.130..0.142) | 0.955x | -0.006 (0.004) | -0.010 | reported | - |
| canvas-default-raster | cpu:SingleVoxelToCanvasFirst | 1.941 (1.891..2.067, 0.073) | fills | 1.847 (1.787..1.891) | 0.951x | -0.094 (0.056) | -0.148 | reported | - |
| canvas-default-raster | gpu:bakeSunShadowMap | 2.401 (2.308..2.459, 0.067) | fills | 3.042 (3.003..3.087) | 1.267x | +0.642 (0.060) | +0.584 | reported | - |
| canvas-default-raster | gpu:canvasClear | 0.014 (0.013..0.015, 0.001) | fills | 0.008 (0.008..0.009) | 0.618x | -0.005 (0.001) | -0.006 | reported | - |
| canvas-default-raster | gpu:cardinalSurfaceFb | 2.552 (2.235..2.677, 0.179) | fills | 3.229 (3.196..3.278) | 1.266x | +0.678 (0.174) | +0.512 | reported | - |
| canvas-default-raster | gpu:computeLightVolume | 0.073 (0.061..0.078, 0.007) | fills | 0.183 (0.156..0.198) | 2.514x | +0.110 (0.019) | +0.092 | reported | - |
| canvas-default-raster | gpu:computeSunShadow | 0.038 (0.031..0.040, 0.004) | fills | 0.032 (0.031..0.033) | 0.847x | -0.006 (0.004) | -0.010 | reported | - |
| canvas-default-raster | gpu:computeVoxelAO | 0.178 (0.166..0.195, 0.011) | fills | 0.089 (0.087..0.092) | 0.498x | -0.090 (0.011) | -0.100 | reported | - |
| canvas-default-raster | gpu:entityCanvasToFb | 3.283 (2.977..3.403, 0.173) | fills | 3.801 (3.765..3.865) | 1.158x | +0.518 (0.165) | +0.361 | reported | - |
| canvas-default-raster | gpu:fbToScreen | 0.056 (0.054..0.060, 0.002) | fills | 0.039 (0.039..0.040) | 0.701x | -0.017 (0.002) | -0.019 | reported | - |
| canvas-default-raster | gpu:lightingToTrixel | 0.030 (0.026..0.031, 0.002) | fills | 0.023 (0.023..0.024) | 0.784x | -0.006 (0.002) | -0.008 | reported | - |
| canvas-default-raster | gpu:shapeDepth | 0.153 (0.147..0.167, 0.008) | fills | 0.105 (0.104..0.107) | 0.689x | -0.048 (0.009) | -0.056 | reported | - |
| canvas-default-raster | gpu:shapeOwnerClear | 0.022 (0.020..0.025, 0.002) | fills | 0.006 (0.005..0.007) | 0.261x | -0.016 (0.002) | -0.019 | reported | - |
| canvas-default-raster | gpu:shapeOwnerElect | 0.152 (0.146..0.166, 0.008) | fills | 0.104 (0.103..0.106) | 0.685x | -0.048 (0.009) | -0.056 | reported | - |
| canvas-default-raster | gpu:shapePublish | 0.157 (0.151..0.170, 0.008) | fills | 0.108 (0.106..0.110) | 0.686x | -0.049 (0.008) | -0.057 | reported | - |
| canvas-default-raster | gpu:trixelToFb | 1.750 (1.519..1.848, 0.132) | fills | 2.632 (2.598..2.656) | 1.504x | +0.882 (0.133) | +0.755 | reported | - |
| canvas-default-raster | gpu:voxelCardinalElect | 0.020 (0.020..0.021, 0.001) | fills | 0.015 (0.015..0.015) | 0.735x | -0.005 (0.001) | -0.006 | reported | - |
| canvas-default-raster | gpu:voxelCompact | 0.047 (0.046..0.048, 0.001) | fills | 0.040 (0.039..0.040) | 0.839x | -0.008 (0.001) | -0.008 | reported | - |
| canvas-default-raster | gpu:voxelStage1 | 0.046 (0.044..0.048, 0.002) | fills | 0.033 (0.033..0.034) | 0.725x | -0.013 (0.002) | -0.014 | reported | - |
| canvas-default-raster | gpu:voxelStage2 | 0.022 (0.021..0.023, 0.001) | fills | 0.016 (0.016..0.016) | 0.727x | -0.006 (0.001) | -0.007 | reported | - |
| canvas-default-raster | gpu_envelope_ms | 7.447 (7.301..7.551, 0.104) | fills | 7.393 (7.330..7.447) | 0.993x | -0.054 (0.084) | -0.135 | reported | - |
| canvas-default-raster | steady_mean_ms | 22.640 (22.360..22.930, 0.223) | fills | 22.198 (21.980..22.310) | 0.980x | -0.442 (0.120) | -0.556 | ratio <= 1.10x | pass |
| canvas-default-raster | steady_p99_ms | 28.784 (27.400..32.620, 2.163) | fills | 26.040 (25.210..27.460) | 0.905x | -2.744 (2.752) | -5.368 | ratio <= 1.10x | pass |
| canvas-default-raster | cpu:BakeSunShadowMap | 0.126 (0.124..0.129, 0.002) | head | 0.128 (0.122..0.135) | 1.019x | +0.002 (0.006) | -0.003 | reported | - |
| canvas-default-raster | cpu:CanvasToFramebuffer | 0.046 (0.045..0.048, 0.001) | head | 0.048 (0.044..0.053) | 1.039x | +0.002 (0.004) | -0.002 | reported | - |
| canvas-default-raster | cpu:ComputeSunShadow | 0.006 (0.006..0.006, 0.000) | head | 0.006 (0.006..0.006) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-default-raster | cpu:LightingToTrixel | 0.143 (0.140..0.145, 0.002) | head | 0.140 (0.133..0.149) | 0.982x | -0.003 (0.007) | -0.009 | reported | - |
| canvas-default-raster | cpu:SingleVoxelToCanvasFirst | 1.941 (1.891..2.067, 0.073) | head | 1.873 (1.793..1.964) | 0.965x | -0.068 (0.085) | -0.149 | reported | - |
| canvas-default-raster | gpu:bakeSunShadowMap | 2.401 (2.308..2.459, 0.067) | head | 3.570 (3.542..3.624) | 1.487x | +1.170 (0.096) | +1.078 | reported | - |
| canvas-default-raster | gpu:canvasClear | 0.014 (0.013..0.015, 0.001) | head | 0.007 (0.007..0.008) | 0.529x | -0.006 (0.001) | -0.007 | reported | - |
| canvas-default-raster | gpu:cardinalSurfaceFb | 2.552 (2.235..2.677, 0.179) | head | 3.650 (3.621..3.715) | 1.430x | +1.098 (0.177) | +0.929 | reported | - |
| canvas-default-raster | gpu:computeLightVolume | 0.073 (0.061..0.078, 0.007) | head | 0.197 (0.169..0.236) | 2.701x | +0.124 (0.025) | +0.100 | reported | - |
| canvas-default-raster | gpu:computeSunShadow | 0.038 (0.031..0.040, 0.004) | head | 0.025 (0.025..0.026) | 0.668x | -0.013 (0.004) | -0.016 | reported | - |
| canvas-default-raster | gpu:computeVoxelAO | 0.178 (0.166..0.195, 0.011) | head | 0.086 (0.085..0.087) | 0.481x | -0.093 (0.012) | -0.104 | reported | - |
| canvas-default-raster | gpu:entityCanvasToFb | 3.283 (2.977..3.403, 0.173) | head | 4.114 (4.086..4.189) | 1.253x | +0.831 (0.174) | +0.665 | reported | - |
| canvas-default-raster | gpu:fbToScreen | 0.056 (0.054..0.060, 0.002) | head | 0.032 (0.031..0.032) | 0.566x | -0.024 (0.002) | -0.027 | reported | - |
| canvas-default-raster | gpu:lightingToTrixel | 0.030 (0.026..0.031, 0.002) | head | 0.018 (0.017..0.018) | 0.601x | -0.012 (0.002) | -0.014 | reported | - |
| canvas-default-raster | gpu:shapeDepth | 0.153 (0.147..0.167, 0.008) | head | 0.082 (0.081..0.084) | 0.539x | -0.071 (0.009) | -0.079 | reported | - |
| canvas-default-raster | gpu:shapeOwnerClear | 0.022 (0.020..0.025, 0.002) | head | 0.004 (0.004..0.004) | 0.180x | -0.018 (0.002) | -0.020 | reported | - |
| canvas-default-raster | gpu:shapeOwnerElect | 0.152 (0.146..0.166, 0.008) | head | 0.082 (0.081..0.083) | 0.539x | -0.070 (0.008) | -0.078 | reported | - |
| canvas-default-raster | gpu:shapePublish | 0.157 (0.151..0.170, 0.008) | head | 0.084 (0.083..0.085) | 0.534x | -0.073 (0.008) | -0.081 | reported | - |
| canvas-default-raster | gpu:trixelToFb | 1.750 (1.519..1.848, 0.132) | head | 2.784 (2.731..2.880) | 1.591x | +1.034 (0.120) | +0.920 | reported | - |
| canvas-default-raster | gpu:voxelCardinalElect | 0.020 (0.020..0.021, 0.001) | head | 0.013 (0.013..0.013) | 0.637x | -0.007 (0.001) | -0.008 | reported | - |
| canvas-default-raster | gpu:voxelCompact | 0.047 (0.046..0.048, 0.001) | head | 0.038 (0.037..0.038) | 0.797x | -0.010 (0.001) | -0.011 | reported | - |
| canvas-default-raster | gpu:voxelStage1 | 0.046 (0.044..0.048, 0.002) | head | 0.029 (0.029..0.029) | 0.633x | -0.017 (0.002) | -0.018 | reported | - |
| canvas-default-raster | gpu:voxelStage2 | 0.022 (0.021..0.023, 0.001) | head | 0.014 (0.014..0.015) | 0.655x | -0.008 (0.001) | -0.009 | reported | - |
| canvas-default-raster | gpu_envelope_ms | 7.447 (7.301..7.551, 0.104) | head | 7.613 (7.567..7.665) | 1.022x | +0.166 (0.118) | +0.053 | reported | - |
| canvas-default-raster | steady_mean_ms | 22.640 (22.360..22.930, 0.223) | head | 22.474 (22.180..22.900) | 0.993x | -0.166 (0.275) | -0.428 | ratio <= 1.10x | pass |
| canvas-default-raster | steady_p99_ms | 28.784 (27.400..32.620, 2.163) | head | 26.034 (24.840..26.910) | 0.904x | -2.750 (2.839) | -5.457 | ratio <= 1.10x | pass |

| cell | arm | steady frame-time bins (share of frames) |
|---|---|---|
| canvas-default-raster | baseline | 20..21 ms 17%, 21..22 ms 24%, 22..23 ms 21%, 23..24 ms 14% |
| canvas-default-raster | per-pixel | 20..21 ms 11%, 21..22 ms 35%, 22..23 ms 27%, 23..24 ms 18% |
| canvas-default-raster | fills | 20..21 ms 14%, 21..22 ms 33%, 22..23 ms 27%, 23..24 ms 17% |
| canvas-default-raster | head | 21..22 ms 31%, 22..23 ms 33%, 23..24 ms 16%, 24..25 ms 9% |

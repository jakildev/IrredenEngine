| cell | metric | baseline mean (min..max) | head mean (min..max) | head / baseline |
|---|---|---|---|---|
| canvas-lower | cpu:BakeSunShadowMap | 0.034 (0.031..0.039) | 0.044 (0.021..0.089) | 1.287x |
| canvas-lower | cpu:CanvasToFramebuffer | 0.111 (0.097..0.125) | 0.239 (0.068..0.749) | 2.165x |
| canvas-lower | cpu:ComputeSunShadow | 0.014 (0.013..0.016) | 0.253 (0.009..1.209) | 17.789x |
| canvas-lower | cpu:LightingToTrixel | 0.039 (0.035..0.044) | 0.198 (0.023..0.843) | 5.130x |
| canvas-lower | cpu:SingleVoxelToCanvasFirst | 0.225 (0.202..0.245) | 0.248 (0.145..0.304) | 1.102x |
| canvas-lower | gpu:bakeSunShadowMap | 1.414 (1.385..1.449) | 2.799 (2.716..2.855) | 1.979x |
| canvas-lower | gpu:canvasClear | 0.029 (0.028..0.029) | 0.030 (0.027..0.037) | 1.056x |
| canvas-lower | gpu:cardinalSurfaceFb | 1.941 (1.891..2.002) | 3.309 (3.179..3.421) | 1.705x |
| canvas-lower | gpu:computeLightVolume | 0.510 (0.479..0.534) | 0.110 (0.108..0.114) | 0.217x |
| canvas-lower | gpu:computeSunShadow | 0.055 (0.052..0.058) | 0.051 (0.049..0.053) | 0.923x |
| canvas-lower | gpu:computeVoxelAO | 0.130 (0.120..0.145) | 0.189 (0.177..0.198) | 1.448x |
| canvas-lower | gpu:fbToScreen | 0.121 (0.118..0.125) | 0.120 (0.114..0.123) | 0.987x |
| canvas-lower | gpu:lightingToTrixel | 0.042 (0.040..0.043) | 0.040 (0.038..0.042) | 0.966x |
| canvas-lower | gpu:trixelToFb | 1.690 (1.651..1.744) | 1.395 (1.292..1.461) | 0.825x |
| canvas-lower | gpu:voxelCardinalElect | 0.265 (0.249..0.275) | 0.377 (0.323..0.420) | 1.421x |
| canvas-lower | gpu:voxelCompact | 0.118 (0.092..0.128) | 0.119 (0.095..0.134) | 1.010x |
| canvas-lower | gpu:voxelStage1 | 0.091 (0.089..0.093) | 0.090 (0.084..0.093) | 0.980x |
| canvas-lower | gpu:voxelStage2 | 0.228 (0.221..0.234) | 0.221 (0.195..0.235) | 0.973x |
| canvas-lower | gpu_envelope_ms | 2.446 (2.397..2.522) | 3.816 (3.668..3.915) | 1.560x |
| canvas-lower | steady_mean_ms | 8.456 (8.330..8.760) | 12.280 (9.320..14.760) | 1.452x |
| canvas-lower | steady_p99_ms | 12.512 (9.390..18.010) | 20.198 (19.450..20.870) | 1.614x |

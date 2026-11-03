# Interleaved A/B timing: baseline vs growth

5 alternating pairs per cell; whole-frame budget 1.10x.

## analytic-control (IRCanvasStress)

| Measurement | Baseline mean ms (min–max) | growth mean ms (min–max) | Ratio | Budget |
|---|---:|---:|---:|---|
| frame_avg | 75.314 (75.230–75.450) | 75.038 (74.730–75.450) | 0.996 | within |
| frame_p99 | 79.684 (79.230–80.260) | 80.750 (79.320–83.990) | 1.013 | within |
| steady_avg | 74.906 (74.730–75.010) | 74.616 (74.250–74.980) | 0.996 | within |
| steady_p99 | 79.068 (78.810–79.310) | 79.018 (78.330–79.500) | 0.999 | within |
| gpu_frame (envelope) | 46.286 (46.087–46.672) | 46.088 (45.802–46.436) | 0.996 | baseline spread 0.585 ms |

| Variant | Profiles | Steady frames per profile | Completed steady samples | Retried profiles |
|---|---:|---:|---:|---:|
| baseline | 5 | 270 | 1350 | 0 |
| growth | 5 | 270 | 1350 | 0 |

| Variant | Final capacity | Allocated bytes | Cleared bytes per frame | Resizes per profile | Resize CPU µs (min–max, n) |
|---|---:|---:|---:|---:|---:|
| baseline | 65,536 | 30,670,864 | 17,039,376 | 0 | — |
| growth | 65,536 | 30,670,868 | 17,039,376 | 0 | — |

| GPU stage | Baseline mean ms (min–max) | growth mean ms (min–max) | Delta ms | Baseline spread ms |
|---|---:|---:|---:|---:|
| bakeSunShadowMap | 0.024 (0.024–0.024) | 0.027 (0.027–0.028) | +0.003 | 0.000 |
| canvasClear | 0.076 (0.074–0.079) | 0.101 (0.100–0.102) | +0.025 | 0.005 |
| cardinalSurfaceFb | 0.688 (0.685–0.692) | 0.686 (0.677–0.699) | -0.003 | 0.007 |
| computeLightVolume | 0.037 (0.037–0.037) | 0.037 (0.037–0.038) | +0.000 | 0.000 |
| computeSunShadow | 0.063 (0.063–0.063) | 0.062 (0.061–0.063) | -0.001 | 0.000 |
| computeVoxelAO | 0.068 (0.066–0.069) | 0.067 (0.066–0.068) | -0.000 | 0.003 |
| entityCanvasToFb | 7.579 (7.512–7.615) | 7.542 (7.461–7.643) | -0.038 | 0.103 |
| fbToScreen | 0.037 (0.037–0.037) | 0.037 (0.037–0.038) | +0.000 | 0.000 |
| lightingToTrixel | 0.028 (0.028–0.028) | 0.028 (0.028–0.029) | +0.000 | 0.000 |
| shapeCastBake | 0.207 (0.204–0.214) | 0.207 (0.204–0.213) | +0.000 | 0.010 |
| shapeCastBoxes | 0.260 (0.256–0.266) | 0.262 (0.258–0.268) | +0.002 | 0.010 |
| shapeCastClear | 0.093 (0.092–0.094) | 0.093 (0.092–0.094) | +0.000 | 0.002 |
| shapeCastFallback | 0.007 (0.007–0.007) | 0.007 (0.007–0.007) | +0.000 | 0.000 |
| shapeCastResolve | 0.021 (0.021–0.021) | 0.021 (0.021–0.022) | +0.000 | 0.000 |
| shapeDepth | 0.009 (0.009–0.010) | 0.010 (0.009–0.010) | +0.000 | 0.001 |
| shapeOwnerClear | 0.016 (0.016–0.017) | 0.016 (0.016–0.017) | +0.000 | 0.001 |
| shapeOwnerElect | 0.010 (0.010–0.011) | 0.010 (0.010–0.010) | -0.000 | 0.001 |
| shapePublish | 0.013 (0.012–0.013) | 0.012 (0.012–0.013) | -0.000 | 0.001 |
| trixelToFb | 0.046 (0.045–0.047) | 0.045 (0.045–0.046) | -0.001 | 0.002 |
| voxelCardinalElect | 0.015 (0.015–0.015) | 0.015 (0.015–0.015) | +0.000 | 0.000 |
| voxelCompact | 0.017 (0.017–0.018) | 0.018 (0.018–0.019) | +0.001 | 0.001 |
| voxelStage1 | 0.068 (0.067–0.069) | 0.073 (0.072–0.074) | +0.005 | 0.002 |
| voxelStage2 | 0.017 (0.017–0.017) | 0.017 (0.016–0.017) | -0.000 | 0.000 |
| voxelSunFaces | 0.035 (0.034–0.036) | 0.035 (0.035–0.035) | -0.000 | 0.002 |

## analytic-exhausted (IRCanvasStress)

| Measurement | Baseline mean ms (min–max) | growth mean ms (min–max) | Ratio | Budget |
|---|---:|---:|---:|---|
| frame_avg | 105.350 (104.760–106.790) | 104.912 (104.540–105.550) | 0.996 | within |
| frame_p99 | 136.994 (134.030–140.740) | 138.172 (136.950–140.060) | 1.009 | within |
| steady_avg | 105.318 (104.370–106.710) | 104.248 (103.350–104.870) | 0.990 | within |
| steady_p99 | 135.336 (133.930–136.140) | 136.898 (136.100–137.550) | 1.012 | within |
| gpu_frame (envelope) | 56.811 (56.370–57.788) | 56.836 (56.391–57.464) | 1.000 | baseline spread 1.418 ms |

| Variant | Profiles | Steady frames per profile | Completed steady samples | Retried profiles |
|---|---:|---:|---:|---:|
| baseline | 5 | 270 | 1350 | 0 |
| growth | 5 | 270 | 1350 | 0 |

| Variant | Final capacity | Allocated bytes | Cleared bytes per frame | Resizes per profile | Resize CPU µs (min–max, n) |
|---|---:|---:|---:|---:|---:|
| baseline | 65,536 | 30,670,864 | 17,039,376 | 0 | — |
| growth | 131,072 | 33,816,596 | 17,039,376 | 1 | 27–55, 5 |

| GPU stage | Baseline mean ms (min–max) | growth mean ms (min–max) | Delta ms | Baseline spread ms |
|---|---:|---:|---:|---:|
| bakeSunShadowMap | 0.038 (0.037–0.038) | 0.042 (0.041–0.042) | +0.004 | 0.001 |
| canvasClear | 0.137 (0.126–0.145) | 0.135 (0.134–0.137) | -0.002 | 0.019 |
| cardinalSurfaceFb | 0.872 (0.855–0.880) | 0.847 (0.834–0.863) | -0.025 | 0.025 |
| computeLightVolume | 0.042 (0.041–0.042) | 0.040 (0.040–0.040) | -0.002 | 0.001 |
| computeSunShadow | 0.071 (0.069–0.072) | 0.089 (0.088–0.091) | +0.018 | 0.003 |
| computeVoxelAO | 0.068 (0.067–0.069) | 0.066 (0.065–0.066) | -0.002 | 0.002 |
| entityCanvasToFb | 7.381 (7.320–7.419) | 8.860 (8.825–8.913) | +1.479 | 0.099 |
| fbToScreen | 0.043 (0.043–0.043) | 0.037 (0.037–0.038) | -0.006 | 0.000 |
| lightingToTrixel | 0.035 (0.034–0.035) | 0.032 (0.032–0.032) | -0.003 | 0.001 |
| shapeCastBake | 0.223 (0.214–0.228) | 0.214 (0.208–0.221) | -0.008 | 0.014 |
| shapeCastBoxes | 0.274 (0.264–0.280) | 0.266 (0.261–0.273) | -0.007 | 0.016 |
| shapeCastClear | 0.103 (0.101–0.106) | 0.101 (0.100–0.101) | -0.003 | 0.005 |
| shapeCastFallback | 0.008 (0.008–0.009) | 0.008 (0.008–0.008) | -0.000 | 0.001 |
| shapeCastResolve | 0.025 (0.025–0.026) | 0.023 (0.023–0.024) | -0.002 | 0.001 |
| shapeDepth | 0.011 (0.011–0.012) | 0.011 (0.010–0.011) | -0.001 | 0.001 |
| shapeOwnerClear | 0.018 (0.017–0.019) | 0.016 (0.016–0.017) | -0.002 | 0.002 |
| shapeOwnerElect | 0.012 (0.012–0.013) | 0.012 (0.011–0.012) | -0.001 | 0.001 |
| shapePublish | 0.015 (0.014–0.015) | 0.014 (0.014–0.014) | -0.001 | 0.001 |
| trixelToFb | 0.049 (0.048–0.050) | 0.049 (0.047–0.050) | -0.000 | 0.002 |
| voxelCardinalElect | 0.029 (0.027–0.031) | 0.027 (0.027–0.028) | -0.001 | 0.004 |
| voxelCompact | 0.028 (0.026–0.029) | 0.027 (0.027–0.028) | -0.000 | 0.003 |
| voxelStage1 | 0.100 (0.094–0.105) | 0.099 (0.097–0.101) | -0.001 | 0.011 |
| voxelStage2 | 0.032 (0.030–0.034) | 0.031 (0.030–0.032) | -0.001 | 0.004 |
| voxelSunFaces | 0.054 (0.051–0.057) | 0.054 (0.053–0.055) | -0.001 | 0.006 |

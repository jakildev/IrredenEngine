# Interleaved A/B timing: baseline vs frozen-65536

3 alternating pairs per cell; whole-frame budget 1.10x.

## grid-zoom1 (IRPerfGrid)

| Measurement | Baseline mean ms (min–max) | frozen-65536 mean ms (min–max) | Ratio | Budget |
|---|---:|---:|---:|---|
| frame_avg | 31.660 (31.210–32.380) | 31.817 (31.350–32.330) | 1.005 | within |
| frame_p99 | 666.893 (655.870–681.420) | 661.323 (657.780–666.080) | 0.992 | within |
| steady_avg | 21.010 (20.650–21.390) | 21.393 (20.790–21.750) | 1.018 | within |
| steady_p99 | 23.147 (22.880–23.490) | 23.707 (23.580–23.790) | 1.024 | within |
| gpu_frame (envelope) | 4.742 (4.451–4.911) | 5.046 (4.706–5.278) | 1.064 | baseline spread 0.460 ms |

| Variant | Profiles | Steady frames per profile | Completed steady samples | Retried profiles |
|---|---:|---:|---:|---:|
| baseline | 3 | 45 | 135 | 0 |
| frozen-65536 | 3 | 45 | 135 | 0 |

| Variant | Final capacity | Allocated bytes | Cleared bytes per frame | Resizes per profile | Resize CPU µs (min–max, n) |
|---|---:|---:|---:|---:|---:|
| baseline | 65,536 | 30,670,864 | 17,039,376 | 0 | — |
| frozen-65536 | 65,536 | 30,670,868 | 17,039,376 | 0 | — |

| GPU stage | Baseline mean ms (min–max) | frozen-65536 mean ms (min–max) | Delta ms | Baseline spread ms |
|---|---:|---:|---:|---:|
| bakeSunShadowMap | 0.089 (0.085–0.093) | 0.096 (0.091–0.100) | +0.007 | 0.008 |
| canvasClear | 0.067 (0.060–0.076) | 0.117 (0.107–0.122) | +0.050 | 0.016 |
| computeLightVolume | 2.574 (2.380–2.677) | 2.640 (2.450–2.754) | +0.067 | 0.297 |
| computeSunShadow | 0.070 (0.067–0.073) | 0.070 (0.065–0.075) | -0.001 | 0.006 |
| computeVoxelAO | 0.055 (0.052–0.058) | 0.054 (0.050–0.057) | -0.001 | 0.006 |
| fbToScreen | 0.120 (0.111–0.126) | 0.121 (0.113–0.126) | +0.001 | 0.015 |
| fogToTrixel | 0.033 (0.031–0.034) | 0.032 (0.030–0.033) | -0.001 | 0.003 |
| lightingToTrixel | 0.057 (0.054–0.060) | 0.058 (0.055–0.061) | +0.001 | 0.006 |
| textToTrixel | 1.261 (1.155–1.389) | 1.571 (1.468–1.658) | +0.311 | 0.234 |
| trixelToFb | 1.268 (1.202–1.329) | 1.096 (0.986–1.168) | -0.172 | 0.127 |
| voxelCardinalElect | 0.464 (0.421–0.499) | 0.452 (0.418–0.470) | -0.011 | 0.078 |
| voxelCompact | 0.309 (0.297–0.324) | 0.783 (0.745–0.833) | +0.475 | 0.027 |
| voxelStage1 | 0.565 (0.526–0.584) | 0.559 (0.522–0.590) | -0.005 | 0.058 |
| voxelStage2 | 0.451 (0.415–0.471) | 0.449 (0.414–0.469) | -0.002 | 0.056 |
| voxelSunFaces | 1.077 (1.025–1.108) | 1.095 (1.022–1.143) | +0.018 | 0.083 |

## grid-zoom4 (IRPerfGrid)

| Measurement | Baseline mean ms (min–max) | frozen-65536 mean ms (min–max) | Ratio | Budget |
|---|---:|---:|---:|---|
| frame_avg | 34.880 (34.420–35.310) | 35.037 (34.510–35.470) | 1.004 | within |
| frame_p99 | 663.090 (655.580–676.300) | 671.780 (662.180–677.730) | 1.013 | within |
| steady_avg | 24.147 (23.970–24.370) | 24.267 (23.920–24.650) | 1.005 | within |
| steady_p99 | 28.477 (28.040–29.110) | 28.213 (27.490–29.290) | 0.991 | within |
| gpu_frame (envelope) | 7.343 (7.214–7.480) | 7.296 (7.078–7.424) | 0.994 | baseline spread 0.266 ms |

| Variant | Profiles | Steady frames per profile | Completed steady samples | Retried profiles |
|---|---:|---:|---:|---:|
| baseline | 3 | 45 | 135 | 0 |
| frozen-65536 | 3 | 45 | 135 | 0 |

| Variant | Final capacity | Allocated bytes | Cleared bytes per frame | Resizes per profile | Resize CPU µs (min–max, n) |
|---|---:|---:|---:|---:|---:|
| baseline | 65,536 | 30,670,864 | 17,039,376 | 0 | — |
| frozen-65536 | 65,536 | 30,670,868 | 17,039,376 | 0 | — |

| GPU stage | Baseline mean ms (min–max) | frozen-65536 mean ms (min–max) | Delta ms | Baseline spread ms |
|---|---:|---:|---:|---:|
| bakeSunShadowMap | 0.037 (0.036–0.037) | 0.037 (0.036–0.038) | +0.001 | 0.001 |
| canvasClear | 0.018 (0.012–0.030) | 0.048 (0.046–0.051) | +0.030 | 0.018 |
| computeLightVolume | 5.951 (5.846–6.047) | 5.903 (5.727–6.020) | -0.048 | 0.201 |
| computeSunShadow | 0.051 (0.050–0.053) | 0.051 (0.049–0.052) | -0.001 | 0.003 |
| computeVoxelAO | 0.052 (0.051–0.053) | 0.051 (0.049–0.052) | -0.001 | 0.002 |
| fbToScreen | 0.036 (0.036–0.037) | 0.036 (0.035–0.037) | -0.000 | 0.001 |
| fogToTrixel | 0.016 (0.015–0.016) | 0.015 (0.015–0.016) | -0.000 | 0.001 |
| lightingToTrixel | 0.041 (0.040–0.042) | 0.042 (0.039–0.043) | +0.001 | 0.002 |
| textToTrixel | 0.653 (0.594–0.698) | 1.029 (0.990–1.063) | +0.376 | 0.104 |
| trixelToFb | 1.045 (1.018–1.077) | 0.745 (0.711–0.774) | -0.300 | 0.059 |
| voxelCardinalElect | 1.808 (1.779–1.825) | 1.782 (1.720–1.825) | -0.026 | 0.046 |
| voxelCompact | 0.191 (0.159–0.237) | 0.704 (0.684–0.717) | +0.514 | 0.078 |
| voxelStage1 | 1.937 (1.911–1.987) | 1.911 (1.870–1.935) | -0.026 | 0.076 |
| voxelStage2 | 1.792 (1.750–1.818) | 1.751 (1.699–1.796) | -0.041 | 0.068 |
| voxelSunFaces | 0.975 (0.962–0.997) | 0.971 (0.946–0.985) | -0.003 | 0.035 |

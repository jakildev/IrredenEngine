# Production Metal hierarchy build comparison

Device: Apple M4 Max. Base: `e766f2bb80f5e06d346fc96a9b3dcf70906fa823`.

PASS: 38 paired builds; 70658 used nodes checked independently.
All readiness/tree bytes match; guards, non-hierarchy storage and unused nodes are unchanged.
Fast and safe math compile the expanded production shaders independently.

GPU command envelope per build (not engine frame timing).
At least 3 seconds burn-in per case/mode; alternating forward/reverse arm order.

| Case | Math | Base median ms | Head median ms |
|---|---|---:|---:|
| empty | fast | 0.023858 | 0.003025 |
| not-needed | fast | 0.037099 | 0.004928 |
| finite-1 | fast | 0.028658 | 0.005165 |
| finite-8 | fast | 0.033731 | 0.010730 |
| finite-9 | fast | 0.029953 | 0.009971 |
| finite-1023 | fast | 0.039707 | 0.012781 |
| finite-1024 | fast | 0.039191 | 0.012719 |
| finite-1025 | fast | 0.035815 | 0.012322 |
| finite-65536 | fast | 0.039870 | 0.014107 |
| mixed-1025 | fast | 0.040120 | 0.013139 |
| empty | safe | 0.023560 | 0.003025 |
| not-needed | safe | 0.026652 | 0.003447 |
| finite-1 | safe | 0.028014 | 0.005164 |
| finite-8 | safe | 0.034012 | 0.010587 |
| finite-9 | safe | 0.029412 | 0.010375 |
| finite-1023 | safe | 0.038997 | 0.012629 |
| finite-1024 | safe | 0.039439 | 0.012819 |
| finite-1025 | safe | 0.034237 | 0.011968 |
| finite-65536 | safe | 0.045048 | 0.016537 |
| mixed-1025 | safe | 0.038542 | 0.012753 |

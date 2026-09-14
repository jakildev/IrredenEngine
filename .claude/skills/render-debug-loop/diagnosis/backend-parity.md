# Diagnosis: Backend parity (OpenGL ↔ Metal)

A backend-specific defect calls for comparing bindings, layouts, sampling and
synchronization as well as shader logic. It can expose a shared pipeline bug
that the other backend happens to tolerate. Capture matching before/after shots;
use the `backend-parity` skill when a backend port or correction is needed.

## Parity-only symptoms

| Symptom | Likely surface |
|---|---|
| Defect at zoom 1 only on Metal | Dispatch-grid helper rounding floor vs ceil differently |
| Atomic writes flicker on Metal | `atomicAdd` → `atomic_fetch_add_explicit` memory order |
| Texture sampling off by half a pixel | MSL `sample` vs GLSL `texelFetch` addressing |
| Buffer binding index wrong on one side | `kBufferIndex_*` constant not mirrored across backends |

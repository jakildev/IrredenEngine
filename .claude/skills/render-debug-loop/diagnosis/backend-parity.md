# Diagnosis: Backend parity (OpenGL ↔ Metal)

A defect on one backend only is a parity problem, not a pipeline bug. This
loop captures the evidence (before/after shots from both backends); the
`backend-parity` skill — GLSL↔MSL cheatsheet and port procedure — drives the
port.

## Parity-only symptoms

| Symptom | Likely surface |
|---|---|
| Defect at zoom 1 only on Metal | Dispatch-grid helper rounding floor vs ceil differently |
| Atomic writes flicker on Metal | `atomicAdd` → `atomic_fetch_add_explicit` memory order |
| Texture sampling off by half a pixel | MSL `sample` vs GLSL `texelFetch` addressing |
| Buffer binding index wrong on one side | `kBufferIndex_*` constant not mirrored across backends |

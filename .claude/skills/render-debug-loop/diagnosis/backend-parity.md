# Diagnosis: Backend parity (OpenGL ↔ Metal)

When a visual defect appears on only one backend, this is a parity problem, not a pipeline bug. Hand off to the `backend-parity` skill — its GLSL↔MSL cheatsheet and per-port checklist are the right tool. The `render-debug-loop` captures the evidence (before/after screenshots from both backends); `backend-parity` drives the port.

## Common parity-only symptoms

| Symptom | Likely surface |
|---------|----------------|
| Defect at zoom 1 only on Metal | Dispatch-grid helper returning floor-vs-ceil differently |
| Atomic writes flicker on Metal | `atomicAdd` → `atomic_fetch_add_explicit` memory-order |
| Texture sampling off-by-half-pixel | MSL `sample` vs. GLSL `texelFetch` addressing conventions |
| Buffer binding index wrong on one side | `kBufferIndex_*` constant not mirrored across backends |

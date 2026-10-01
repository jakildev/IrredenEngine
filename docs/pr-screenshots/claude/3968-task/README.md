## Canvas hover-canary evidence

`hover-canary-after.png` is the corrected `IRCanvasStress --gui-session
hover_canary` frame. The cube is intentionally off-center so the independent
world-to-mouse projection exposes framebuffer/mouse Y-axis mismatches.

The baseline capture was not retained because the baseline GUI process reported
zero displays and was stopped by the 120-second watchdog before assertions
completed.

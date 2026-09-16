# Frozen scatter coverage arbitration

A frozen IRCanvasStress pose at yaw 45 degrees alternated twelve RGB pixels
between two shades. The camera, geometry and executable were unchanged.
Twenty captures (996–1015) reproduce this; disabling the overflow draw still
flickers (1016–1035). AO is stable (1036–1055), while the margin diagnostic
alternates exact Z-face coverage and expanded Z-face margin (1056–1075).
The demo now exposes the existing `peraxis_margin` overlay in its CLI.

A temporary cell-identity probe localized the competing records to low-byte
`(cell.x, cell.y, rawDepth)` values `(159,118,11)` and `(161,118,14)`.
Their two-bit cell codes collide. Atomic cell compaction changes their draw order,
so an equal final depth lets either the exact face or the expanded margin win.
This disproves the earlier assumption that the lattice-neighbor cell code
separates every same-axis overlap: displaced cells can differ by `(2,0)`.
The probe is not part of the shipped shaders.

## Contract

Keep the existing depth band, face/cell priority, geometric footprint and margin
yield. For an equal band and face/cell code, exact coverage precedes margin
coverage. The low bit of a 24-bit integer depth code carries that distinction:

```text
q = bandIndex * 32 + faceCellCode * 2 + isMargin
normalized = (q + (q >= 2^23 ? 1 : 0)) * 2^-24
```

The correction avoids a fixed-depth rounding collision at 0.5 without a floating
reciprocal. Every integer q from 0 through 2^24-1 maps back to q under nearest
D24 conversion and remains strictly ordered in float32. All operations use
exactly representable integers and powers of two. Shader twins share the same
formula in the dedicated `ir_scatter_depth` fragments; unrelated consumers of
the iso helpers do not compile the new function.

This is final coverage arbitration, not smoothing or an enlarged depth tolerance.
Same-class/equal-code collisions still follow draw order. Relative to the prior
normalized depth, upper-half exact fragments move by 2^-24 and upper-half margin
fragments by 2^-23. Cross-pass ties at that scale can change. The largest margin
code maps to depth 1 and loses to a depth buffer cleared to 1 with LESS, as a
far-plane boundary should. No new buffers, sorting, dispatches or CPU waits.

## Evidence

Commands use frozen pose 0.47, `--no-auto-rotate --no-spin`, six-frame screenshot
warmup. A constant `--sweep-yaw 0.785398163 0.785398163 20` captures temporal
stability; `--sweep-yaw 0 6.2831853 9` covers the quadrants.

- Final normal captures 1136–1155 have identical full-frame RGB bytes. Final
  margin captures 1165–1184 are also identical and select exact coverage at the
  reported patch. The earlier half-step-only Metal experiment gives exactly the
  same scene RGB as the portable formula; its D24 midpoint collision prevents
  shipping that simpler expression.
- Nine-angle sweep 1156–1164: eight parent images match byte for byte. At 45°,
  64 pixels along the same horizontal face junction change relative to the
  parent's darker-patch frame; 76 differ from its brighter-patch frame. This
  includes coverage ties that previously picked a stable but margin-owned winner.
  Cardinal views and the surrounding SDF floor remain identical.
- The headless GPU test runs the actual shader helper at 256 codes around band,
  midpoint and endpoint boundaries. Float ordering and a CPU nearest-D24 model
  pass. Removing the upper-half correction fails starting at code 8388609;
  restoring it passes. An exhaustive mathematical check of all 16,777,216 codes
  found zero D24 conversion mismatches. This is not a native OpenGL attachment
  test; OpenGL runtime remains unverified on this host.
- Native build, header/Metal registry checks and comment lint pass. Focused
  reviewer found no executable blocker; determinism claims were narrowed to
  the proven coverage class.

Full before/after images and labeled nearest-neighbor 8x crops are retained in
`docs/pr-screenshots/codex/frozen-scatter-flicker/`. These captures establish this
specific defect, not the absence of every flicker a moving camera might reveal.

Three frozen 64³/yaw45/zoom4 Debug IRPerfGrid runs average 19.370 ms/frame
(19.240–19.470), versus 19.323 (19.250–19.360) before this fix. This is within
the observed run spread, not a speedup claim. Sampling was disabled; GPU profiling
was enabled. [Reports and provenance](../perf/frozen-scatter-flicker/) are retained.

## Follow-up

Retry overflow deduplication against this stable control. Its prior 12-pixel
rejection coincided with the same independently reproduced cell-path flicker;
that coincidence does not establish deduplication equivalence. Keep same-class
collisions and broader continuous-camera temporal coverage on the TODO.

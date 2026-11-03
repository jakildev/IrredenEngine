# Selected finite surface query

Native Metal, Apple M4 Max, 2560x1440, September 24, 2026.
Parent: `ebfa8d86fc3e1564019a3786bdbb1b78d979e90c`.

```sh
fleet-run --timeout 45 IRCanvasStress --only shadowbox,floor --probe-analytic-box --pivot-origin --no-spin --no-auto-rotate --no-ao --subdivisions 3 --zoom 2.5 --auto-screenshot 6 --sweep-yaw 0 0.78539816 2
```

The run compiled the specialized Metal shadow kernel and exited cleanly.
[Yaw 0](capture-2291.png) and [yaw 45](capture-2292.png) are pixel-identical
(RGB ImageChops difference is empty) to the corresponding previously recorded
[2273/2274 lighting-contract controls](../surface-lighting-contract/README.md).
Those controls predate the parent sky correction; this command does not enable
the sky-only HDR probe. This is output equivalence, not a new sharp-edge result.
The floor shadow outline remains visibly jagged and requires fragment receiving.

Executed shader controls retain 377,920 finite hits and 235,392 misses per
backend. Selection tests separately exercise stored-owner addressing, fractional
query forwarding and no output replacement on failed queries, including a
callee that writes outputs before reporting a miss. Snapping the query to the
owner is a failing mutation. Scalar adapters do not replace native OpenGL
validation, which remains pending.

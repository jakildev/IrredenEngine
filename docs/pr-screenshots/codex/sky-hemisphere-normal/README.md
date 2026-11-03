# Sky hemisphere normal evidence

Native Metal, Apple M4 Max, 2560x1440, September 24, 2026.
Base: `f02c8ce2176b23ce1aa3a2384f73de906aa7a19d` plus only the new demo probe.
The fixed captures use this commit's shader changes. All runs exited cleanly.

```sh
fleet-run --timeout 45 IRCanvasStress --only shadowbox,floor --probe-analytic-box --probe-sky --pivot-origin --no-spin --no-auto-rotate --no-ao --subdivisions 3 --zoom 2.5 --auto-screenshot 6 --sweep-yaw 0 0.78539816 2
```

The probe enables HDR, white sky intensity 1, exposure 1, and zero sun
intensity. No local lights are present in these selected fixtures. Black
vertical faces are expected: this isolates the upper-hemisphere response.
The box top and floor have the same sky response, so their boundary disappears.

| Case | Yaw 0 | Yaw 45 |
|---|---|---|
| Before: wrong hemisphere, all black | [2281](capture-2281.png) | [2282](capture-2282.png) |
| After: top and floor lit, vertical sides black | [2283](capture-2283.png) | [2284](capture-2284.png) |
| After with `--no-shadows` | [2285](capture-2285.png) | [2286](capture-2286.png) |
| Rotated source-face control | [2287](capture-2287.png) | [2288](capture-2288.png) |

RGB comparison using Pillow ImageChops gives no differing pixels for 2283/2285
and 2284/2286. Sun visibility cannot darken the sky term in this control.
For the source-face run replace `--probe-analytic-box` with
`--probe-source-box --probe-overlapping-source --frozen-pose 0.65`.
Tilted faces have intermediate sky response; both camera angles retain it.

Validation: 229 rendering tests pass; shader-execution tests cover all six
signed normals and 7,992 tilted-normal/yaw/AO combinations per backend.
Wrong-hemisphere and omitted-AO mutations fail. IRCanvasStress build,
format-changed, header-checks and ruff pass. Three focused reviews found no
blockers. Native underside and native OpenGL coverage remain pending;
underside behavior is covered by the executed scalar shader tests.

This corrects the sky sign, not the outstanding finite per-fragment SDF
receiver edges. It adds no filtering, allocations or dispatches. Sky remains
an AO-modulated hemisphere approximation, not traced environmental visibility.

Main-grid per-axis control: [yaw 0](capture-2289.png),
[yaw 45](capture-2290.png). Use `--only maingrid,floor` and omit
`--probe-analytic-box` from the command above. Tops retain the same sky response
as the floor; vertical faces stay black. The existing bounded per-axis canvas
subdivision cap was logged at yaw 45; this patch does not change that cap.

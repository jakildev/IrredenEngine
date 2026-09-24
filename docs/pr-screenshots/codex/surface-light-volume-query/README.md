# Shared surface light-volume query

Mechanical extraction on top of `149db5db099187e5918c9524c766c875f0c02336`.
Native Metal, Apple M4 Max, 2026-09-24. No shadow-edge improvement is claimed.

```sh
fleet-build -j3 --target IRLightingSpot header-checks
fleet-run --timeout 45 IRLightingSpot --auto-screenshot 6 --no-ao
```

| View | Shared query | Parent | RGB difference |
|---|---|---|---|
| yaw 0, zoom 4 | [115](capture-115.png) | [119](capture-119.png) | zero |
| yaw 30, zoom 4 | [116](capture-116.png) | [120](capture-120.png) | zero |
| yaw 45, zoom 4 | [117](capture-117.png) | [121](capture-121.png) | zero |
| yaw 0, zoom 7 | [118](capture-118.png) | [122](capture-122.png) | zero |

The same binary ran both sets. For the parent control, only its two original
Metal lighting shader bodies were staged into the isolated demo runtime; the
new bodies were restored afterwards. Images were compared in RGB. These views
exercise a seeded spotlight at cardinal and rotating per-axis camera angles;
they do not establish that all existing geometry artifacts are correct.

IRCanvasStress analytical-box beauty controls 2367/2368 also matched parent
2345/2346 exactly (yaw 0/45, subdivisions 3, zoom 2.5, no AO/spin). See the
[parent evidence](../fragment-caster-footprint/README.md) for that fixture.

Validation: 233 render tests pass; each backend executes 594 checked local-light
queries and rejects five deliberate mutations. Header checks, native build and
script lint pass. Three focused simplify reviewers found no actionable issues.
Native OpenGL execution remains pending. Sampler policy stays at call sites
so the independent Metal filtering change in PR #3740 can still apply.

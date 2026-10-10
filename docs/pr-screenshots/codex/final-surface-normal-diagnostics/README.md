# Final-surface normal diagnostics

Parent: `e4f11e0998448d161c8979be8adef0991b179556`. Native macOS Metal,
1280×720 game resolution, 2560×1440 framebuffer.
[implementation.patch](implementation.patch) records the captured source change.

The `normals` overlay follows finite BOX fragment lighting wherever beauty uses
that lighting. Previously it showed the compute normal even when presentation
selected another finite face. Procedural materials, fog pipelines, unsupported
owners and failed queries retain their existing fallback, matching beauty's
eligibility. Normal colors bypass material, AO, shadow, exposure and tone mapping.
No geometry, coverage, depth, new buffer or dispatch is introduced.

## Captures

| Control | Command | Result |
|---|---|---|
| Normal overlay | `fleet-run IRCanvasStress --auto-screenshot 120 --debug-overlay normals` | Twelve captures; changed floor boundary colors now follow the fragment receiver |
| Canvas beauty | `fleet-run IRCanvasStress --auto-screenshot 120` | All twelve full frames byte-identical to parent |
| Explored fog | `fleet-run IRFogDemo --auto-screenshot 10 --explored-decay` | All five full frames byte-identical to parent; fog probes passed |

All runs exited CLEAN. Capture JSON files contain commands and all image hashes;
filtered logs retain camera poses, capture events, fog probes and wrapper exits.
[comparisons.json](comparisons.json) includes every before/after hash and changed
RGB pixel count. Representative full frames are retained here.

The parent normal/beauty controls were captured during the
[receiver-edge investigation](../receiver-edge-validation/README.md); the parent
reference-only commits do not alter their rendered inputs. No reference PNG or
comparison threshold changes in this slice.

## Diagnostic agreement

The earlier isolated floor-fragment probe returned the actual lighting normal
before material evaluation. Its six signed-axis RGB colors identify the floor
region. Comparing that region with the new built-in overlay gives:

| Shot index | Floor pixels | Old overlay disagreements | New disagreements |
|---|---:|---:|---:|
| 1 (`so3_offsnap_disc`) | 203,704 | 1,216 | 0 |
| 4 (`so3_offsnap_wide`) | 199,608 | 1,320 | 0 |

For example, pixel (818,668) in shot 1 changes from +X `(255,128,128)` to
top -Z `(128,128,0)`; pixel (1675,806) changes from top -Z to side -Y
`(128,0,128)`. Both match the isolated fragment probe. This establishes which
normal the diagnostic observes, not an independent proof of every surface.
Signed-face geometry and ray/slab controls remain in the receiver test suite.

## Checks and scope

- `test_render_shape_surface_lighting.py`: both GLSL and Metal adapters pass
  20,736 beauty-composition cases and 21 normal controls, with mutation rejection.
  The controls cover six signed axes, an angled normal and procedural fallbacks;
  debug output performs no material/AO/shadow/local-light reads.
- `test_render_per_axis_probe_routing.py`: two tests pass, including 176 finite
  lighting gate combinations and mutations removing normals, fog or depth guards.
- `test_render_fragment_shape_receiver.py`: both tests pass, retaining query,
  owner, coverage/alpha, shadow and binding-restoration controls.
- Header/binding checks, Ruff, comment-reference lint and whitespace checks pass.
  Native IRCanvasStress and IRFogDemo build and execute successfully.
- Focused correctness and simplify reviews found no blocking issues.

The explored-decay source comments now describe full BOX sizes and suspended
panels. Its fog square conservatively covers the SDF panels; its larger voxel
source panels have their own boundary-column margin. Geometry and fog policy
are unchanged. This fixture does not prove floor contact or SDF/voxel parity.

Native OpenGL, a new whole-demo reference sweep and performance profiling were
not run. This is a diagnostic correction and fixture-contract cleanup, not a
claim that every visible artifact or fallback lighting mode is resolved.

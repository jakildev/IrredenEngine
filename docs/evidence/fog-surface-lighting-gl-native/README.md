# Fog-composing surface lighting on Windows/OpenGL: the 10 non-origin placements

PR #4853 (evidence on the `media` branch under
`docs/pr-screenshots/claude/4813-gl-fog-surface-lighting/`) ran the fog-composing
GLSL twins on Windows/OpenGL and graded the receiver-shadow polygon oracle at the
8 origin placements only. GL then rendered the Metal config at 1×, and
Windows clamped a 2560×1440 window to 1421 rows. #4852 (PR #5186) makes a 2560×1440
offscreen request capture a 2560×1440 frame. This directory grades the other 10
placements (`near` ×4 yaws, `mid` ×4 yaws, `subpos`, `subneg`) **natively**, with no
upscale, on master `00b5da049`.

Host, driver and binary hashes: [artifacts.txt](artifacts.txt). NVIDIA RTX 3060 Ti,
driver 581.80, `windows-debug`. No code changed.

## Drivers

- [cap.py](drivers/cap.py.txt) is the earlier `cap.py` with `WINDOW = ('2560', '1440')`.
  It uses the Metal capture argv unchanged: the `shadowbox,floor` fixture,
  `--fog-reveal 16`, and offscreen `config.lua` with `game_resolution` 1280×720.
- [matrix.py](drivers/matrix.py.txt) runs the 10 non-origin placements of PR #4853's
  `drivers/matrix_defs.py.txt` (same placements and variants as the Metal matrices).
  It takes `--fog`, `--surface` and the variant list, and resumes from finished
  captures.
- [grade.py](drivers/grade.py.txt) runs `scripts/render_receiver_shadow_polygon.py`
  unchanged. It refuses any frame that is not 2560×1440 and compares each count
  dictionary with the Metal `results.json` row for the same key. It also audits every
  capture: exactly one frame with the recorded hash, a `SURFACE-ROUTE` line,
  `RESULT=CLEAN`, `OpenGL offscreen screen target attached (2560x1440)`, and no GL
  error lines.
- [shift_probe.py](drivers/shift_probe.py.txt) grades the sub-pixel frames shifted by
  whole pixels (see below).

[polygon/results.json](polygon/results.json) holds every row: command, env, route,
target line, frame hash, counts, the Metal row, and the audit.
[polygon/shift_probe.json](polygon/shift_probe.json) holds the shift grid.

## Readings

122 captures, all `RESULT=CLEAN` and audit-clean: 82 graded rows plus 40 local-light references. Every opt-in-on capture with fog logs `fog=compose fogComposed=1`, every opt-in-off capture logs `fog=fallback fogComposed=0`, and the two fog-off discriminators log `fog=none fogComposed=0`.

| Opt-in | `--fog-reveal` | Placements | Palette | GL m/e/u | Metal m/e/u | Count dicts equal Metal |
|---|---|---|---|---|---|---|
| on | 16 | near/mid ×8 | default | 0/0/0 | 0/0/0 | 8/8 |
| on | 16 | near/mid ×8 | ambient0.15 | 0/0/0 | 0/0/0 | 8/8 |
| on | 16 | near/mid ×8 | ambient0.55 | 0/0/0 | 0/0/0 | 8/8 |
| on | 16 | near/mid ×8 | local | 0/0/0 | 0/0/0 | 8/8 |
| on | 16 | subpos ×1 | default | 21/26/0 | 0/0/0 | 0/1 |
| on | 16 | subpos ×1 | ambient0.15 | 21/26/0 | 0/0/0 | 0/1 |
| on | 16 | subpos ×1 | ambient0.55 | 21/26/0 | 0/0/0 | 0/1 |
| on | 16 | subpos ×1 | local | 21/26/0 | 0/0/0 | 0/1 |
| on | 16 | subneg ×1 | default | 21/26/0 | 0/0/0 | 0/1 |
| on | 16 | subneg ×1 | ambient0.15 | 21/26/0 | 0/0/0 | 0/1 |
| on | 16 | subneg ×1 | ambient0.55 | 21/26/0 | 0/0/0 | 0/1 |
| on | 16 | subneg ×1 | local | 21/26/0 | 0/0/0 | 0/1 |
| on | 0 | subpos ×1 | default | 21/26/0 | 0/0/0 | 0/1 |
| on | 0 | subneg ×1 | default | 21/26/0 | 0/0/0 | 0/1 |
| off | 16 | near/mid ×8 | default | 18/638/0, 18/644/0, 22/660/0, 32/650/0 | 18/638/0, 18/644/0, 22/660/0, 32/650/0 | 8/8 |
| off | 16 | near/mid ×8 | ambient0.15 | 18/638/0, 18/644/0, 22/660/0, 32/650/0 | 18/638/0, 18/644/0, 22/660/0, 32/650/0 | 8/8 |
| off | 16 | near/mid ×8 | ambient0.55 | 18/638/0, 18/644/0, 22/660/0, 32/650/0 | 18/638/0, 18/644/0, 22/660/0, 32/650/0 | 8/8 |
| off | 16 | near/mid ×8 | local | 18/638/0, 18/644/0, 22/660/0, 32/650/0 | 18/638/0, 18/644/0, 22/660/0, 32/650/0 | 8/8 |
| off | 16 | subpos ×1 | default | 28/647/0 | 18/638/0 | 0/1 |
| off | 16 | subpos ×1 | ambient0.15 | 28/647/0 | 18/638/0 | 0/1 |
| off | 16 | subpos ×1 | ambient0.55 | 28/647/0 | 18/638/0 | 0/1 |
| off | 16 | subpos ×1 | local | 28/647/0 | 18/638/0 | 0/1 |
| off | 16 | subneg ×1 | default | 28/647/0 | 18/638/0 | 0/1 |
| off | 16 | subneg ×1 | ambient0.15 | 28/647/0 | 18/638/0 | 0/1 |
| off | 16 | subneg ×1 | ambient0.55 | 28/647/0 | 18/638/0 | 0/1 |
| off | 16 | subneg ×1 | local | 28/647/0 | 18/638/0 | 0/1 |

## The sub-pixel placements: a GL screen-placement defect outside the fog twins (#5190)

`subpos` (`--camera-iso 0.1 -0.2`) and `subneg` (`-0.1 0.2`) give **21 missing /
26 excess / 0 unknown** on GL in every opt-in-on palette. Metal gives 0/0/0. Three
readings place the cause outside the fog-composing programs:

1. **The fog-off arm fails identically.** With the opt-in on and `--fog-reveal 0`,
   the route is `cardinal=0 perAxis=1 fog=none fogComposed=0`, so no fog program
   runs. It still gives 21/26/0, and its 47 failing pixels are **the same set** as the
   fog-composed arm's.
2. **The failure moves with the camera.** The subneg failing set is the subpos set
   translated by exactly (-3, +3), the difference of the two placements' analytic
   screen offsets, (1, -2) and (-2, 1).
3. **It is a whole-frame one-row offset.** Shifting the GL frame's content up one row
   gives **0/0/0** at both placements. Every other whole-pixel shift is worse.

| shift (dx, dy) | subpos m/e/u | subneg m/e/u |
|---|---|---|
| (0, 0) | 21/26/0 | 21/26/0 |
| **(0, -1)** | **0/0/0** | **0/0/0** |
| (-1, -1) / (1, -1) | 3/6/0 | 3/6/0 |
| (-1, 0) / (1, 0) | 24/25/0 / 28/40/0 | 24/25/0 / 28/40/0 |
| (0, 1) | 88/102/0 | 88/102/0 |

The residual is fractional in both x and y at these placements, (1.6, -1.6) and
(-1.6, 1.6) screen pixels, and x is placed correctly. `near` and `mid` have
whole-pixel residuals, and they grade 0/0/0. GL resolves a fractional **y** residual
to `ceil` where the contract (`camera_screen_offset`, which Metal meets exactly) has
`floor`. That fits a floor taken on GL's bottom-up y axis. It is filed as #5190 with
this repro. #4813's scope routes a GL defect outside the GLSL twins to its own issue.

## What remains for #4813

Criterion 2 is met at the 8 origin placements (PR #4853) and at the 8 `near`/`mid`
placements (here). The two sub-pixel placements wait on #5190. Once it lands, re-run
`matrix.py <out> default,ambient0.15,ambient0.55,local,local-lit,local-dark --fog 16 --surface 1,0`
for `subpos`/`subneg` (40 captures; delete those folders first), then `grade.py`. Every
row should equal Metal's: 0/0/0 opt-in on, 18/638/0 opt-in off.

## Engine tests

`fleet-run IrredenEngineTest --gtest_filter='Fog*:SurfaceFogRoute.*:PerAxisSurfaceLighting*'`
on `00b5da049`: `[  PASSED  ] 245 tests.`, `RESULT=CLEAN` ([engine-test.log](engine-test.log)).

## Retained captures

`polygon/retained/<variant>/<placement>/` keeps the frame, `config.lua` and
`capture.json` for `default/near-yaw0.002-fog16-surface1`, `default/subpos-yaw0.6-fog16-surface1`, `default/subpos-yaw0.6-fog0-surface1`, `default/subpos-yaw0.6-fog16-surface0`. Every other frame's hash is in `results.json`, and
so are each run's route, target and GL-error lines. The drivers' absolute scratch
paths are this host's. The PNG reader is the stdlib shim in
[pil_image_shim.py](drivers/pil_image_shim.py.txt), since Pillow is not installed here.

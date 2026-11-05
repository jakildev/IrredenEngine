# Gather-filter diagnostic evidence

Readings behind `docs/design/continuous-camera-zoom.md` § "Filtered minification", Metal only.

- `results.json`: `python3 scripts/render-continuous-zoom-metric.py filter --output <dir>`,
  `RESULT=PASS`. Per sweep: zoom, density and output scale, the ownership counts of each arm
  against nearest, the GRID / revoxelized temporal differences and the interior mean-colour
  spread.
- `yaw0-zoom2.05-nearest-owner-unrestricted.png`: shot 10 of the yaw-0 forward sweep
  (`z` 2.05, density 3). Left to right: nearest, owner, unrestricted. Top: the GRID checker
  body. Bottom: the touching red / green pair. Screen pixels enlarged 4x.
- `gpu-timing.txt`: whole-frame GPU envelope per `--auto-profile` run. `quiet` is ir-run's
  host-quiet verdict for that run.

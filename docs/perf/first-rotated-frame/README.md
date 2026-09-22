# First rotated frame: arms and provenance

Every arm: `IRPerfGrid --wave-freeze --no-overlay --config-preset
configs/perf/million-profiling-off.lua` plus the flags below, Release, macos,
Metal, pivot pinned, one screenshot a run through `fleet-run` (so the reports
carry no manifest; the console's `ir-run: RESULT=CLEAN … exit=0` was read for
every run and is not committed). Binaries: **unpatched** `18ab1d21…` is the
parked-set build of #3665 before the branch's clang-format pass, run from a
snapshot of its staging directory; **fullspan** `3cc55978…` is the same
source plus [fullspan.patch](fullspan.patch), which makes
`overflowSortDispatchSpan` return the cap so every merge stage is encoded
whatever the lagged count. The patch was applied and reverted around the
build and never committed; the file here is the record.

| Report | Flags | Binary | Capture SHA-256 |
|---|---|---|---|
| `fresh-1p2-unpatched.txt` | `--yaw 0 --yaw-step 0.020943951 --auto-profile 80 --capture-frame 2` (frame 1 on the cardinal without `--yaw-first-frame`; the run sweeps on to 94.8°) | unpatched | `5c707d6a6babfe8d` |
| `settled-1p2-unpatched.txt` | `--yaw 0.020943951 --pivot-origin --auto-profile 80 --capture-frame 77` | unpatched | `eafdfe3ad6993206` |
| `fresh-46p8-unpatched.txt` | `--yaw-first-frame 0 --yaw 0.816814 --auto-profile 80 --capture-frame 2` | unpatched | `e84eccb8cfd26e39` |
| `fresh-46p8-unpatched-repeat.txt` | the same, run again | unpatched | `10a159aaa104fc76` |
| `settled-46p8-unpatched.txt` | `--yaw 0.816814 --pivot-origin --auto-profile 80 --capture-frame 77` | unpatched | `f42cdb630fb35288` |
| `fresh-46p8-fullspan.txt` | `--yaw-first-frame 0 --yaw 0.816814 --auto-profile 80 --capture-frame 2` | fullspan | `f42cdb630fb35288` |
| `settled-46p8-fullspan.txt` | `--yaw 0.816814 --pivot-origin --auto-profile 80 --capture-frame 77` | fullspan | `f42cdb630fb35288` |

Captures are committed full-size under
`docs/pr-screenshots/claude/million-entity-render-first-rotated-frame/`, one
file per distinct hash (`settled-46p8deg-and-fullspan.png` stands for the
three `f42cdb630fb35288` arms).

# Objective: any fleet host can produce a clean-box-runnable package

**Status:** active

## Outcome
From any of the three supported hosts (Linux/WSL2, native Windows,
macOS), a creation packages into a double-clickable artifact that runs
on a machine with none of the dev toolchain installed.

## Done means
- [x] The macOS bundle walks transitive dependencies: the package helper
  resolves the complete non-system Mach-O closure, rewrites every bundled
  load edge, and validates the result from an extracted archive.
- [ ] A per-host package smoke exists: build + clean-box-run of a
  packaged reference demo (`IRShapeDebug` is the wired
  `irreden_package_target` reference) on each of the three hosts.
- [x] The `BUILD.md` "maturing the Linux build is first-class ongoing
  work / expect Linux-only breaks" caveat is retired — Linux build
  breakage is no longer an expected class.
- [ ] `fleet:needs-<host>-smoke` backlogs stay drained as a matter of
  course (platform-catchup is routine, not archaeology).

## Non-goals
Mobile or console targets; installer tooling, notarization, or signing
beyond the existing ad-hoc re-sign; WSL MIDI forwarding (MIDI demos run
from the Windows-native clone by design).

## Current state
Three debug presets with release twins (`CMakePresets.json`), bootstrap
scripts for Linux and macOS (`scripts/bootstrap_linux.sh`,
`scripts/bootstrap_macos.sh`; none for Windows), and the native-Windows
clone as the authoritative does-this-actually-ship host.
`irreden_package_target` (`cmake/ir_functions.cmake`) stages the executable
with its `data/`, `shaders/` and `scripts/`, bundles the Windows DLL closure
(`cmake/windows_bundle_dlls.cmake`) or the macOS Mach-O closure
(`cmake/macos_bundle_dylibs.cmake`), and zips `<target>-<os>-<arch>.zip`;
`IRShapeDebug`, `IRPersistRoundtrip` and `IRSceneReset` are wired, and the
launcher's Package button builds the target (`creations/launcher/main.lua`).
`scripts/package-verify.py` audits the extracted Windows or macOS archive's
library closure statically. `scripts/package-smoke.py` (#4765, PR #4778) is
the automated clean-box smoke: it unpacks the package, restores the archived
mode bits and launches the executable through `fleet-run`, with the host's
clean-box restriction applied by `ir-run`'s child options (`--child-env` /
`--child-unset` / `--child-prefix`) to the executable alone. Its macOS arm is
measured (`IRShapeDebug` runs CLEAN under the Homebrew-denial `sandbox-exec`
profile with `DYLD_*` unset; an archive missing `libglfw.3.dylib` fails with
the dyld error). Its Windows arm is measured (#4774): `IRShapeDebug` runs
CLEAN with the executable's `PATH` reduced to `System32` + `SystemRoot`
(passed in MSYS POSIX form, which arrives as exactly those two directories),
and an archive missing `avcodec-62.dll` exits 127 through the loader. The
Linux arm (#4775) is the only one left, so row 2 stays open.
The Linux "expect breakage" caveat is gone (PR #3260): `BUILD.md` says a
Linux-only break is fixed in a dedicated PR, and `engine-tests.yml` builds
`linux-debug` demos on every PR, so row 3 verifies. Smoke labels are a
post-merge ledger since #4160 (the OpenGL tier mints
`fleet:needs-windows-smoke` only; the Linux label is retired for minting):
36 Windows and 12 macOS merged PRs carry an undrained label today, the
oldest from 2026-09-21 and 2026-08-21, so row 4 stays open.

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
| 2026-07-20 | — | objective seeded |
| 2026-09-12 | PR #3260 | `BUILD.md` "maturing the Linux build / expect Linux-specific breakage" caveat removed; `engine-tests.yml` builds `linux-debug` demos on every PR. Row 3 verifies. |
| 2026-10-06 | #4160 | smoke labels become a post-merge ledger; `fleet:needs-linux-smoke` retired for minting (the OpenGL tier routes to Windows) |
| 2026-10-12 | #4352 | Windows package bundles the exe's full transitive DLL closure (FFmpeg + codecs + MinGW runtime); a hand-run clean-box launch and `--auto-record` pass with only the system dirs on `PATH`. The automated per-host smoke row stays open. |
| 2026-10-13 | #4333 / PR #4394 | macOS package bundles and ad-hoc signs the complete non-system dylib closure; an extracted-archive audit proves all load edges are bundle-local. The automated per-host smoke row stays open. |
| 2026-10-22 | #4674 | IRLauncher shows Package on targets whose `creation.json` sets `package` (`IRShapeDebug`, `IRPersistRoundtrip`, `IRSceneReset`): it builds `<target>Package` and shows the archive path the catalog finds. It does not run a clean-box smoke, so the per-host smoke row stays open. |
| 2026-10-27 | sweep; #4765 | re-verified against master `ee85797c0`; row 3 ticked; backlog depth measured (windows 36, oldest 2026-09-21; macos 12, oldest 2026-08-21); #4765 filed for the automated package smoke (row 2) |
| 2026-10-27 | #4765 | `scripts/package-smoke.py` automates the clean-box smoke via new `ir-run` child options (`--child-env` / `--child-unset` / `--child-prefix`) that restrict only the packaged executable. macOS arm measured: `IRShapeDebug` runs CLEAN under the Homebrew-denial `sandbox-exec` profile with `DYLD_*` unset; an archive missing `libglfw.3.dylib` fails with the dyld error. The per-host smoke row stays open until the Windows (#4774) and Linux (#4775) arms land. |
| 2026-10-28 | #4774, #4775 | Windows and Linux arms approved for their native runs (`fleet:agent-approved`, plan-residual of #4765; host-pinned) |
| 2026-10-31 | #4774 | Windows arm measured natively: `scripts/package-smoke.py` passes on `IRShapeDebug` (`RESULT=CLEAN`), and an archive missing `avcodec-62.dll` fails (`exit=127`, `RESULT=FAIL`). The harness now passes the clean-box `PATH` in MSYS POSIX form, because the Windows form is split at the drive colons. It also reads `SystemRoot` case-insensitively, because native Python spells it `SYSTEMROOT`. Row 2 stays open for the Linux arm (#4775). |

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
- [ ] The `BUILD.md` "maturing the Linux build is first-class ongoing
  work / expect Linux-only breaks" caveat is retired — Linux build
  breakage is no longer an expected class.
- [ ] `fleet:needs-<host>-smoke` backlogs stay drained as a matter of
  course (platform-catchup is routine, not archaeology).

## Non-goals
Mobile or console targets; installer tooling, notarization, or signing
beyond the existing ad-hoc re-sign; WSL MIDI forwarding (MIDI demos run
from the Windows-native clone by design).

## Current state
Three presets (`linux-debug` / `windows-debug` / `macos-debug`) with
per-host build docs and bootstrap scripts; `irreden_package_target`
produces per-platform zips; the native-Windows clone is the
authoritative does-this-actually-ship host; platform-catchup processes
cross-host smoke labels on all three hosts. `scripts/package-smoke.py`
is the automated clean-box smoke: it unpacks the package and launches it
through `fleet-run` with the host's clean-box restrictions applied to the
executable only. Its macOS arm is measured; the Windows (#4774) and Linux
(#4775) arms still need a native run. The remaining gaps are those two
native arms and Linux build maturity.

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
| 2026-07-20 | — | objective seeded |
| 2026-10-12 | #4352 | Windows package bundles the exe's full transitive DLL closure (FFmpeg + codecs + MinGW runtime); a hand-run clean-box launch and `--auto-record` pass with only the system dirs on `PATH`. The automated per-host smoke row stays open. |
| 2026-10-13 | #4333 / PR #4394 | macOS package bundles and ad-hoc signs the complete non-system dylib closure; an extracted-archive audit proves all load edges are bundle-local. The automated per-host smoke row stays open. |
| 2026-10-22 | #4674 | IRLauncher shows Package on targets whose `creation.json` sets `package` (`IRShapeDebug`, `IRPersistRoundtrip`, `IRSceneReset`): it builds `<target>Package` and shows the archive path the catalog finds. It does not run a clean-box smoke, so the per-host smoke row stays open. |
| 2026-10-27 | #4765 | `scripts/package-smoke.py` automates the clean-box smoke via new `ir-run` child options (`--child-env` / `--child-unset` / `--child-prefix`) that restrict only the packaged executable. macOS arm measured: `IRShapeDebug` runs CLEAN under the Homebrew-denial `sandbox-exec` profile with `DYLD_*` unset; an archive missing `libglfw.3.dylib` fails with the dyld error. The per-host smoke row stays open until the Windows (#4774) and Linux (#4775) arms land. |

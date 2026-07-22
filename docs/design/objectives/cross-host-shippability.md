# Objective: any fleet host can produce a clean-box-runnable package

**Status:** active

## Outcome
From any of the three supported hosts (Linux/WSL2, native Windows,
macOS), a creation packages into a double-clickable artifact that runs
on a machine with none of the dev toolchain installed.

## Done means
- [ ] The macOS bundle walks transitive dependencies — today
  `cmake/macos_bundle_dylibs.cmake` copies direct non-system dylibs but
  not their Homebrew transitive deps (ffmpeg codec libs), so a truly
  clean-box macOS bundle is admitted follow-up work (`BUILD.md`
  § packaging).
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
cross-host smoke labels on all three hosts. The gaps are bundle depth
(macOS transitive dylibs), the absence of an automated clean-box smoke,
and Linux build maturity.

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
| 2026-07-20 | — | objective seeded |

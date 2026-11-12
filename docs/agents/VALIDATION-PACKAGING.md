# VALIDATION-PACKAGING.md — packaging validators

Part of the [validation index](VALIDATION.md); its rules for citing a
validator apply. Open it for a change to a `<target>Package` archive or its
runtime closure. A row added here also adds its name to this page's line in
the index's area table.

| Validator | Proves | Run | CI |
|---|---|---|---|
| Package clean-box smoke | the unpacked `<target>Package` archive runs to `ir-run: RESULT=CLEAN` with the host's clean-box restrictions applied to the executable only (macOS: `DYLD_*` unset plus a `sandbox-exec` profile denying `/opt/homebrew` and `/usr/local`; Windows: `PATH` = System32 + SystemRoot, in MSYS POSIX form; Linux: `LD_LIBRARY_PATH` unset) | `python3 scripts/package-smoke.py [--target <target>] [--zip FILE]` after `fleet-build --target <target>Package`, from a normal shell on a display host; a pass reads `package-smoke: isolation=applied` (macOS), `ir-run: RESULT=CLEAN`, `package-smoke: RESULT=PASS`; `isolation=skipped` is not clean-box evidence. A Windows loader failure (a DLL missing from the archive) reads as `ir-run: RESULT=CRASH exit=127` with no loader text; `python3 scripts/package-verify.py --check audit` names the missing DLL. Native runs: macOS and Windows | render-harness-tests.yml runs `scripts/tests/test_package_smoke.py`; fleet-tests.yml runs `scripts/fleet/tests/test_ir_run_child_isolation.sh` |
| Windows package closure | the DLL resolver sees the four FFmpeg roots and DLLs reachable only through them; the unzipped `<target>Package` bundle resolves every import from itself or `%SystemRoot%` | `python3 scripts/package-verify.py [--target <target>] [--check probe\|audit]` after `fleet-build --target <target>Package` (native Windows only) | none |
| macOS package closure | the Mach-O resolver sees the four FFmpeg roots plus transitive-only dylibs; every non-system edge in the unzipped `<target>Package` resolves through `@loader_path`, no package-manager rpath remains, and every bundled Mach-O has a valid ad-hoc signature | `python3 scripts/package-verify.py [--target <target>] [--check probe\|audit]` after `fleet-build --target <target>Package` (native macOS only); the probe and audit must each report a transitive-only count greater than zero and `RESULT=PASS` | none |

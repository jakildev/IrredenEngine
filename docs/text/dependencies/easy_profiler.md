## Irreden Engine: Easy Profiler

### Details:
-   **Found in modules:** IRProfile
-   **Package type:** Dynamic library
-   **Precompiled:** No
-   **Version:** 2.1.0
-   **License:** MIT or Apache
-   **Url:** https://github.com/yse/easy_profiler
-   **Source:** https://github.com/yse/easy_profiler
-   **Docs:** https://github.com/yse/easy_profiler/wiki

### Used For:
-   Function and block profiling
-   Flamegraph/visualization GUI

### Build Configuration
-   Profiling markers are compiled through `IrredenEngineProfile` via `BUILD_WITH_EASY_PROFILER`.
-   GUI build toggle: `-DIRREDEN_PROFILE_ENABLE_EASY_PROFILER_GUI=ON|OFF`
-   On macOS, the GUI viewer requires Qt 5 (`qt@5` via Homebrew is the expected local setup).
-   The bootstrap helper `./scripts/bootstrap_macos.sh` installs the recommended macOS prerequisites.

### Runtime Notes
-   Runtime enablement is controlled by the `profiling_enabled` world config entry.
-   The engine writes `profiler_dump.prof` on shutdown when profiling is enabled.
-   Windows helper: `./open_profiler.ps1`
-   macOS helper: `./open_profiler_macos.sh`

### Future Use:


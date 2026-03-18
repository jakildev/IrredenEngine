## Irreden Engine: FFMpeg

### Details:
-   **Found in modules:** IRVideo
-   **Package type:** Optional runtime/build dependency (enabled in current module)
-   **Precompiled:** Supported (recommended for Windows via `IRREDEN_FFMPEG_ROOT`)
-   **Version:** N/A
-   **License:** LGPL/GPL
-   **Url:** https://ffmpeg.org/
-   **Source:** https://git.ffmpeg.org/ffmpeg.git
-   **Docs:** https://ffmpeg.org/documentation.html

### Used For:
-   In-engine video export through `IRVideo::VideoRecorder`
-   RGBA frame encoding to MP4/H264 without an external screen recorder

### Build Configuration

-   Toggle support: `-DIRREDEN_VIDEO_ENABLE_FFMPEG=ON`
-   Optional explicit root (Windows prebuilt bundles): `-DIRREDEN_FFMPEG_ROOT=C:/ffmpeg`
    -   Expected layout under that root:
        -   `include/libavcodec/avcodec.h`
        -   `lib/avcodec(.lib|.a)` and companion libs (`avformat`, `avutil`, `swscale`)
        -   `bin/*.dll` (copied to build output on Windows)
-   If `IRREDEN_FFMPEG_ROOT` is not set, the build tries `pkg-config` (`libavcodec libavformat libavutil libswscale`).
-   On macOS, the recommended path is Homebrew:
    -   `./scripts/bootstrap_macos.sh`
    -   or manually install `ffmpeg` and `pkg-config`
-   The macOS build also checks common Homebrew prefixes such as `/opt/homebrew` and `/usr/local` before falling back to stub mode.

### Runtime Notes

-   Current recorder path is video-first and optimized for high frame rate capture (`ultrafast` + `zerolatency` encoder options).
-   Current implementation includes asynchronous encoding queue/worker to reduce main thread stalls while recording.
-   Current implementation includes GPU asynchronous readback via a PBO ring buffer in `VideoManager` to further reduce render-thread stalls.
-   Audio muxing is implemented, with optional fallback WAV output when muxing or input capture is unavailable.
-   On macOS, microphone capture may require granting microphone permission to the host app (for example, Terminal or the IDE).

### Future Use:
-   Add AAC audio capture/mux path with shared clock.

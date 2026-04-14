#ifndef IR_VIDEO_VIDEO_RECORDER_H
#define IR_VIDEO_VIDEO_RECORDER_H

#include <cstdint>
#include <string>
#include <vector>

namespace IRVideo {

/// Configuration for a single recording session. Pass to @c VideoRecorder::start or
/// @c IRVideo::startRecording. All fields have sane defaults; at minimum set
/// @c width_, @c height_, and @c source_width_, @c source_height_ for the output.
struct VideoRecorderConfig {
    /// Output file path (MP4 container). Created or overwritten on @c start.
    std::string output_file_path_ = "capture.mp4";
    /// Output frame dimensions after any scaling applied by the FFmpeg pipeline.
    int width_ = 0;
    int height_ = 0;
    /// Source frame dimensions as supplied to @c submitVideoFrame. The pipeline
    /// scales source → (width_, height_) if they differ.
    int source_width_ = 0;
    int source_height_ = 0;
    /// Target output frame rate. Frames submitted faster than this are dropped;
    /// slower submission causes the output to run short.
    int target_fps_ = 60;
    /// Video bitrate in bits/s (default 8 Mbps — good for 1080p gameplay at 60 fps).
    int video_bitrate_ = 8'000'000;

    /// @{
    /// @name Audio capture settings
    /// When @c capture_audio_input_ is true, the engine opens @c audio_input_device_name_
    /// via RtAudio and feeds interleaved float samples into the recording. Call
    /// @c IRAudio::startAudioInputCapture and wire its callback to
    /// @c VideoRecorder::submitAudioInputSamples so audio is routed to the encoder.
    bool capture_audio_input_ = false;
    /// RtAudio device name to open for capture. Must match the name returned by
    /// @c IRAudio::startAudioInputCapture's device enumeration.
    std::string audio_input_device_name_ = "";
    int audio_sample_rate_ = 48'000;
    int audio_channels_ = 2;
    /// Audio bitrate in bits/s. Ignored when @c audio_lossless_ is true.
    int audio_bitrate_ = 320'000;
    /// Use a lossless audio codec (e.g. FLAC) instead of AAC.
    bool audio_lossless_ = false;
    /// Mux the captured audio into the MP4 container alongside the video stream.
    bool audio_mux_enabled_ = true;
    /// Also write a sidecar @c .wav file next to @c output_file_path_. Useful for
    /// post-processing audio independently of the video.
    bool audio_wav_enabled_ = true;
    /// Manual A/V sync correction in milliseconds. Positive values delay audio
    /// relative to video; negative values advance it.
    double audio_sync_offset_ms_ = 0.0;
    /// @}
};

/// FFmpeg-backed video encoder. Wraps the full encode pipeline: RGBA → libx264/libx265
/// video, optional AAC/FLAC audio, muxed into an MP4 container.
///
/// Typical frame-submission loop:
/// @code
///   auto buf = recorder.acquireFrameBuffer(width * height * 4);
///   // fill buf with RGBA pixels ...
///   recorder.submitVideoFrame(std::move(buf));
/// @endcode
class VideoRecorder {
  public:
    VideoRecorder();
    ~VideoRecorder();

    /// @{
    /// @name Recording control
    /// @param config  Fully-populated @c VideoRecorderConfig; width/height must be non-zero.
    /// @return @c true on success; @c false if FFmpeg initialisation failed — call
    ///         @c getLastError() for the diagnostic string.
    bool start(const VideoRecorderConfig &config);
    /// Flush the encoder, close the output file, and reset internal state. Safe to call
    /// even when not recording (no-op).
    void stop();
    /// @}

    /// @{
    /// @name Frame submission
    /// Submit a raw RGBA8 frame. Both overloads are non-blocking on the calling thread —
    /// encoding happens on an internal FFmpeg thread.
    /// @param rgbaData    Pointer to RGBA pixel data; must remain valid until the call returns.
    /// @param strideBytes Byte width of one row (typically @c width * 4).
    /// @return @c false if encoding failed; check @c getLastError().
    bool submitVideoFrame(const std::uint8_t *rgbaData, int strideBytes);
    /// Move-in variant — takes ownership of the buffer, avoiding a copy. Prefer this
    /// with @c acquireFrameBuffer for zero-copy submission.
    bool submitVideoFrame(std::vector<std::uint8_t> &&frameData);
    /// Feed interleaved float PCM samples from an RtAudio callback into the audio encoder.
    /// Call this from the @c AudioInputCallback to keep A/V in sync.
    /// @param interleavedSamples  Interleaved float samples (left, right, left, …).
    /// @param frameCount          Number of sample frames (total samples / channels).
    /// @param streamTime          RtAudio stream timestamp in seconds.
    bool submitAudioInputSamples(const float *interleavedSamples, int frameCount, double streamTime);
    /// Acquire a pooled RGBA frame buffer of at least @p minCapacity bytes. Reusing the
    /// returned buffer across frames avoids repeated heap allocations.
    std::vector<std::uint8_t> acquireFrameBuffer(std::size_t minCapacity);
    /// @}

    /// @{
    /// @name Query
    [[nodiscard]] bool isRecording() const;
    /// Total number of video frames submitted since the last @c start call.
    [[nodiscard]] std::uint64_t getVideoFrameCount() const;
    /// Last error string set by FFmpeg on a failed @c start or @c submitVideoFrame.
    [[nodiscard]] const std::string &getLastError() const;
    /// @}

  private:
    bool m_isRecording = false;
    std::uint64_t m_videoFrameCount = 0;
    std::string m_lastError;

    struct FFmpegState;
    FFmpegState *m_state = nullptr;
};

} // namespace IRVideo

#endif /* IR_VIDEO_VIDEO_RECORDER_H */

#ifndef VIDEO_MANAGER_H
#define VIDEO_MANAGER_H

#include <irreden/video/auto_screenshot.hpp>
#include <irreden/video/video_recorder.hpp>

#include <cstdint>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace IRAudio {
class IAudioCaptureSource;
} // namespace IRAudio

namespace IRVideo {

class VideoManager {
  public:
    VideoManager();
    ~VideoManager();

    void configureCapture(
        const std::string &outputFilePath,
        int targetFps,
        int videoBitrate,
        bool captureAudioInput,
        const std::string &audioInputDeviceName,
        int audioSampleRate,
        int audioChannels,
        int audioBitrate = 320'000,
        bool audioMuxEnabled = true,
        bool audioWavEnabled = true,
        double audioSyncOffsetMs = 0.0,
        IRAudio::IAudioCaptureSource *audioCaptureSource = nullptr
    );
    void configureScreenshotOutputDir(const std::string &outputDirPath);

    void toggleRecording();
    void requestScreenshot();
    void requestScreenshotWithCrops(
        const char *shotLabel,
        const RoiCrop *crops,
        int numCrops
    );
    void requestCanvasScreenshot();
    void notifyFixedUpdate();
    void render();
    void shutdown();

    bool startRecording(const VideoRecorderConfig &config);
    void stopRecording();
    bool recordFrame(const std::uint8_t *rgbaData, int strideBytes);

    [[nodiscard]] bool isRecording() const;
    [[nodiscard]] std::uint64_t getFrameCount() const;
    [[nodiscard]] const std::string &getLastError() const;

  private:
    std::atomic<bool> m_captureEnabled = false;
    bool m_toggleRequested = false;
    int m_targetFps = 60;
    int m_videoBitrate = 10'000'000;
    bool m_captureAudioInput = false;
    std::string m_audioInputDeviceName;
    int m_audioSampleRate = 48'000;
    int m_audioChannels = 2;
    int m_audioBitrate = 320'000;
    bool m_audioMuxEnabled = true;
    bool m_audioWavEnabled = true;
    double m_audioSyncOffsetMs = 0.0;
    std::string m_outputFilePath = "capture.mp4";
    std::string m_screenshotOutputDirPath = "save_files/screenshots";
    std::uint64_t m_nextScreenshotIndex = 1;
    bool m_screenshotRequested = false;
    bool m_canvasScreenshotRequested = false;
    /// Per-screenshot ROI crop metadata. Labels live as @c std::string here
    /// so the caller's storage can die between request and capture.
    struct PendingCrop {
        int x_ = 0;
        int y_ = 0;
        int w_ = 0;
        int h_ = 0;
        std::string label_;
    };
    std::string m_pendingScreenshotShotLabel;
    std::vector<PendingCrop> m_pendingScreenshotCrops;
    int m_sourceFrameWidth = 0;
    int m_sourceFrameHeight = 0;
    int64_t m_totalFixedUpdates = 0;
    int64_t m_totalCapturedFrames = 0;
    std::size_t m_sourceFrameBytes = 0;
    int m_pboWriteIndex = 0;
    int m_pboPrimingFrames = 0;
    std::vector<unsigned int> m_readbackPbos;
    std::vector<std::uint8_t> m_rawFrameBuffer;
    std::atomic<bool> m_finalizeInProgress = false;
    std::thread m_finalizeThread;
    std::mutex m_recorderMutex;

    void armAudioInput();
    void toggleCapture();
    bool captureScreenshot();
    bool captureCanvasScreenshot();
    std::uint64_t reserveNextScreenshotIndex();
    std::string screenshotFilePathForIndex(std::uint64_t index) const;
    void writePendingRoiCrops(
        const std::uint8_t *frameData,
        int frameWidth,
        int frameHeight,
        std::uint64_t shotIndex
    );
    bool captureFrame();
    void initReadbackPbos();
    void releaseReadbackPbos();
    bool encodeCurrentRawFrame();
    void beginAsyncFinalize();
    void joinFinalizeThreadIfDone();

    IRAudio::IAudioCaptureSource *m_audioCaptureSource = nullptr;
    bool m_audioInputArmed = false;
    VideoRecorder m_videoRecorder;
};

} // namespace IRVideo

#endif /* VIDEO_MANAGER_H */

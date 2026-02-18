#ifndef VIDEO_MANAGER_H
#define VIDEO_MANAGER_H

#include <irreden/video/video_recorder.hpp>

#include <cstdint>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace IRVideo {

class VideoManager {
  public:
    VideoManager();
    ~VideoManager() = default;

    void configureCapture(const std::string &outputFilePath, int targetFps, int videoBitrate);
    void configureScreenshotOutputDir(const std::string &outputDirPath);

    void toggleRecording();
    void requestScreenshot();
    void render();
    void shutdown();

    bool startRecording(const VideoRecorderConfig &config);
    void stopRecording();
    bool recordFrame(const std::uint8_t *rgbaData, int strideBytes);

    [[nodiscard]] bool isRecording() const;
    [[nodiscard]] std::uint64_t getFrameCount() const;
    [[nodiscard]] const std::string &getLastError() const;

  private:
    static constexpr int kReadbackPboCount = 3;

    bool m_captureEnabled = false;
    bool m_toggleRequested = false;
    bool m_loggedResizeWarning = false;
    int m_targetFps = 60;
    int m_videoBitrate = 10'000'000;
    std::string m_outputFilePath = "capture.mp4";
    std::string m_screenshotOutputDirPath = "save_files/screenshots";
    std::uint64_t m_nextScreenshotIndex = 1;
    bool m_screenshotRequested = false;
    int m_sourceFrameWidth = 0;
    int m_sourceFrameHeight = 0;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    double m_captureAccumulatorSeconds = 0.0;
    std::size_t m_sourceFrameBytes = 0;
    int m_pboWriteIndex = 0;
    int m_pboPrimingFrames = 0;
    std::vector<unsigned int> m_readbackPbos;
    std::vector<std::uint8_t> m_rawFrameBuffer;
    std::vector<std::uint8_t> m_uploadFrameBuffer;
    std::atomic<bool> m_finalizeInProgress = false;
    std::thread m_finalizeThread;
    std::mutex m_recorderMutex;

    void toggleCapture();
    bool captureScreenshot();
    std::string getNextScreenshotFilePath();
    bool captureFrame();
    void initReadbackPbos();
    void releaseReadbackPbos();
    bool encodeCurrentRawFrame();
    void beginAsyncFinalize();
    void joinFinalizeThreadIfDone();

    VideoRecorder m_videoRecorder;
};

} // namespace IRVideo

#endif /* VIDEO_MANAGER_H */

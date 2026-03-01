#ifndef VIDEO_MANAGER_H
#define VIDEO_MANAGER_H

#include <irreden/video/video_recorder.hpp>

#include <cstdint>
#include <atomic>
#include <mutex>
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
    static constexpr int kReadbackPboCount = 3;

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
    std::string getNextScreenshotFilePath();
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

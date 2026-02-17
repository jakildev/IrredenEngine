#ifndef VIDEO_RECORDER_H
#define VIDEO_RECORDER_H

namespace IRVideo {

class VideoRecorder {
  public:
    VideoRecorder();
    ~VideoRecorder();

    void startRecording();
    void stopRecording();
    void recordFrame();
};

} // namespace IRVideo

#endif /* VIDEO_RECORDER_H */

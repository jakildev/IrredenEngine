#include <irreden/video/video_recorder.hpp>

#include <irreden/ir_profile.hpp>

#if IR_VIDEO_HAS_FFMPEG
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#endif

#if IR_VIDEO_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}
#endif

namespace IRVideo {

namespace {

std::string makeErrorString(int ffmpegErrorCode) {
#if IR_VIDEO_HAS_FFMPEG
    char errorBuffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_make_error_string(errorBuffer, sizeof(errorBuffer), ffmpegErrorCode);
    return std::string(errorBuffer);
#else
    (void)ffmpegErrorCode;
    return "FFmpeg disabled at compile time";
#endif
}

} // namespace

struct VideoRecorder::FFmpegState {
#if IR_VIDEO_HAS_FFMPEG
    AVFormatContext *formatContext = nullptr;
    AVCodecContext *videoCodecContext = nullptr;
    AVStream *videoStream = nullptr;
    SwsContext *scaleContext = nullptr;
    AVFrame *videoFrame = nullptr;
    AVPacket *packet = nullptr;
    int64_t nextVideoPts = 0;
    int rgbaStrideBytes = 0;
    int frameHeight = 0;

    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::deque<std::vector<std::uint8_t>> frameQueue;
    std::thread workerThread;
    bool stopRequested = false;
    bool encodeFailed = false;
    std::string workerError;
    std::size_t maxQueuedFrames = 8;
#endif
};

VideoRecorder::VideoRecorder() {
    IRE_LOG_INFO("Created VideoRecorder instance.");
}

VideoRecorder::~VideoRecorder() {
    stop();
}

bool VideoRecorder::start(const VideoRecorderConfig &config) {
    stop();
    m_lastError.clear();
    m_videoFrameCount = 0;

    if (config.width_ <= 0 || config.height_ <= 0 || config.target_fps_ <= 0) {
        m_lastError = "Invalid video recorder config. Width, height, and fps must be greater than zero.";
        IRE_LOG_ERROR("VideoRecorder start failed: {}", m_lastError.c_str());
        return false;
    }

#if !IR_VIDEO_HAS_FFMPEG
    m_lastError = "FFmpeg support is not enabled. Configure with -DIRREDEN_VIDEO_ENABLE_FFMPEG=ON "
                  "and point -DIRREDEN_FFMPEG_ROOT to your FFmpeg build if needed.";
    IRE_LOG_ERROR("VideoRecorder start failed: {}", m_lastError.c_str());
    return false;
#else
    std::unique_ptr<FFmpegState> state = std::make_unique<FFmpegState>();

    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (codec == nullptr) {
        m_lastError = "Could not find H264 encoder (AV_CODEC_ID_H264).";
        IRE_LOG_ERROR("VideoRecorder start failed: {}", m_lastError.c_str());
        return false;
    }

    int result = avformat_alloc_output_context2(&state->formatContext, nullptr, nullptr,
                                                config.output_file_path_.c_str());
    if (result < 0 || state->formatContext == nullptr) {
        m_lastError = "Could not allocate output context: " + makeErrorString(result);
        IRE_LOG_ERROR("VideoRecorder start failed: {}", m_lastError.c_str());
        return false;
    }

    state->videoStream = avformat_new_stream(state->formatContext, codec);
    if (state->videoStream == nullptr) {
        m_lastError = "Could not create output video stream.";
        IRE_LOG_ERROR("VideoRecorder start failed: {}", m_lastError.c_str());
        stop();
        return false;
    }

    state->videoCodecContext = avcodec_alloc_context3(codec);
    if (state->videoCodecContext == nullptr) {
        m_lastError = "Could not allocate video codec context.";
        IRE_LOG_ERROR("VideoRecorder start failed: {}", m_lastError.c_str());
        stop();
        return false;
    }

    state->videoCodecContext->codec_id = codec->id;
    state->videoCodecContext->codec_type = AVMEDIA_TYPE_VIDEO;
    state->videoCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    state->videoCodecContext->width = config.width_;
    state->videoCodecContext->height = config.height_;
    state->videoCodecContext->time_base = AVRational{1, config.target_fps_};
    state->videoCodecContext->framerate = AVRational{config.target_fps_, 1};
    state->videoCodecContext->bit_rate = std::max(config.video_bitrate_, 250000);
    state->videoCodecContext->gop_size = config.target_fps_ * 2;
    state->videoCodecContext->max_b_frames = 2;
    state->videoCodecContext->thread_count = 0;

    if ((state->formatContext->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
        state->videoCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    AVDictionary *codecOptions = nullptr;
    av_dict_set(&codecOptions, "preset", "ultrafast", 0);
    av_dict_set(&codecOptions, "tune", "zerolatency", 0);
    result = avcodec_open2(state->videoCodecContext, codec, &codecOptions);
    av_dict_free(&codecOptions);
    if (result < 0) {
        m_lastError = "Could not open video codec: " + makeErrorString(result);
        IRE_LOG_ERROR("VideoRecorder start failed: {}", m_lastError.c_str());
        stop();
        return false;
    }

    result = avcodec_parameters_from_context(state->videoStream->codecpar, state->videoCodecContext);
    if (result < 0) {
        m_lastError = "Could not copy stream parameters: " + makeErrorString(result);
        IRE_LOG_ERROR("VideoRecorder start failed: {}", m_lastError.c_str());
        stop();
        return false;
    }
    state->videoStream->time_base = state->videoCodecContext->time_base;

    if ((state->formatContext->oformat->flags & AVFMT_NOFILE) == 0) {
        result = avio_open(&state->formatContext->pb, config.output_file_path_.c_str(), AVIO_FLAG_WRITE);
        if (result < 0) {
            m_lastError = "Could not open output file: " + makeErrorString(result);
            IRE_LOG_ERROR("VideoRecorder start failed: {}", m_lastError.c_str());
            stop();
            return false;
        }
    }

    result = avformat_write_header(state->formatContext, nullptr);
    if (result < 0) {
        m_lastError = "Could not write stream header: " + makeErrorString(result);
        IRE_LOG_ERROR("VideoRecorder start failed: {}", m_lastError.c_str());
        stop();
        return false;
    }

    state->scaleContext = sws_getContext(config.width_, config.height_, AV_PIX_FMT_RGBA, config.width_,
                                         config.height_, state->videoCodecContext->pix_fmt, SWS_BILINEAR,
                                         nullptr, nullptr, nullptr);
    if (state->scaleContext == nullptr) {
        m_lastError = "Could not create RGBA->YUV420P conversion context.";
        IRE_LOG_ERROR("VideoRecorder start failed: {}", m_lastError.c_str());
        stop();
        return false;
    }

    state->videoFrame = av_frame_alloc();
    if (state->videoFrame == nullptr) {
        m_lastError = "Could not allocate video frame.";
        IRE_LOG_ERROR("VideoRecorder start failed: {}", m_lastError.c_str());
        stop();
        return false;
    }
    state->videoFrame->format = state->videoCodecContext->pix_fmt;
    state->videoFrame->width = config.width_;
    state->videoFrame->height = config.height_;
    state->rgbaStrideBytes = config.width_ * 4;
    state->frameHeight = config.height_;

    result = av_frame_get_buffer(state->videoFrame, 32);
    if (result < 0) {
        m_lastError = "Could not allocate video frame buffer: " + makeErrorString(result);
        IRE_LOG_ERROR("VideoRecorder start failed: {}", m_lastError.c_str());
        stop();
        return false;
    }

    state->packet = av_packet_alloc();
    if (state->packet == nullptr) {
        m_lastError = "Could not allocate output packet.";
        IRE_LOG_ERROR("VideoRecorder start failed: {}", m_lastError.c_str());
        stop();
        return false;
    }

    m_state = state.release();
    m_isRecording = true;
    m_state->workerThread = std::thread([this]() {
        FFmpegState *state = m_state;
        for (;;) {
            std::vector<std::uint8_t> frameBuffer;
            {
                std::unique_lock<std::mutex> lock(state->queueMutex);
                state->queueCv.wait(lock, [state]() {
                    return state->stopRequested || !state->frameQueue.empty();
                });

                if (state->frameQueue.empty()) {
                    if (state->stopRequested) {
                        break;
                    }
                    continue;
                }

                frameBuffer = std::move(state->frameQueue.front());
                state->frameQueue.pop_front();
            }

            int result = av_frame_make_writable(state->videoFrame);
            if (result < 0) {
                std::lock_guard<std::mutex> lock(state->queueMutex);
                state->encodeFailed = true;
                state->workerError = "Video frame not writable: " + makeErrorString(result);
                state->stopRequested = true;
                state->queueCv.notify_all();
                break;
            }

            const std::uint8_t *srcSlices[4] = {frameBuffer.data(), nullptr, nullptr, nullptr};
            const int srcStride[4] = {state->rgbaStrideBytes, 0, 0, 0};
            sws_scale(state->scaleContext, srcSlices, srcStride, 0, state->videoCodecContext->height,
                      state->videoFrame->data, state->videoFrame->linesize);

            state->videoFrame->pts = state->nextVideoPts++;
            result = avcodec_send_frame(state->videoCodecContext, state->videoFrame);
            if (result < 0) {
                std::lock_guard<std::mutex> lock(state->queueMutex);
                state->encodeFailed = true;
                state->workerError = "Failed to send frame to encoder: " + makeErrorString(result);
                state->stopRequested = true;
                state->queueCv.notify_all();
                break;
            }

            while (result >= 0) {
                result = avcodec_receive_packet(state->videoCodecContext, state->packet);
                if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                    break;
                }
                if (result < 0) {
                    std::lock_guard<std::mutex> lock(state->queueMutex);
                    state->encodeFailed = true;
                    state->workerError =
                        "Failed to receive encoded packet: " + makeErrorString(result);
                    state->stopRequested = true;
                    state->queueCv.notify_all();
                    break;
                }

                av_packet_rescale_ts(state->packet, state->videoCodecContext->time_base,
                                     state->videoStream->time_base);
                state->packet->stream_index = state->videoStream->index;
                result = av_interleaved_write_frame(state->formatContext, state->packet);
                av_packet_unref(state->packet);
                if (result < 0) {
                    std::lock_guard<std::mutex> lock(state->queueMutex);
                    state->encodeFailed = true;
                    state->workerError = "Failed to write packet: " + makeErrorString(result);
                    state->stopRequested = true;
                    state->queueCv.notify_all();
                    break;
                }
            }
            if (state->encodeFailed) {
                break;
            }
        }

        if (!state->encodeFailed) {
            int result = avcodec_send_frame(state->videoCodecContext, nullptr);
            if (result < 0) {
                std::lock_guard<std::mutex> lock(state->queueMutex);
                state->encodeFailed = true;
                state->workerError = "Failed to flush encoder: " + makeErrorString(result);
                return;
            }

            while (result >= 0) {
                result = avcodec_receive_packet(state->videoCodecContext, state->packet);
                if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                    break;
                }
                if (result < 0) {
                    std::lock_guard<std::mutex> lock(state->queueMutex);
                    state->encodeFailed = true;
                    state->workerError =
                        "Failed to receive flushed packet: " + makeErrorString(result);
                    break;
                }

                av_packet_rescale_ts(state->packet, state->videoCodecContext->time_base,
                                     state->videoStream->time_base);
                state->packet->stream_index = state->videoStream->index;
                result = av_interleaved_write_frame(state->formatContext, state->packet);
                av_packet_unref(state->packet);
                if (result < 0) {
                    std::lock_guard<std::mutex> lock(state->queueMutex);
                    state->encodeFailed = true;
                    state->workerError =
                        "Failed to write flushed packet: " + makeErrorString(result);
                    break;
                }
            }
        }
    });
    IRE_LOG_INFO("VideoRecorder started output to {} at {}x{} @ {} FPS",
                 config.output_file_path_.c_str(), config.width_, config.height_, config.target_fps_);
    return true;
#endif
}

void VideoRecorder::stop() {
#if IR_VIDEO_HAS_FFMPEG
    if (m_state != nullptr) {
        FFmpegState *state = m_state;

        {
            std::lock_guard<std::mutex> lock(state->queueMutex);
            state->stopRequested = true;
            state->queueCv.notify_all();
        }
        if (state->workerThread.joinable()) {
            state->workerThread.join();
        }

        {
            std::lock_guard<std::mutex> lock(state->queueMutex);
            if (state->encodeFailed) {
                m_lastError = state->workerError;
                IRE_LOG_ERROR("VideoRecorder async encode error: {}", m_lastError.c_str());
            }
        }

        if (state->formatContext != nullptr) {
            av_write_trailer(state->formatContext);
        }
        if (state->packet != nullptr) {
            av_packet_free(&state->packet);
        }
        if (state->videoFrame != nullptr) {
            av_frame_free(&state->videoFrame);
        }
        if (state->scaleContext != nullptr) {
            sws_freeContext(state->scaleContext);
        }
        if (state->videoCodecContext != nullptr) {
            avcodec_free_context(&state->videoCodecContext);
        }
        if (state->formatContext != nullptr) {
            if ((state->formatContext->oformat->flags & AVFMT_NOFILE) == 0 &&
                state->formatContext->pb != nullptr) {
                avio_closep(&state->formatContext->pb);
            }
            avformat_free_context(state->formatContext);
        }

        delete state;
        m_state = nullptr;
    }
#endif
    m_isRecording = false;
}

bool VideoRecorder::submitVideoFrame(const std::uint8_t *rgbaData, int strideBytes) {
    if (!m_isRecording) {
        m_lastError = "Recorder is not running.";
        return false;
    }

    if (rgbaData == nullptr || strideBytes <= 0) {
        m_lastError = "Invalid frame data. Null pointer or non-positive stride.";
        return false;
    }

#if !IR_VIDEO_HAS_FFMPEG
    m_lastError = "FFmpeg support is disabled.";
    return false;
#else
    FFmpegState *state = m_state;
    if (state == nullptr || state->videoCodecContext == nullptr || state->videoFrame == nullptr ||
        state->packet == nullptr) {
        m_lastError = "Recorder FFmpeg state is not initialized.";
        return false;
    }

    if (strideBytes != state->rgbaStrideBytes) {
        m_lastError = "Unexpected frame stride. Expected tightly packed RGBA.";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(state->queueMutex);
        if (state->encodeFailed) {
            m_lastError = state->workerError;
            return false;
        }
        if (state->stopRequested) {
            m_lastError = "Recorder is stopping and cannot accept new frames.";
            return false;
        }

        const std::size_t frameBytes =
            static_cast<std::size_t>(state->rgbaStrideBytes) * static_cast<std::size_t>(state->frameHeight);
        std::vector<std::uint8_t> frame(frameBytes);
        std::memcpy(frame.data(), rgbaData, frameBytes);

        if (state->frameQueue.size() >= state->maxQueuedFrames) {
            state->frameQueue.pop_front();
        }
        state->frameQueue.push_back(std::move(frame));
    }
    state->queueCv.notify_one();

    ++m_videoFrameCount;
    return true;
#endif
}

bool VideoRecorder::isRecording() const {
    return m_isRecording;
}

std::uint64_t VideoRecorder::getVideoFrameCount() const {
    return m_videoFrameCount;
}

const std::string &VideoRecorder::getLastError() const {
    return m_lastError;
}

} // namespace IRVideo
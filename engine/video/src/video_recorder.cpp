#include <irreden/video/video_recorder.hpp>

#include <irreden/ir_profile.hpp>

#if IR_VIDEO_HAS_FFMPEG
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
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
#include <libavutil/samplefmt.h>
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

std::string makeFallbackAudioPath(const std::string &videoPath) {
    const std::filesystem::path videoFilePath(videoPath);
    const std::filesystem::path stemPath = videoFilePath.stem();
    const std::filesystem::path parentPath = videoFilePath.parent_path();
    const std::string fallbackName = stemPath.string() + "_audio.wav";
    return (parentPath / fallbackName).string();
}

void writeFloat32WavFile(
    const std::string &outputPath,
    int sampleRate,
    int channels,
    const std::vector<float> &samples
) {
    std::ofstream output(outputPath, std::ios::binary);
    if (!output.is_open()) {
        IRE_LOG_WARN("Could not open audio output path '{}'.", outputPath.c_str());
        return;
    }

    const std::uint32_t dataChunkSize =
        static_cast<std::uint32_t>(samples.size() * sizeof(float));
    const std::uint32_t riffChunkSize = 36U + dataChunkSize;
    const std::uint16_t audioFormat = 3U; // IEEE float
    const std::uint16_t numChannels = static_cast<std::uint16_t>(channels);
    const std::uint32_t sampleRateU32 = static_cast<std::uint32_t>(sampleRate);
    const std::uint16_t bitsPerSample = 32U;
    const std::uint16_t blockAlign =
        static_cast<std::uint16_t>((numChannels * bitsPerSample) / 8U);
    const std::uint32_t byteRate = sampleRateU32 * static_cast<std::uint32_t>(blockAlign);

    output.write("RIFF", 4);
    output.write(reinterpret_cast<const char *>(&riffChunkSize), sizeof(riffChunkSize));
    output.write("WAVE", 4);
    output.write("fmt ", 4);
    const std::uint32_t formatChunkSize = 16U;
    output.write(reinterpret_cast<const char *>(&formatChunkSize), sizeof(formatChunkSize));
    output.write(reinterpret_cast<const char *>(&audioFormat), sizeof(audioFormat));
    output.write(reinterpret_cast<const char *>(&numChannels), sizeof(numChannels));
    output.write(reinterpret_cast<const char *>(&sampleRateU32), sizeof(sampleRateU32));
    output.write(reinterpret_cast<const char *>(&byteRate), sizeof(byteRate));
    output.write(reinterpret_cast<const char *>(&blockAlign), sizeof(blockAlign));
    output.write(reinterpret_cast<const char *>(&bitsPerSample), sizeof(bitsPerSample));
    output.write("data", 4);
    output.write(reinterpret_cast<const char *>(&dataChunkSize), sizeof(dataChunkSize));
    if (!samples.empty()) {
        output.write(reinterpret_cast<const char *>(samples.data()), dataChunkSize);
    }
}

[[maybe_unused]] AVSampleFormat chooseAudioSampleFormat(const AVCodec *codec) {
    if (codec == nullptr || codec->sample_fmts == nullptr) {
        return AV_SAMPLE_FMT_NONE;
    }
    AVSampleFormat fallback = AV_SAMPLE_FMT_NONE;
    for (const AVSampleFormat *fmt = codec->sample_fmts; *fmt != AV_SAMPLE_FMT_NONE; ++fmt) {
        if (*fmt == AV_SAMPLE_FMT_FLTP) {
            return *fmt;
        }
        if (*fmt == AV_SAMPLE_FMT_FLT) {
            fallback = *fmt;
        } else if (fallback == AV_SAMPLE_FMT_NONE) {
            fallback = *fmt;
        }
    }
    return fallback;
}

} // namespace

struct VideoRecorder::FFmpegState {
#if IR_VIDEO_HAS_FFMPEG
    struct AudioChunk {
        std::vector<float> interleavedSamples;
        int frameCount = 0;
        int64_t pts = 0;
    };

    AVFormatContext *formatContext = nullptr;
    AVCodecContext *videoCodecContext = nullptr;
    AVStream *videoStream = nullptr;
    AVCodecContext *audioCodecContext = nullptr;
    AVStream *audioStream = nullptr;
    SwsContext *scaleContext = nullptr;
    AVFrame *videoFrame = nullptr;
    AVFrame *audioFrame = nullptr;
    AVPacket *packet = nullptr;
    int64_t nextVideoPts = 0;
    int64_t nextAudioPts = 0;
    int sourceStrideBytes = 0;
    int sourceHeight = 0;
    int audioChannels = 2;
    int audioSampleRate = 48'000;
    double firstAudioStreamTimeSeconds = -1.0;
    bool audioCaptureRequested = false;
    bool audioMuxEnabled = false;
    std::string fallbackAudioPath;
    std::vector<float> fallbackAudioPcm;

    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::deque<std::vector<std::uint8_t>> frameQueue;
    std::deque<AudioChunk> audioQueue;
    std::vector<std::vector<std::uint8_t>> freeFramePool;
    std::thread workerThread;
    bool stopRequested = false;
    bool encodeFailed = false;
    std::string workerError;
    std::size_t maxQueuedFrames = 8;
    std::size_t maxQueuedAudioChunks = 128;
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
        m_lastError =
            "Invalid video recorder config. Width, height, and fps must be greater than zero.";
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
    state->audioCaptureRequested = config.capture_audio_input_;
    state->audioSampleRate = std::max(config.audio_sample_rate_, 8'000);
    state->audioChannels = std::clamp(config.audio_channels_, 1, 2);
    if (state->audioCaptureRequested) {
        state->fallbackAudioPath = makeFallbackAudioPath(config.output_file_path_);
        const std::size_t preAllocSamples =
            static_cast<std::size_t>(state->audioSampleRate) *
            static_cast<std::size_t>(state->audioChannels) * 120U;
        state->fallbackAudioPcm.reserve(preAllocSamples);
    }

    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (codec == nullptr) {
        m_lastError = "Could not find H264 encoder (AV_CODEC_ID_H264).";
        IRE_LOG_ERROR("VideoRecorder start failed: {}", m_lastError.c_str());
        return false;
    }

    int result = avformat_alloc_output_context2(
        &state->formatContext,
        nullptr,
        nullptr,
        config.output_file_path_.c_str()
    );
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

    result =
        avcodec_parameters_from_context(state->videoStream->codecpar, state->videoCodecContext);
    if (result < 0) {
        m_lastError = "Could not copy stream parameters: " + makeErrorString(result);
        IRE_LOG_ERROR("VideoRecorder start failed: {}", m_lastError.c_str());
        stop();
        return false;
    }
    state->videoStream->time_base = state->videoCodecContext->time_base;

    // Audio mux into the video container is disabled for now (phase 1:
    // lossless WAV verification).  The fallback WAV path handles capture.

    if ((state->formatContext->oformat->flags & AVFMT_NOFILE) == 0) {
        result =
            avio_open(&state->formatContext->pb, config.output_file_path_.c_str(), AVIO_FLAG_WRITE);
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

    const int srcW = (config.source_width_ > 0) ? config.source_width_ : config.width_;
    const int srcH = (config.source_height_ > 0) ? config.source_height_ : config.height_;
    state->scaleContext = sws_getContext(
        srcW,
        srcH,
        AV_PIX_FMT_RGBA,
        config.width_,
        config.height_,
        state->videoCodecContext->pix_fmt,
        SWS_POINT,
        nullptr,
        nullptr,
        nullptr
    );
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
    state->sourceStrideBytes = srcW * 4;
    state->sourceHeight = srcH;

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
        auto setWorkerFailure = [state](const std::string &errorMessage) {
            std::lock_guard<std::mutex> lock(state->queueMutex);
            state->encodeFailed = true;
            state->workerError = errorMessage;
            state->stopRequested = true;
            state->queueCv.notify_all();
        };

        auto writeEncodedPackets =
            [state, &setWorkerFailure](AVCodecContext *codecContext, AVStream *stream) -> bool {
            int result = 0;
            while (result >= 0) {
                result = avcodec_receive_packet(codecContext, state->packet);
                if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                    return true;
                }
                if (result < 0) {
                    setWorkerFailure("Failed to receive encoded packet: " + makeErrorString(result));
                    return false;
                }

                av_packet_rescale_ts(state->packet, codecContext->time_base, stream->time_base);
                state->packet->stream_index = stream->index;
                result = av_interleaved_write_frame(state->formatContext, state->packet);
                av_packet_unref(state->packet);
                if (result < 0) {
                    setWorkerFailure("Failed to write packet: " + makeErrorString(result));
                    return false;
                }
            }
            return true;
        };

        for (;;) {
            std::vector<std::uint8_t> frameBuffer;
            FFmpegState::AudioChunk audioChunk;
            bool processAudioChunk = false;
            {
                std::unique_lock<std::mutex> lock(state->queueMutex);
                state->queueCv.wait(lock, [state]() {
                    return state->stopRequested || !state->frameQueue.empty() ||
                           (state->audioMuxEnabled && !state->audioQueue.empty());
                });

                if (state->stopRequested && state->frameQueue.empty() &&
                    (!state->audioMuxEnabled || state->audioQueue.empty())) {
                    break;
                }

                if (state->audioMuxEnabled && !state->audioQueue.empty()) {
                    const int64_t nextVideoPtsInAudioSamples = av_rescale_q(
                        state->nextVideoPts,
                        state->videoCodecContext->time_base,
                        AVRational{1, state->audioSampleRate}
                    );
                    if (state->frameQueue.empty() ||
                        state->audioQueue.front().pts <= nextVideoPtsInAudioSamples) {
                        audioChunk = std::move(state->audioQueue.front());
                        state->audioQueue.pop_front();
                        processAudioChunk = true;
                    }
                }
                if (!processAudioChunk && !state->frameQueue.empty()) {
                    frameBuffer = std::move(state->frameQueue.front());
                    state->frameQueue.pop_front();
                }
            }

            if (processAudioChunk) {
                const int frameBlockSize = state->audioFrame->nb_samples;
                int consumedFrames = 0;
                while (consumedFrames < audioChunk.frameCount) {
                    int result = av_frame_make_writable(state->audioFrame);
                    if (result < 0) {
                        setWorkerFailure("Audio frame not writable: " + makeErrorString(result));
                        break;
                    }

                    const int chunkFrames = std::min(frameBlockSize, audioChunk.frameCount - consumedFrames);
                    state->audioFrame->pts = audioChunk.pts + consumedFrames;
                    const AVSampleFormat sampleFormat = state->audioCodecContext->sample_fmt;
                    const int channels = state->audioChannels;
                    const std::size_t srcBaseIndex =
                        static_cast<std::size_t>(consumedFrames) * static_cast<std::size_t>(channels);

                    if (sampleFormat == AV_SAMPLE_FMT_FLTP) {
                        for (int channel = 0; channel < channels; ++channel) {
                            float *dst = reinterpret_cast<float *>(state->audioFrame->data[channel]);
                            for (int i = 0; i < chunkFrames; ++i) {
                                const std::size_t srcIndex = srcBaseIndex +
                                    static_cast<std::size_t>(i) * static_cast<std::size_t>(channels) +
                                    static_cast<std::size_t>(channel);
                                dst[i] = audioChunk.interleavedSamples[srcIndex];
                            }
                            for (int i = chunkFrames; i < frameBlockSize; ++i) {
                                dst[i] = 0.0f;
                            }
                        }
                    } else if (sampleFormat == AV_SAMPLE_FMT_FLT) {
                        float *dst = reinterpret_cast<float *>(state->audioFrame->data[0]);
                        std::fill(dst, dst + frameBlockSize * channels, 0.0f);
                        const std::size_t copySamples =
                            static_cast<std::size_t>(chunkFrames) * static_cast<std::size_t>(channels);
                        std::copy_n(audioChunk.interleavedSamples.data() + srcBaseIndex, copySamples, dst);
                    } else if (sampleFormat == AV_SAMPLE_FMT_S16P) {
                        for (int channel = 0; channel < channels; ++channel) {
                            std::int16_t *dst =
                                reinterpret_cast<std::int16_t *>(state->audioFrame->data[channel]);
                            for (int i = 0; i < chunkFrames; ++i) {
                                const std::size_t srcIndex = srcBaseIndex +
                                    static_cast<std::size_t>(i) * static_cast<std::size_t>(channels) +
                                    static_cast<std::size_t>(channel);
                                const float clamped =
                                    std::clamp(audioChunk.interleavedSamples[srcIndex], -1.0f, 1.0f);
                                dst[i] = static_cast<std::int16_t>(std::lrint(clamped * 32767.0f));
                            }
                            for (int i = chunkFrames; i < frameBlockSize; ++i) {
                                dst[i] = 0;
                            }
                        }
                    } else if (sampleFormat == AV_SAMPLE_FMT_S16) {
                        std::int16_t *dst = reinterpret_cast<std::int16_t *>(state->audioFrame->data[0]);
                        std::fill(dst, dst + frameBlockSize * channels, 0);
                        const int totalSamples = chunkFrames * channels;
                        for (int i = 0; i < totalSamples; ++i) {
                            const float clamped =
                                std::clamp(audioChunk.interleavedSamples[srcBaseIndex + i], -1.0f, 1.0f);
                            dst[i] = static_cast<std::int16_t>(std::lrint(clamped * 32767.0f));
                        }
                    } else {
                        setWorkerFailure("Unsupported audio sample format for encoder.");
                        break;
                    }

                    result = avcodec_send_frame(state->audioCodecContext, state->audioFrame);
                    if (result < 0) {
                        setWorkerFailure("Failed to send audio frame to encoder: " +
                                         makeErrorString(result));
                        break;
                    }
                    if (!writeEncodedPackets(state->audioCodecContext, state->audioStream)) {
                        break;
                    }

                    consumedFrames += chunkFrames;
                }
                if (state->encodeFailed) {
                    break;
                }
                continue;
            }

            if (frameBuffer.empty()) {
                continue;
            }

            int result = av_frame_make_writable(state->videoFrame);
            if (result < 0) {
                setWorkerFailure("Video frame not writable: " + makeErrorString(result));
                break;
            }

            // Flip vertically during color conversion + resize by pointing
            // to the last row and using a negative stride. sws_scale handles
            // source→output resolution scaling, color conversion, and vertical
            // flip in a single SIMD-optimized pass (OpenGL PBO data is bottom-up).
            const int srcHeight = state->sourceHeight;
            const std::uint8_t *lastRow =
                frameBuffer.data() +
                static_cast<std::size_t>(srcHeight - 1) *
                    static_cast<std::size_t>(state->sourceStrideBytes);
            const std::uint8_t *srcSlices[4] = {lastRow, nullptr, nullptr, nullptr};
            const int srcStride[4] = {-state->sourceStrideBytes, 0, 0, 0};
            sws_scale(
                state->scaleContext,
                srcSlices,
                srcStride,
                0,
                srcHeight,
                state->videoFrame->data,
                state->videoFrame->linesize
            );

            state->videoFrame->pts = state->nextVideoPts++;
            result = avcodec_send_frame(state->videoCodecContext, state->videoFrame);
            if (result < 0) {
                setWorkerFailure("Failed to send frame to encoder: " + makeErrorString(result));
                break;
            }
            (void)writeEncodedPackets(state->videoCodecContext, state->videoStream);
            if (state->encodeFailed) {
                break;
            }

            // Return spent buffer to the pool so the main thread can reuse it
            // without allocating. clear() keeps the heap allocation intact.
            frameBuffer.clear();
            {
                std::lock_guard<std::mutex> lock(state->queueMutex);
                state->freeFramePool.push_back(std::move(frameBuffer));
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
            (void)writeEncodedPackets(state->videoCodecContext, state->videoStream);

            if (state->audioMuxEnabled && !state->encodeFailed) {
                result = avcodec_send_frame(state->audioCodecContext, nullptr);
                if (result < 0) {
                    std::lock_guard<std::mutex> lock(state->queueMutex);
                    state->encodeFailed = true;
                    state->workerError = "Failed to flush audio encoder: " + makeErrorString(result);
                    return;
                }
                (void)writeEncodedPackets(state->audioCodecContext, state->audioStream);
            }
        }
    });
    IRE_LOG_INFO(
        "VideoRecorder started output to {} at {}x{} @ {} FPS",
        config.output_file_path_.c_str(),
        config.width_,
        config.height_,
        config.target_fps_
    );
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
        if (state->audioCaptureRequested) {
            if (state->fallbackAudioPcm.empty()) {
                IRE_LOG_WARN("Audio capture was requested but no samples were recorded.");
            } else if (!state->audioMuxEnabled || state->encodeFailed) {
                const std::size_t totalSamples = state->fallbackAudioPcm.size();
                const double durationSeconds =
                    static_cast<double>(totalSamples) /
                    static_cast<double>(state->audioChannels) /
                    static_cast<double>(state->audioSampleRate);
                writeFloat32WavFile(
                    state->fallbackAudioPath,
                    state->audioSampleRate,
                    state->audioChannels,
                    state->fallbackAudioPcm
                );
                IRE_LOG_INFO(
                    "Wrote audio capture ({:.2f}s, {} samples): {}",
                    durationSeconds,
                    totalSamples,
                    state->fallbackAudioPath.c_str()
                );
            }
        }
        if (state->packet != nullptr) {
            av_packet_free(&state->packet);
        }
        if (state->audioFrame != nullptr) {
            av_frame_free(&state->audioFrame);
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
        if (state->audioCodecContext != nullptr) {
            avcodec_free_context(&state->audioCodecContext);
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
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_VIDEO);
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

    if (strideBytes != state->sourceStrideBytes) {
        m_lastError = "Unexpected frame stride. Expected tightly packed RGBA.";
        return false;
    }

    const std::size_t frameBytes = static_cast<std::size_t>(state->sourceStrideBytes) *
                                   static_cast<std::size_t>(state->sourceHeight);
    std::vector<std::uint8_t> frame(frameBytes);
    std::memcpy(frame.data(), rgbaData, frameBytes);

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

bool VideoRecorder::submitVideoFrame(std::vector<std::uint8_t> &&frameData) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_VIDEO);
    if (!m_isRecording) {
        m_lastError = "Recorder is not running.";
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

    const std::size_t expectedBytes = static_cast<std::size_t>(state->sourceStrideBytes) *
                                      static_cast<std::size_t>(state->sourceHeight);
    if (frameData.size() != expectedBytes) {
        m_lastError = "Frame data size mismatch.";
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

        if (state->frameQueue.size() >= state->maxQueuedFrames) {
            state->frameQueue.pop_front();
        }
        state->frameQueue.push_back(std::move(frameData));
    }
    state->queueCv.notify_one();

    ++m_videoFrameCount;
    return true;
#endif
}

std::vector<std::uint8_t> VideoRecorder::acquireFrameBuffer(std::size_t minCapacity) {
#if IR_VIDEO_HAS_FFMPEG
    FFmpegState *state = m_state;
    if (state != nullptr) {
        std::lock_guard<std::mutex> lock(state->queueMutex);
        if (!state->freeFramePool.empty()) {
            auto buf = std::move(state->freeFramePool.back());
            state->freeFramePool.pop_back();
            buf.resize(minCapacity);
            return buf;
        }
    }
#endif
    return std::vector<std::uint8_t>(minCapacity);
}

bool VideoRecorder::submitAudioInputSamples(
    const float *interleavedSamples,
    int frameCount,
    double streamTime
) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_AUDIO);
#if !IR_VIDEO_HAS_FFMPEG
    (void)interleavedSamples;
    (void)frameCount;
    (void)streamTime;
    return false;
#else
    if (!m_isRecording || interleavedSamples == nullptr || frameCount <= 0) {
        return false;
    }
    FFmpegState *state = m_state;
    if (state == nullptr || !state->audioCaptureRequested) {
        return false;
    }

    const std::size_t totalSamples =
        static_cast<std::size_t>(frameCount) * static_cast<std::size_t>(state->audioChannels);

    // Append to the WAV buffer outside the mutex. This is safe because only
    // the audio callback thread writes here and stop() reads only after the
    // audio device is closed (guaranteed by shutdown() ordering).
    state->fallbackAudioPcm.insert(
        state->fallbackAudioPcm.end(),
        interleavedSamples,
        interleavedSamples + totalSamples
    );

    if (!state->audioMuxEnabled) {
        return true;
    }

    std::lock_guard<std::mutex> lock(state->queueMutex);
    if (state->encodeFailed || state->stopRequested) {
        return false;
    }

    if (state->firstAudioStreamTimeSeconds < 0.0) {
        state->firstAudioStreamTimeSeconds = streamTime;
    }
    const double relativeTime = std::max(0.0, streamTime - state->firstAudioStreamTimeSeconds);
    int64_t chunkPts =
        static_cast<int64_t>(std::llround(relativeTime * static_cast<double>(state->audioSampleRate)));
    chunkPts = std::max(chunkPts, state->nextAudioPts);
    state->nextAudioPts = chunkPts + frameCount;

    FFmpegState::AudioChunk chunk;
    chunk.interleavedSamples.assign(interleavedSamples, interleavedSamples + totalSamples);
    chunk.frameCount = frameCount;
    chunk.pts = chunkPts;
    if (state->audioQueue.size() >= state->maxQueuedAudioChunks) {
        state->audioQueue.pop_front();
    }
    state->audioQueue.push_back(std::move(chunk));
    state->queueCv.notify_one();
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
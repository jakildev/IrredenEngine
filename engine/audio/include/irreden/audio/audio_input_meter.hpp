#ifndef AUDIO_INPUT_METER_H
#define AUDIO_INPUT_METER_H

#include <irreden/audio/ir_audio_types.hpp>

#include <irreden/ir_math.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

namespace IRAudio::detail {

// Per-channel peak, RMS, and clip count over consecutive, non-overlapping
// windows of kAudioInputMeterWindow, counted in delivered frames.
//
// accumulate() is the producer: one thread at a time, never blocks, logs, or
// allocates. reset() rewinds the producer side and must not overlap an
// accumulate(). read() and setActive() are safe from any thread; a read that
// overlaps a publish retries, and a publish is a handful of stores.
class AudioInputMeter {
  public:
    // `stride` is the interleaved channel count of the delivered samples; the
    // meter tracks the first kAudioInputMaxChannels of them. Publishes a
    // silent generation-0 snapshot.
    void reset(unsigned int stride, unsigned int sampleRate) {
        m_stride = stride;
        m_channels = IRMath::min(stride, kAudioInputMaxChannels);
        m_sampleRate = sampleRate;
        m_windowFrames = IRMath::max(
            static_cast<unsigned int>(
                static_cast<std::uint64_t>(sampleRate) * kAudioInputMeterWindow.count() / 1000
            ),
            1U
        );
        m_generation = 0;
        clearWindow();
        publish();
    }

    void accumulate(const float *samples, int frameCount) {
        if (m_channels == 0 || samples == nullptr) {
            return;
        }
        int frame = 0;
        while (frame < frameCount) {
            const int take = IRMath::min(
                frameCount - frame,
                static_cast<int>(m_windowFrames - m_framesInWindow)
            );
            for (int i = 0; i < take; ++i) {
                const float *frameSamples =
                    samples + static_cast<std::size_t>(frame + i) * m_stride;
                for (unsigned int channel = 0; channel < m_channels; ++channel) {
                    const float sample = frameSamples[channel];
                    const float magnitude = IRMath::abs(sample);
                    m_peak[channel] = IRMath::max(m_peak[channel], magnitude);
                    m_sumSquares[channel] += static_cast<double>(sample) * sample;
                    if (magnitude >= 1.0f) {
                        ++m_clipped[channel];
                    }
                }
            }
            frame += take;
            m_framesInWindow += static_cast<unsigned int>(take);
            if (m_framesInWindow == m_windowFrames) {
                ++m_generation;
                publish();
                clearWindow();
            }
        }
    }

    // Gates read(): an inactive meter reads as nothing armed. The owner
    // activates it once its stream is published and deactivates it before
    // the stream closes, so a late producer cannot make a closed input read
    // as live.
    void setActive(bool active) {
        m_active.store(active, std::memory_order_release);
    }

    [[nodiscard]] std::optional<AudioInputLevels> read() const {
        if (!m_active.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        AudioInputLevels levels;
        for (;;) {
            const std::uint64_t before = m_sequence.load(std::memory_order_acquire);
            if ((before & 1U) != 0) {
                continue;
            }
            for (unsigned int channel = 0; channel < kAudioInputMaxChannels; ++channel) {
                const PublishedChannel &published = m_published[channel];
                levels.channels_[channel].peakDbfs_ =
                    published.peakDbfs_.load(std::memory_order_relaxed);
                levels.channels_[channel].rmsDbfs_ =
                    published.rmsDbfs_.load(std::memory_order_relaxed);
                levels.channels_[channel].clippedSamples_ =
                    published.clippedSamples_.load(std::memory_order_relaxed);
            }
            levels.channelCount_ = m_publishedChannelCount.load(std::memory_order_relaxed);
            levels.sampleRate_ = m_publishedSampleRate.load(std::memory_order_relaxed);
            levels.generation_ = m_publishedGeneration.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (m_sequence.load(std::memory_order_relaxed) == before) {
                break;
            }
        }
        return levels;
    }

  private:
    struct PublishedChannel {
        std::atomic<float> peakDbfs_ = kAudioInputSilenceDbfs;
        std::atomic<float> rmsDbfs_ = kAudioInputSilenceDbfs;
        std::atomic<std::uint32_t> clippedSamples_ = 0;
    };

    // Producer side.
    unsigned int m_stride = 0;
    unsigned int m_channels = 0;
    unsigned int m_sampleRate = 0;
    unsigned int m_windowFrames = 1;
    unsigned int m_framesInWindow = 0;
    std::uint64_t m_generation = 0;
    std::array<float, kAudioInputMaxChannels> m_peak{};
    std::array<double, kAudioInputMaxChannels> m_sumSquares{};
    std::array<std::uint32_t, kAudioInputMaxChannels> m_clipped{};

    // Published side: a seqlock, odd while a publish is in progress.
    std::atomic<std::uint64_t> m_sequence = 0;
    std::array<PublishedChannel, kAudioInputMaxChannels> m_published;
    std::atomic<unsigned int> m_publishedChannelCount = 0;
    std::atomic<unsigned int> m_publishedSampleRate = 0;
    std::atomic<std::uint64_t> m_publishedGeneration = 0;
    std::atomic<bool> m_active = false;

    // `decibelsPerDecade` is 20 for an amplitude ratio, 10 for a power ratio
    // such as a mean square.
    static float toDbfs(double ratio, double decibelsPerDecade) {
        if (ratio <= 0.0) {
            return kAudioInputSilenceDbfs;
        }
        return IRMath::max(
            static_cast<float>(decibelsPerDecade * IRMath::log10(ratio)),
            kAudioInputSilenceDbfs
        );
    }

    void clearWindow() {
        m_framesInWindow = 0;
        m_peak.fill(0.0f);
        m_sumSquares.fill(0.0);
        m_clipped.fill(0);
    }

    void publish() {
        const std::uint64_t sequence = m_sequence.load(std::memory_order_relaxed);
        m_sequence.store(sequence + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        for (unsigned int channel = 0; channel < kAudioInputMaxChannels; ++channel) {
            const bool tracked = channel < m_channels;
            PublishedChannel &published = m_published[channel];
            published.peakDbfs_.store(
                tracked ? toDbfs(m_peak[channel], 20.0) : kAudioInputSilenceDbfs,
                std::memory_order_relaxed
            );
            published.rmsDbfs_.store(
                tracked ? toDbfs(m_sumSquares[channel] / m_windowFrames, 10.0)
                        : kAudioInputSilenceDbfs,
                std::memory_order_relaxed
            );
            published.clippedSamples_.store(
                tracked ? m_clipped[channel] : 0U,
                std::memory_order_relaxed
            );
        }
        m_publishedChannelCount.store(m_channels, std::memory_order_relaxed);
        m_publishedSampleRate.store(m_sampleRate, std::memory_order_relaxed);
        m_publishedGeneration.store(m_generation, std::memory_order_relaxed);
        m_sequence.store(sequence + 2, std::memory_order_release);
    }
};

} // namespace IRAudio::detail

#endif /* AUDIO_INPUT_METER_H */

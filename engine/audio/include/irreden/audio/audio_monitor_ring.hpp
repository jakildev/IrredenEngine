#ifndef AUDIO_MONITOR_RING_H
#define AUDIO_MONITOR_RING_H

#include <irreden/ir_profile.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace IRAudio::detail {

class AudioMonitorRing {
  public:
    AudioMonitorRing(
        std::size_t capacityFrames,
        unsigned int channels,
        std::size_t targetFillFrames,
        std::size_t highWaterFrames
    )
        : m_samples(capacityFrames * channels)
        , m_capacityFrames(capacityFrames)
        , m_mask(capacityFrames - 1)
        , m_channels(channels)
        , m_targetFillFrames(targetFillFrames)
        , m_highWaterFrames(highWaterFrames) {
        IR_ASSERT(
            capacityFrames >= 2 && (capacityFrames & (capacityFrames - 1)) == 0,
            "Audio monitor ring capacity must be a power of two"
        );
        IR_ASSERT(channels > 0, "Audio monitor ring requires at least one channel");
        IR_ASSERT(
            targetFillFrames < highWaterFrames && highWaterFrames < capacityFrames,
            "Audio monitor ring watermarks must be ordered within its capacity"
        );
    }

    AudioMonitorRing(const AudioMonitorRing &) = delete;
    AudioMonitorRing &operator=(const AudioMonitorRing &) = delete;

    bool push(const float *samples, std::size_t frameCount) {
        if (samples == nullptr || frameCount == 0 ||
            m_resetPending.load(std::memory_order_acquire)) {
            return false;
        }
        const std::uint64_t write = m_writeFrame.load(std::memory_order_relaxed);
        const std::uint64_t read = m_readFrame.load(std::memory_order_acquire);
        if (frameCount > m_capacityFrames - static_cast<std::size_t>(write - read)) {
            m_overrunFrames.fetch_add(frameCount, std::memory_order_relaxed);
            return false;
        }
        copyIn(write, samples, frameCount);
        m_writeFrame.store(write + frameCount, std::memory_order_release);
        return true;
    }

    void consume(float *samples, std::size_t frameCount) {
        if (samples == nullptr || frameCount == 0) {
            return;
        }
        applyPendingReset();
        std::uint64_t read = m_readFrame.load(std::memory_order_relaxed);
        const std::uint64_t write = m_writeFrame.load(std::memory_order_acquire);
        std::size_t depth = static_cast<std::size_t>(write - read);
        if (depth > m_highWaterFrames) {
            const std::size_t discarded = depth - m_targetFillFrames;
            read += discarded;
            depth -= discarded;
            m_discardedFrames.fetch_add(discarded, std::memory_order_relaxed);
        }
        if (m_prefilling && depth < m_targetFillFrames) {
            zero(samples, frameCount);
            return;
        }
        m_prefilling = false;
        const std::size_t copied = depth < frameCount ? depth : frameCount;
        copyOut(read, samples, copied);
        if (copied < frameCount) {
            zero(samples + copied * m_channels, frameCount - copied);
            m_underrunFrames.fetch_add(frameCount - copied, std::memory_order_relaxed);
        }
        m_readFrame.store(read + copied, std::memory_order_release);
        m_copiedFrames.fetch_add(copied, std::memory_order_relaxed);
    }

    void silence(float *samples, std::size_t frameCount) const {
        if (samples != nullptr) {
            zero(samples, frameCount);
        }
    }

    void requestReset() {
        m_resetPending.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool resetPending() const {
        return m_resetPending.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t targetFillFrames() const {
        return m_targetFillFrames;
    }

    [[nodiscard]] std::uint64_t copiedFrames() const {
        return m_copiedFrames.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t underrunFrames() const {
        return m_underrunFrames.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t overrunFrames() const {
        return m_overrunFrames.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t discardedFrames() const {
        return m_discardedFrames.load(std::memory_order_relaxed);
    }

  private:
    std::vector<float> m_samples;
    std::size_t m_capacityFrames;
    std::size_t m_mask;
    unsigned int m_channels;
    std::size_t m_targetFillFrames;
    std::size_t m_highWaterFrames;
    alignas(64) std::atomic<std::uint64_t> m_writeFrame = 0;
    alignas(64) std::atomic<std::uint64_t> m_readFrame = 0;
    std::atomic<bool> m_resetPending = false;
    bool m_prefilling = true;
    std::atomic<std::uint64_t> m_copiedFrames = 0;
    std::atomic<std::uint64_t> m_underrunFrames = 0;
    std::atomic<std::uint64_t> m_overrunFrames = 0;
    std::atomic<std::uint64_t> m_discardedFrames = 0;

    void applyPendingReset() {
        if (!m_resetPending.exchange(false, std::memory_order_acq_rel)) {
            return;
        }
        m_readFrame.store(m_writeFrame.load(std::memory_order_acquire), std::memory_order_release);
        m_prefilling = true;
    }

    void copyIn(std::uint64_t firstFrame, const float *samples, std::size_t frameCount) {
        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            const std::size_t destination =
                (static_cast<std::size_t>(firstFrame + frame) & m_mask) * m_channels;
            const std::size_t source = frame * m_channels;
            for (unsigned int channel = 0; channel < m_channels; ++channel) {
                m_samples[destination + channel] = samples[source + channel];
            }
        }
    }

    void copyOut(std::uint64_t firstFrame, float *samples, std::size_t frameCount) const {
        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            const std::size_t source =
                (static_cast<std::size_t>(firstFrame + frame) & m_mask) * m_channels;
            const std::size_t destination = frame * m_channels;
            for (unsigned int channel = 0; channel < m_channels; ++channel) {
                samples[destination + channel] = m_samples[source + channel];
            }
        }
    }

    void zero(float *samples, std::size_t frameCount) const {
        for (std::size_t sample = 0; sample < frameCount * m_channels; ++sample) {
            samples[sample] = 0.0f;
        }
    }
};

} // namespace IRAudio::detail

#endif /* AUDIO_MONITOR_RING_H */

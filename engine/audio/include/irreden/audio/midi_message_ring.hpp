#ifndef MIDI_MESSAGE_RING_H
#define MIDI_MESSAGE_RING_H

#include <irreden/audio/components/component_midi_message.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace IRAudio {

inline constexpr std::size_t kMidiMessageRingCapacity = 1024;
static_assert((kMidiMessageRingCapacity & (kMidiMessageRingCapacity - 1)) == 0);
static_assert(std::atomic<std::size_t>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

class MidiMessageRing {
  public:
    MidiMessageRing() = default;
    MidiMessageRing(const MidiMessageRing &) = delete;
    MidiMessageRing &operator=(const MidiMessageRing &) = delete;
    MidiMessageRing(MidiMessageRing &&) = delete;
    MidiMessageRing &operator=(MidiMessageRing &&) = delete;

    bool tryPush(const IRComponents::C_MidiMessage &message) {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        const std::size_t head = m_head.load(std::memory_order_acquire);
        if (tail - head == kMidiMessageRingCapacity) {
            return false;
        }

        m_messages[tail & (kMidiMessageRingCapacity - 1)] = message;
        m_tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    void push(const IRComponents::C_MidiMessage &message) {
        if (!tryPush(message)) {
            m_droppedCount.fetch_add(1, std::memory_order_relaxed);
        }
    }

    template <typename Visitor> void drain(Visitor &&visitor) {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        const std::size_t tail = m_tail.load(std::memory_order_acquire);
        for (std::size_t index = head; index != tail; ++index) {
            visitor(m_messages[index & (kMidiMessageRingCapacity - 1)]);
        }
        m_head.store(tail, std::memory_order_release);
    }

    std::uint32_t takeDroppedCount() {
        return m_droppedCount.exchange(0, std::memory_order_relaxed);
    }

  private:
    std::array<IRComponents::C_MidiMessage, kMidiMessageRingCapacity> m_messages{};
    alignas(64) std::atomic<std::size_t> m_head{0};
    alignas(64) std::atomic<std::size_t> m_tail{0};
    std::atomic<std::uint32_t> m_droppedCount{0};
};

} // namespace IRAudio

#endif /* MIDI_MESSAGE_RING_H */

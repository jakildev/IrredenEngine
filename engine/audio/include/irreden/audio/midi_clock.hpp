#ifndef MIDI_CLOCK_H
#define MIDI_CLOCK_H

#include <irreden/audio/ir_audio_types.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace IRAudio {

// MIDI beat clock on one output port: START, then TIMING_CLOCK at
// kMidiClockPulsesPerQuarterNote from its own thread, then STOP. The send
// callback is injected so the pacing and the transport order are testable
// without a device; AudioManager supplies one that writes through MidiOut.
//
// Threading contract: START and STOP leave on the thread that calls start()
// / stop(), so a send callback may fan them out to the outbound observer
// (Lua). TIMING_CLOCK leaves on the clock thread, where the callback must
// touch neither the observer nor Lua. The callback is also called from both
// threads at once with the main thread's own note/CC sends, so the port
// writer behind it serialises (MidiOut holds one mutex over its sends).
class MidiClock {
  public:
    using RealtimeSend = std::function<void(int portIndex, unsigned char status)>;

    explicit MidiClock(RealtimeSend send);
    ~MidiClock();
    MidiClock(const MidiClock &) = delete;
    MidiClock &operator=(const MidiClock &) = delete;

    // Starts on portIndex at bpm. Already running on that port: retunes and
    // returns true with no transport message. Running on another port: stops
    // there (STOP) first. A bpm outside [kMidiClockMinBpm, kMidiClockMaxBpm]
    // or not finite is refused: returns false, nothing sent, state unchanged.
    bool start(int portIndex, double bpm);
    // Retunes a running clock; the next pulse already paces at the new rate.
    // Returns false (unchanged) when stopped or the bpm is out of range.
    bool setBpm(double bpm);
    // Ends the pulses (joins the thread) and sends STOP. No-op when stopped.
    void stop();

    bool isRunning() const;
    int portIndex() const;
    double bpm() const;
    // TIMING_CLOCK pulses sent since the last start().
    std::uint64_t pulsesSent() const;

  private:
    void pulseLoop();

    RealtimeSend m_send;
    std::atomic<bool> m_running = false;
    std::atomic<int> m_portIndex = -1;
    std::atomic<double> m_bpm = 0.0;
    std::atomic<std::uint64_t> m_pulsesSent = 0;
    std::condition_variable m_wakeCondition;
    std::mutex m_wakeMutex;
    std::thread m_pulseThread;
};

} // namespace IRAudio

#endif /* MIDI_CLOCK_H */

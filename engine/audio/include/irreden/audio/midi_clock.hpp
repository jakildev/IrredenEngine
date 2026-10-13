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
// kMidiClockPulsesPerQuarterNote from its own thread, then STOP. Both
// callbacks are injected so the pacing and the transport order are testable
// without a device; AudioManager supplies a MidiOut writer and its outbound
// observer.
//
// Threading contract: START and STOP leave on the thread that calls start()
// / stop(), to observeTransport first and then to write — the order every
// other outbound message takes through IRAudio::sendMidiMessage. TIMING_CLOCK
// leaves on the clock thread through write alone, so the observer (which may
// run Lua) never runs there. write is therefore called from both threads, at
// once with the main thread's own note/CC sends, and the port writer behind
// it serialises (MidiOut holds one mutex over its sends).
class MidiClock {
  public:
    using RealtimeSend = std::function<void(int portIndex, unsigned char status)>;

    explicit MidiClock(RealtimeSend write, RealtimeSend observeTransport = {});
    ~MidiClock();
    MidiClock(const MidiClock &) = delete;
    MidiClock &operator=(const MidiClock &) = delete;

    // Starts on portIndex at bpm. Already running on that port: retunes and
    // returns true with no transport message. Running on another port: stops
    // there (STOP) first. A bpm outside [kMidiClockMinBpm, kMidiClockMaxBpm]
    // or not finite is refused: returns false, nothing sent, state unchanged.
    bool start(int portIndex, double bpm);
    // Retunes a running clock. The pending pulse is re-timed from the last one
    // sent, so a faster tempo does not wait out the old interval (a pulse
    // already overdue at the new rate leaves at once) and a slower one
    // stretches it. Returns false (unchanged) when stopped or the bpm is out
    // of range.
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
    void retune(double bpm);
    void sendTransport(int portIndex, unsigned char status);

    RealtimeSend m_write;
    RealtimeSend m_observeTransport;
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

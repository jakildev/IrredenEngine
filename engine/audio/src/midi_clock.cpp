#include <irreden/audio/midi_clock.hpp>

#include <irreden/ir_profile.hpp>

#include <chrono>
#include <cmath>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#include <timeapi.h>
#endif

namespace IRAudio {

namespace {

// The wait ends this early and the thread spins the rest: a timed wait
// overshoots by its scheduler quantum, which at 120 BPM would be most of a
// pulse.
constexpr auto kSpinTail = std::chrono::milliseconds(1);

// Windows times a wait on a 15.6 ms quantum unless the process asks for a
// finer one; held for the life of the clock thread only.
struct ScopedTimerResolution {
#ifdef _WIN32
    static constexpr UINT kMilliseconds = 1;
    ScopedTimerResolution() {
        timeBeginPeriod(kMilliseconds);
    }
    ~ScopedTimerResolution() {
        timeEndPeriod(kMilliseconds);
    }
#endif
};

bool isClockBpm(double bpm) {
    return std::isfinite(bpm) && bpm >= kMidiClockMinBpm && bpm <= kMidiClockMaxBpm;
}

} // namespace

MidiClock::MidiClock(RealtimeSend write, RealtimeSend observeTransport)
    : m_write(std::move(write))
    , m_observeTransport(std::move(observeTransport)) {}

MidiClock::~MidiClock() {
    stop();
}

bool MidiClock::start(int portIndex, double bpm) {
    if (!isClockBpm(bpm)) {
        IRE_LOG_WARN(
            "MidiClock: bpm {} is outside {}..{}; not started",
            bpm,
            kMidiClockMinBpm,
            kMidiClockMaxBpm
        );
        return false;
    }
    if (m_running.load()) {
        if (m_portIndex.load() == portIndex) {
            retune(bpm);
            return true;
        }
        stop();
    }
    m_portIndex.store(portIndex);
    m_bpm.store(bpm);
    m_pulsesSent.store(0);
    sendTransport(portIndex, kMidiStatus_START);
    m_running.store(true);
    m_pulseThread = std::thread(&MidiClock::pulseLoop, this);
    IRE_LOG_INFO("MidiClock: started on MIDI Out port {} at {} BPM", portIndex, bpm);
    return true;
}

bool MidiClock::setBpm(double bpm) {
    if (!m_running.load() || !isClockBpm(bpm)) {
        return false;
    }
    retune(bpm);
    return true;
}

void MidiClock::stop() {
    if (!m_running.load()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_wakeMutex);
        m_running.store(false);
    }
    m_wakeCondition.notify_all();
    if (m_pulseThread.joinable()) {
        m_pulseThread.join();
    }
    sendTransport(m_portIndex.load(), kMidiStatus_STOP);
    IRE_LOG_INFO(
        "MidiClock: stopped on MIDI Out port {} after {} pulse(s)",
        m_portIndex.load(),
        m_pulsesSent.load()
    );
}

bool MidiClock::isRunning() const {
    return m_running.load();
}

int MidiClock::portIndex() const {
    return m_portIndex.load();
}

double MidiClock::bpm() const {
    return m_bpm.load();
}

std::uint64_t MidiClock::pulsesSent() const {
    return m_pulsesSent.load();
}

// Stored under the wake mutex so the clock thread cannot read the old tempo
// and then sleep through the wake.
void MidiClock::retune(double bpm) {
    {
        std::lock_guard<std::mutex> lock(m_wakeMutex);
        m_bpm.store(bpm);
    }
    m_wakeCondition.notify_all();
}

void MidiClock::sendTransport(int portIndex, unsigned char status) {
    if (m_observeTransport) {
        m_observeTransport(portIndex, status);
    }
    m_write(portIndex, status);
}

// Paces on accumulated deadlines so scheduler jitter never shortens the
// average period; a stall of a whole pulse or more resumes from now rather
// than bursting the missed pulses out back to back. Each deadline is the last
// pulse's plus one period at the current tempo, and a retune wakes the wait to
// derive it again, so the pending pulse moves with the tempo.
void MidiClock::pulseLoop() {
    using Clock = std::chrono::steady_clock;
    [[maybe_unused]] const ScopedTimerResolution resolution;
    Clock::time_point last = Clock::now();
    while (m_running.load()) {
        m_write(m_portIndex.load(), kMidiStatus_TIMING_CLOCK);
        m_pulsesSent.fetch_add(1);
        for (;;) {
            const double bpm = m_bpm.load();
            const auto stoppedOrRetuned = [this, bpm]() {
                return !m_running.load() || m_bpm.load() != bpm;
            };
            const auto period = std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(midiClockPulseSeconds(bpm))
            );
            Clock::time_point next = last + period;
            const Clock::time_point now = Clock::now();
            if (next + period < now) {
                next = now;
            }
            {
                std::unique_lock<std::mutex> lock(m_wakeMutex);
                m_wakeCondition.wait_until(lock, next - kSpinTail, stoppedOrRetuned);
            }
            while (!stoppedOrRetuned() && Clock::now() < next) {
                std::this_thread::yield();
            }
            if (!m_running.load()) {
                return;
            }
            if (!stoppedOrRetuned()) {
                last = next;
                break;
            }
        }
    }
}

} // namespace IRAudio

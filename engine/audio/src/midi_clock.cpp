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

MidiClock::MidiClock(RealtimeSend send)
    : m_send(std::move(send)) {}

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
            m_bpm.store(bpm);
            return true;
        }
        stop();
    }
    m_portIndex.store(portIndex);
    m_bpm.store(bpm);
    m_pulsesSent.store(0);
    m_send(portIndex, kMidiStatus_START);
    m_running.store(true);
    m_pulseThread = std::thread(&MidiClock::pulseLoop, this);
    IRE_LOG_INFO("MidiClock: started on MIDI Out port {} at {} BPM", portIndex, bpm);
    return true;
}

bool MidiClock::setBpm(double bpm) {
    if (!m_running.load() || !isClockBpm(bpm)) {
        return false;
    }
    m_bpm.store(bpm);
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
    m_send(m_portIndex.load(), kMidiStatus_STOP);
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

// Paces on accumulated deadlines so scheduler jitter never shortens the
// average period; a stall of a whole pulse or more resumes from now rather
// than bursting the missed pulses out back to back.
void MidiClock::pulseLoop() {
    using Clock = std::chrono::steady_clock;
    [[maybe_unused]] const ScopedTimerResolution resolution;
    Clock::time_point next = Clock::now();
    while (m_running.load()) {
        m_send(m_portIndex.load(), kMidiStatus_TIMING_CLOCK);
        m_pulsesSent.fetch_add(1);
        const auto period = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(midiClockPulseSeconds(m_bpm.load()))
        );
        next += period;
        const Clock::time_point now = Clock::now();
        if (next + period < now) {
            next = now;
        }
        {
            std::unique_lock<std::mutex> lock(m_wakeMutex);
            if (m_wakeCondition.wait_until(lock, next - kSpinTail, [this]() {
                    return !m_running.load();
                })) {
                break;
            }
        }
        while (m_running.load() && Clock::now() < next) {
            std::this_thread::yield();
        }
    }
}

} // namespace IRAudio

#include <gtest/gtest.h>

#include "../common/wait_for.hpp"

#include <irreden/audio/ir_audio_types.hpp>
#include <irreden/audio/midi_clock.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

// MidiClock transport order and pacing without a device: the injected
// callbacks record every (port, status) they are handed.

namespace {

using namespace IRAudio;
using IRTest::waitFor;

struct SendRecorder {
    std::mutex mutex;
    std::vector<std::pair<int, unsigned char>> sent;

    MidiClock::RealtimeSend callback() {
        return [this](int portIndex, unsigned char status) {
            std::lock_guard<std::mutex> lock(mutex);
            sent.emplace_back(portIndex, status);
        };
    }

    std::vector<std::pair<int, unsigned char>> snapshot() {
        std::lock_guard<std::mutex> lock(mutex);
        return sent;
    }

    std::size_t count(unsigned char status) {
        std::size_t n = 0;
        for (const auto &entry : snapshot()) {
            if (entry.second == status) {
                ++n;
            }
        }
        return n;
    }
};

using SteadyClock = std::chrono::steady_clock;

// Stamps each TIMING_CLOCK as the port callback receives it.
struct PulseTimeline {
    std::mutex mutex;
    std::vector<SteadyClock::time_point> pulsedAt;

    MidiClock::RealtimeSend callback() {
        return [this](int, unsigned char status) {
            if (status != kMidiStatus_TIMING_CLOCK) {
                return;
            }
            std::lock_guard<std::mutex> lock(mutex);
            pulsedAt.push_back(SteadyClock::now());
        };
    }

    std::vector<SteadyClock::time_point> snapshot() {
        std::lock_guard<std::mutex> lock(mutex);
        return pulsedAt;
    }

    // The pulse-to-pulse interval `at` falls in; empty until a pulse has
    // been stamped on each side of it.
    std::optional<SteadyClock::duration> intervalAcross(SteadyClock::time_point at) {
        const auto pulses = snapshot();
        for (std::size_t i = 1; i < pulses.size(); ++i) {
            if (pulses[i - 1] <= at && pulses[i] > at) {
                return pulses[i] - pulses[i - 1];
            }
        }
        return std::nullopt;
    }
};

SteadyClock::duration pulsePeriod(double bpm) {
    return std::chrono::duration_cast<SteadyClock::duration>(
        std::chrono::duration<double>(midiClockPulseSeconds(bpm))
    );
}

constexpr int kPort = 2;
constexpr int kOtherPort = 5;
constexpr double kBpm = 120.0;
// Fast enough that a short run still sees many pulses (4.2 ms apart).
constexpr double kFastBpm = 600.0;
constexpr auto kRunFor = std::chrono::milliseconds(250);
constexpr auto kSettle = std::chrono::milliseconds(50);

double expectedPulses(double bpm, std::chrono::milliseconds runFor) {
    const double seconds = static_cast<double>(runFor.count()) / 1000.0;
    return seconds / midiClockPulseSeconds(bpm);
}

TEST(MidiClockTest, StartSendsStartThenPacedPulsesThenStopOnThePort) {
    SendRecorder recorder;
    MidiClock clock(recorder.callback());

    ASSERT_TRUE(clock.start(kPort, kFastBpm));
    EXPECT_TRUE(clock.isRunning());
    EXPECT_EQ(clock.portIndex(), kPort);
    EXPECT_DOUBLE_EQ(clock.bpm(), kFastBpm);

    std::this_thread::sleep_for(kRunFor);
    clock.stop();
    EXPECT_FALSE(clock.isRunning());

    const auto sent = recorder.snapshot();
    ASSERT_GE(sent.size(), 3u);
    EXPECT_EQ(sent.front().second, kMidiStatus_START);
    EXPECT_EQ(sent.back().second, kMidiStatus_STOP);
    for (const auto &entry : sent) {
        EXPECT_EQ(entry.first, kPort);
    }
    const std::size_t pulses = recorder.count(kMidiStatus_TIMING_CLOCK);
    EXPECT_EQ(pulses, sent.size() - 2u); // nothing but START, pulses, STOP
    EXPECT_EQ(pulses, clock.pulsesSent());
    // Scheduler slack on a loaded box: a wide band, but never a burst and
    // never a stall.
    const double expected = expectedPulses(kFastBpm, kRunFor);
    EXPECT_GE(static_cast<double>(pulses), expected * 0.4);
    EXPECT_LE(static_cast<double>(pulses), expected * 1.6);
}

TEST(MidiClockTest, StopEndsThePulsesAndRepeatsAsNoOp) {
    SendRecorder recorder;
    MidiClock clock(recorder.callback());
    ASSERT_TRUE(clock.start(kPort, kFastBpm));
    std::this_thread::sleep_for(kSettle);
    clock.stop();

    const std::size_t sentAtStop = recorder.snapshot().size();
    std::this_thread::sleep_for(kSettle);
    clock.stop();
    EXPECT_EQ(recorder.snapshot().size(), sentAtStop);
    EXPECT_EQ(recorder.count(kMidiStatus_STOP), 1u);
}

TEST(MidiClockTest, RefusesABpmOutsideTheRange) {
    SendRecorder recorder;
    MidiClock clock(recorder.callback());

    EXPECT_FALSE(clock.start(kPort, 0.0));
    EXPECT_FALSE(clock.start(kPort, kMidiClockMinBpm - 1.0));
    EXPECT_FALSE(clock.start(kPort, kMidiClockMaxBpm + 1.0));
    EXPECT_FALSE(clock.start(kPort, std::nan("")));
    EXPECT_FALSE(clock.isRunning());
    EXPECT_TRUE(recorder.snapshot().empty());

    // Stopped: setBpm has nothing to retune.
    EXPECT_FALSE(clock.setBpm(kBpm));

    ASSERT_TRUE(clock.start(kPort, kBpm));
    EXPECT_FALSE(clock.setBpm(-1.0));
    EXPECT_DOUBLE_EQ(clock.bpm(), kBpm);
    EXPECT_TRUE(clock.setBpm(kFastBpm));
    EXPECT_DOUBLE_EQ(clock.bpm(), kFastBpm);
    clock.stop();
}

TEST(MidiClockTest, StartOnTheRunningPortRetunesWithoutTransport) {
    SendRecorder recorder;
    MidiClock clock(recorder.callback());
    ASSERT_TRUE(clock.start(kPort, kBpm));
    ASSERT_TRUE(clock.start(kPort, kFastBpm));
    EXPECT_DOUBLE_EQ(clock.bpm(), kFastBpm);
    EXPECT_TRUE(clock.isRunning());
    clock.stop();

    EXPECT_EQ(recorder.count(kMidiStatus_START), 1u);
    EXPECT_EQ(recorder.count(kMidiStatus_STOP), 1u);
}

// The pulse-to-pulse interval a retune from startBpm to retuneBpm lands in;
// empty when the clock never pulsed on both sides of it.
std::optional<SteadyClock::duration> intervalAcrossRetune(double startBpm, double retuneBpm) {
    PulseTimeline timeline;
    MidiClock clock(timeline.callback());
    if (!clock.start(kPort, startBpm) || !waitFor([&]() { return clock.pulsesSent() >= 1; }) ||
        !clock.setBpm(retuneBpm)) {
        return std::nullopt;
    }
    const SteadyClock::time_point retunedAt = SteadyClock::now();
    waitFor([&]() { return timeline.intervalAcross(retunedAt).has_value(); });
    clock.stop();
    return timeline.intervalAcross(retunedAt);
}

// The interval a retune lands in is re-timed from its last pulse, in both
// directions; a clock that only reads the tempo at each pulse would run that
// interval out at the old rate.
TEST(MidiClockTest, RetuneFasterCutsThePendingIntervalShort) {
    // 125 ms apart, then 4.2 ms.
    const auto interval = intervalAcrossRetune(kMidiClockMinBpm, kFastBpm);
    ASSERT_TRUE(interval.has_value());
    EXPECT_LT(*interval, pulsePeriod(kMidiClockMinBpm) / 2);
}

TEST(MidiClockTest, RetuneSlowerStretchesThePendingInterval) {
    // 20.8 ms apart, then 125 ms.
    const auto interval = intervalAcrossRetune(kBpm, kMidiClockMinBpm);
    ASSERT_TRUE(interval.has_value());
    // A lower bound: scheduler slack only lengthens it.
    EXPECT_GE(*interval, pulsePeriod(kMidiClockMinBpm) * 4 / 5);
}

// START and STOP reach the transport observer and then the port, both on the
// caller's thread; the pulses reach the port alone, from the clock thread.
TEST(MidiClockTest, TransportReachesTheObserverBeforeThePortAndPulsesNeverDo) {
    enum class Via { OBSERVER, PORT };
    struct Call {
        Via via;
        int portIndex;
        unsigned char status;
        std::thread::id thread;
        bool operator==(const Call &) const = default;
    };
    std::mutex mutex;
    std::vector<Call> calls;
    const auto record = [&](Via via) {
        return [&, via](int portIndex, unsigned char status) {
            std::lock_guard<std::mutex> lock(mutex);
            calls.push_back(Call{via, portIndex, status, std::this_thread::get_id()});
        };
    };

    {
        MidiClock clock(record(Via::PORT), record(Via::OBSERVER));
        ASSERT_TRUE(clock.start(kPort, kFastBpm));
        ASSERT_TRUE(waitFor([&]() { return clock.pulsesSent() >= 3; }));
        clock.stop();
    }

    const std::thread::id caller = std::this_thread::get_id();
    ASSERT_GE(calls.size(), 7u);
    EXPECT_EQ(calls[0], (Call{Via::OBSERVER, kPort, kMidiStatus_START, caller}));
    EXPECT_EQ(calls[1], (Call{Via::PORT, kPort, kMidiStatus_START, caller}));
    for (std::size_t i = 2; i + 2 < calls.size(); ++i) {
        EXPECT_EQ(calls[i].via, Via::PORT);
        EXPECT_EQ(calls[i].status, kMidiStatus_TIMING_CLOCK);
        EXPECT_NE(calls[i].thread, caller);
    }
    EXPECT_EQ(calls[calls.size() - 2], (Call{Via::OBSERVER, kPort, kMidiStatus_STOP, caller}));
    EXPECT_EQ(calls[calls.size() - 1], (Call{Via::PORT, kPort, kMidiStatus_STOP, caller}));
}

TEST(MidiClockTest, StartOnAnotherPortStopsTheFirst) {
    SendRecorder recorder;
    MidiClock clock(recorder.callback());
    ASSERT_TRUE(clock.start(kPort, kBpm));
    ASSERT_TRUE(clock.start(kOtherPort, kBpm));
    EXPECT_EQ(clock.portIndex(), kOtherPort);
    clock.stop();

    std::vector<std::pair<int, unsigned char>> transport;
    for (const auto &entry : recorder.snapshot()) {
        if (entry.second != kMidiStatus_TIMING_CLOCK) {
            transport.push_back(entry);
        }
    }
    const std::vector<std::pair<int, unsigned char>> expected{
        {kPort, kMidiStatus_START},
        {kPort, kMidiStatus_STOP},
        {kOtherPort, kMidiStatus_START},
        {kOtherPort, kMidiStatus_STOP},
    };
    EXPECT_EQ(transport, expected);
}

TEST(MidiClockTest, DestructorStopsARunningClock) {
    SendRecorder recorder;
    {
        MidiClock clock(recorder.callback());
        ASSERT_TRUE(clock.start(kPort, kFastBpm));
        std::this_thread::sleep_for(kSettle);
    }
    const auto sent = recorder.snapshot();
    ASSERT_FALSE(sent.empty());
    EXPECT_EQ(sent.back().second, kMidiStatus_STOP);
    const std::size_t sentAtDestroy = sent.size();
    std::this_thread::sleep_for(kSettle);
    EXPECT_EQ(recorder.snapshot().size(), sentAtDestroy);
}

} // namespace

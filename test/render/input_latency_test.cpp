// The input latency probe pairs a frame loop record (last poll, RENDER's
// cursor sample) with the render device's report of that frame's end. A
// scripted clock stands in for steady_clock so the tests can name every time
// and count every read.

#include <gtest/gtest.h>

#include <irreden/render/input_latency.hpp>
#include <irreden/render/render_device.hpp>

#include <cstdint>
#include <vector>

namespace {

using IRRender::FrameCompletion;
using IRRender::FrameCompletionQueue;
using IRRender::FrameEndEvent;
using IRRender::InputLatencyAccumulator;

struct ScriptedClock {
    std::vector<double> times_;
    std::size_t reads_ = 0;

    double operator()() {
        const double t = reads_ < times_.size() ? times_[reads_] : 0.0;
        ++reads_;
        return t;
    }
};

auto read(ScriptedClock &clock) {
    return [&clock]() { return clock(); };
}

FrameCompletion
completion(std::uint64_t frameId, double submit, double end, double observed, bool valid = true) {
    FrameCompletion c;
    c.frameId_ = frameId;
    c.submitSeconds_ = submit;
    c.endSeconds_ = end;
    c.observedSeconds_ = observed;
    c.valid_ = valid;
    return c;
}

TEST(InputLatencyAccumulator, DisabledTakesNoTimestampAndRecordsNothing) {
    InputLatencyAccumulator latency;
    latency.reset(false, 16);
    ScriptedClock clock{{1.0, 2.0}};
    latency.recordPoll(read(clock));
    latency.beginFrame(3, read(clock));
    latency.recordRender(1, read(clock));
    latency.addCompletion(completion(1, 1.0, 1.1, 1.1));
    EXPECT_EQ(clock.reads_, 0u);
    EXPECT_TRUE(latency.frames().empty());
    EXPECT_EQ(
        latency.takeSummary(FrameEndEvent::GPU_COMPLETE).endEvent_,
        FrameEndEvent::NOT_MEASURED
    );
}

TEST(InputLatencyAccumulator, PairsTheEndWithItsFrame) {
    InputLatencyAccumulator latency;
    latency.reset(true, 16);
    ScriptedClock clock{{0.995, 1.000, 1.004}};
    latency.beginFrame(0, read(clock));
    latency.recordPoll(read(clock));
    latency.recordRender(7, read(clock));
    FrameCompletion end = completion(7, 1.010, 1.020, 1.0201);
    end.mappingErrorSeconds_ = 0.000002;
    latency.addCompletion(end);

    ASSERT_EQ(latency.frames().size(), 1u);
    const auto &frame = latency.frames()[0];
    EXPECT_TRUE(frame.ended_);
    EXPECT_NEAR(frame.inputToEndMs_, 20.0f, 1e-3f);
    EXPECT_NEAR(frame.cursorToEndMs_, 16.0f, 1e-3f);
    EXPECT_NEAR(frame.submitToEndMs_, 10.0f, 1e-3f);
    EXPECT_NEAR(frame.startToSubmitMs_, 15.0f, 1e-3f);
    EXPECT_NEAR(frame.endObservedLagMs_, 0.1f, 1e-3f);
    EXPECT_EQ(frame.framesInFlight_, 0u);

    const auto summary = latency.takeSummary(FrameEndEvent::GPU_COMPLETE);
    EXPECT_EQ(summary.endEvent_, FrameEndEvent::GPU_COMPLETE);
    EXPECT_EQ(summary.frames_.size(), 1u);
    EXPECT_NEAR(summary.clockMappingErrorMs_, 0.002, 1e-9);
}

TEST(InputLatencyAccumulator, AFrameWithNoPollOfItsOwnKeepsTheEarlierPoll) {
    InputLatencyAccumulator latency;
    latency.reset(true, 16);
    ScriptedClock clock{{1.000, 1.002, 1.019}};
    latency.recordPoll(read(clock));
    latency.recordRender(1, read(clock));
    latency.addCompletion(completion(1, 1.005, 1.010, 1.010));
    latency.recordRender(2, read(clock));
    latency.addCompletion(completion(2, 1.022, 1.030, 1.030));

    ASSERT_EQ(latency.frames().size(), 2u);
    EXPECT_NEAR(latency.frames()[1].inputToEndMs_, 30.0f, 1e-3f);
    EXPECT_NEAR(latency.frames()[1].cursorToEndMs_, 11.0f, 1e-3f);
}

TEST(InputLatencyAccumulator, AFrameBeforeAnyPollHasNoReading) {
    InputLatencyAccumulator latency;
    latency.reset(true, 16);
    ScriptedClock clock{{1.000}};
    latency.recordRender(1, read(clock));
    latency.addCompletion(completion(1, 1.001, 1.002, 1.002));
    ASSERT_EQ(latency.frames().size(), 1u);
    EXPECT_FALSE(latency.frames()[0].ended_);
}

TEST(InputLatencyAccumulator, EndsArrivingFramesLaterLandOnTheirOwnFrames) {
    InputLatencyAccumulator latency;
    latency.reset(true, 16);
    ScriptedClock clock{{0.0, 1.000, 1.001, 0.0, 1.010, 1.011, 0.0, 1.020, 1.021}};
    for (std::uint64_t id = 1; id <= 3; ++id) {
        latency.beginFrame(static_cast<std::uint32_t>(id - 1), read(clock));
        latency.recordPoll(read(clock));
        latency.recordRender(id, read(clock));
    }
    latency.addCompletion(completion(1, 1.002, 1.030, 1.030));
    latency.addCompletion(completion(2, 1.012, 1.040, 1.040));

    ASSERT_EQ(latency.frames().size(), 3u);
    EXPECT_NEAR(latency.frames()[0].inputToEndMs_, 30.0f, 1e-3f);
    EXPECT_NEAR(latency.frames()[1].inputToEndMs_, 30.0f, 1e-3f);
    EXPECT_FALSE(latency.frames()[2].ended_);
    EXPECT_EQ(latency.frames()[2].framesInFlight_, 2u);
}

TEST(InputLatencyAccumulator, ASkippedEndGivesUpTheOlderFrame) {
    InputLatencyAccumulator latency;
    latency.reset(true, 16);
    ScriptedClock clock{{1.000, 1.001, 1.002}};
    latency.recordPoll(read(clock));
    latency.recordRender(1, read(clock));
    latency.recordRender(2, read(clock));
    latency.addCompletion(completion(2, 1.003, 1.010, 1.010));
    latency.addCompletion(completion(1, 1.003, 1.010, 1.010));

    EXPECT_FALSE(latency.frames()[0].ended_);
    EXPECT_TRUE(latency.frames()[1].ended_);
}

TEST(InputLatencyAccumulator, AnInvalidEndClosesTheFrameWithoutAReading) {
    InputLatencyAccumulator latency;
    latency.reset(true, 16);
    ScriptedClock clock{{1.000, 1.001}};
    latency.recordPoll(read(clock));
    latency.recordRender(1, read(clock));
    latency.addCompletion(completion(1, 1.002, 0.0, 1.003, false));
    latency.addCompletion(completion(1, 1.002, 1.004, 1.004));
    EXPECT_FALSE(latency.frames()[0].ended_);
}

TEST(InputLatencyFrameCompletionQueue, DisabledOrUnmeasuredReadsNoClock) {
    FrameCompletionQueue queue;
    ScriptedClock clock{{1.0}};
    queue.submitted(read(clock));
    queue.reset(true, FrameEndEvent::NOT_MEASURED);
    queue.submitted(read(clock));
    queue.reset(false, FrameEndEvent::GPU_COMPLETE);
    queue.submitted(read(clock));
    queue.finished(1.0, 1.0, 0.0, true);
    FrameCompletion out;
    EXPECT_FALSE(queue.poll(out));
    EXPECT_EQ(queue.inFlight(), 0u);
    EXPECT_EQ(clock.reads_, 0u);
}

TEST(InputLatencyFrameCompletionQueue, SubmissionsFinishInOrderUnderTheirFrameIds) {
    FrameCompletionQueue queue;
    queue.reset(true, FrameEndEvent::GPU_COMPLETE);
    EXPECT_EQ(queue.endEvent(), FrameEndEvent::GPU_COMPLETE);
    ScriptedClock clock{{1.0, 2.0}};
    queue.setFrameId(10);
    queue.submitted(read(clock));
    queue.setFrameId(11);
    queue.submitted(read(clock));
    EXPECT_EQ(queue.inFlight(), 2u);

    queue.finished(1.5, 1.6, 0.001, true);
    EXPECT_EQ(queue.inFlight(), 1u);
    FrameCompletion out;
    ASSERT_TRUE(queue.poll(out));
    EXPECT_EQ(out.frameId_, 10u);
    EXPECT_DOUBLE_EQ(out.submitSeconds_, 1.0);
    EXPECT_DOUBLE_EQ(out.endSeconds_, 1.5);
    EXPECT_DOUBLE_EQ(out.observedSeconds_, 1.6);
    EXPECT_DOUBLE_EQ(out.mappingErrorSeconds_, 0.001);
    EXPECT_TRUE(out.valid_);
    EXPECT_FALSE(queue.poll(out));

    queue.finished(2.5, 2.5, 0.0, false);
    ASSERT_TRUE(queue.poll(out));
    EXPECT_EQ(out.frameId_, 11u);
    EXPECT_FALSE(out.valid_);
    EXPECT_EQ(queue.inFlight(), 0u);
}

TEST(InputLatencyFrameCompletionQueue, AFinishWithNothingInFlightIsIgnored) {
    FrameCompletionQueue queue;
    queue.reset(true, FrameEndEvent::GPU_COMPLETE);
    queue.finished(1.0, 1.0, 0.0, true);
    FrameCompletion out;
    EXPECT_FALSE(queue.poll(out));
}

// Every pure virtual stubbed; the probe surface is the base class's own.
class StubDevice : public IRRender::RenderDevice {
  public:
    void beginFrame() override {}
    void present() override {}
    void dispatchCompute(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void dispatchComputeIndirect(const IRRender::Buffer *, std::ptrdiff_t) override {}
    void memoryBarrier(IRRender::BarrierType) override {}
    void unbindBufferBase(IRRender::BufferTarget, std::uint32_t) override {}
    void drawElements(IRRender::DrawMode, int, IRRender::IndexType) override {}
    void drawElementsInstanced(IRRender::DrawMode, int, IRRender::IndexType, int) override {}
    void drawElementsInstancedIndirect(
        IRRender::DrawMode, IRRender::IndexType, const IRRender::Buffer *, std::ptrdiff_t
    ) override {}
    void drawArrays(IRRender::DrawMode, int, int) override {}
    void drawArraysInstanced(IRRender::DrawMode, int, int, int) override {}
    void copyImageSubData(
        std::uint32_t, int, int, int, int, std::uint32_t, int, int, int, int, int, int, int
    ) override {}
    void copyTexture2D(const IRRender::Texture2D &, IRRender::Texture2D &) override {}
    void setPolygonMode(IRRender::PolygonMode) override {}
    void bindDefaultFramebuffer() override {}
    void clearDefaultFramebuffer() override {}
    bool readDefaultFramebuffer(int, int, int, int, void *) override {
        return false;
    }
    void enableBlending() override {}
    void disableBlending() override {}
    void setDepthTest(bool) override {}
    void setDepthWrite(bool) override {}
    void clearTexImage(const IRRender::Texture2D *, int, const void *) override {}
    void fillBuffer(const IRRender::Buffer *, std::size_t, std::size_t, std::uint8_t) override {}
    void copyBufferSubData(
        const IRRender::Buffer *,
        std::ptrdiff_t,
        const IRRender::Buffer *,
        std::ptrdiff_t,
        std::size_t
    ) override {}
    std::uint64_t submissionSerial() const override {
        return 0;
    }
    std::uint64_t completedSubmissionSerial() override {
        return 0;
    }
    void finish() override {}
};

// Times frame ends the way the Metal device does: a blocking present that
// stamps the submission, then reports the GPU end.
class BlockingPresentDevice final : public StubDevice {
  public:
    void setGpuFrameTimingEnabled(bool enabled) override {
        m_frameCompletions.reset(enabled, FrameEndEvent::GPU_COMPLETE);
    }
    void present() override {
        m_frameCompletions.submitted([this]() { return now_; });
        m_frameCompletions.finished(now_ + gpuMs_ / 1000.0, now_ + gpuMs_ / 1000.0, 0.0, true);
    }
    double now_ = 0.0;
    double gpuMs_ = 0.0;
};

TEST(InputLatencyRenderDevice, ADeviceThatTimesNoFrameEndReportsNotMeasured) {
    StubDevice device;
    device.setGpuFrameTimingEnabled(true);
    device.setPresentingFrame(1);
    device.present();
    EXPECT_EQ(device.frameEndEvent(), FrameEndEvent::NOT_MEASURED);
    EXPECT_EQ(device.framesInFlight(), 0u);
    FrameCompletion out;
    EXPECT_FALSE(device.pollFrameCompletion(out));
}

TEST(InputLatencyRenderDevice, ABlockingPresentLeavesNoFrameInFlight) {
    BlockingPresentDevice device;
    device.setGpuFrameTimingEnabled(true);
    InputLatencyAccumulator latency;
    latency.reset(device.frameEndEvent() != FrameEndEvent::NOT_MEASURED, 16);
    ScriptedClock clock{{0.999, 1.000, 1.002, 1.019, 1.020, 1.021}};
    for (std::uint64_t id = 1; id <= 2; ++id) {
        latency.beginFrame(device.framesInFlight(), read(clock));
        latency.recordPoll(read(clock));
        latency.recordRender(id, read(clock));
        device.setPresentingFrame(id);
        device.now_ = 1.005 + 0.020 * static_cast<double>(id - 1);
        device.gpuMs_ = 4.0;
        device.present();
        FrameCompletion completion;
        while (device.pollFrameCompletion(completion)) {
            latency.addCompletion(completion);
        }
    }
    ASSERT_EQ(latency.frames().size(), 2u);
    for (const auto &frame : latency.frames()) {
        EXPECT_TRUE(frame.ended_);
        EXPECT_EQ(frame.framesInFlight_, 0u);
        EXPECT_NEAR(frame.submitToEndMs_, 4.0f, 1e-3f);
    }
    EXPECT_NEAR(latency.frames()[0].inputToEndMs_, 9.0f, 1e-3f);
    EXPECT_NEAR(latency.frames()[1].inputToEndMs_, 9.0f, 1e-3f);
}

} // namespace

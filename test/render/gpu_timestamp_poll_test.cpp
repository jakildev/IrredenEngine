#include <gtest/gtest.h>
#include <irreden/render/gpu_stage_timing_observer.hpp>
#include <irreden/render/gpu_substage_timing.hpp>

namespace IRRender {
extern RenderDevice *g_renderDevice;
}

namespace {
using namespace IRRender;

class TimestampDevice : public RenderDevice {
  public:
    void beginFrame() override {}
    void present() override {}
    void dispatchCompute(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void dispatchComputeIndirect(const Buffer *, std::ptrdiff_t) override {}
    void memoryBarrier(BarrierType) override {}
    void drawElements(DrawMode, int, IndexType) override {}
    void drawElementsInstanced(DrawMode, int, IndexType, int) override {}
    void
    drawElementsInstancedIndirect(DrawMode, IndexType, const Buffer *, std::ptrdiff_t) override {}
    void drawArrays(DrawMode, int, int) override {}
    void drawArraysInstanced(DrawMode, int, int, int) override {}
    void copyImageSubData(
        std::uint32_t, int, int, int, int, std::uint32_t, int, int, int, int, int, int, int
    ) override {}
    void setPolygonMode(PolygonMode) override {}
    void bindDefaultFramebuffer() override {}
    void clearDefaultFramebuffer() override {}
    bool readDefaultFramebuffer(int, int, int, int, void *) override {
        return false;
    }
    void enableBlending() override {}
    void disableBlending() override {}
    void setDepthTest(bool) override {}
    void setDepthWrite(bool) override {}
    void clearTexImage(const Texture2D *, int, const void *) override {}
    void fillBuffer(const Buffer *, std::size_t, std::uint8_t) override {}
    void finish() override {}
    bool supportsGpuTimestampPairs() const override {
        return true;
    }
    int recommendedTimestampPairsInFlight() const override {
        return 1;
    }
    GpuTimestampHandle createTimestampPair() override {
        return 1;
    }
    void writeTimestamp(GpuTimestampHandle, TimestampSlot slot) override {
        if (slot == TimestampSlot::START)
            ++starts_;
    }
    bool readTimestampPairMs(GpuTimestampHandle, float &ms) override {
        ms = 2.5f;
        return status_ == TimestampReadStatus::READY;
    }
    TimestampReadStatus pollTimestampPairMs(GpuTimestampHandle, float &ms) override {
        ms = 2.5f;
        return status_;
    }
    TimestampReadStatus status_ = TimestampReadStatus::PENDING;
    int starts_ = 0;
};

class GpuTimestampPollTest : public testing::Test {
  protected:
    void SetUp() override {
        previous_ = g_renderDevice;
        setDevice(&device_);
        saved_ = gpuStageTiming();
        gpuStageTiming().enabled_ = true;
        gpuStageTiming().legacyFinishTiming_ = false;
        resetGpuStageAccumulators();
    }
    void TearDown() override {
        setDevice(previous_);
        gpuStageTiming() = saved_;
        resetGpuStageAccumulators();
    }
    TimestampDevice device_;
    RenderDevice *previous_ = nullptr;
    GpuStageTiming saved_;
};

TEST_F(GpuTimestampPollTest, SubstageReusesInvalidSlotWithoutRecordingZero) {
    GpuSubStageTimer timer;
    auto *state = timer.acquire(gpuStageRegistry()[0].name_);
    ASSERT_NE(state, nullptr);
    timer.begin(*state);
    timer.end(*state);
    timer.begin(*state);
    timer.end(*state);
    EXPECT_EQ(device_.starts_, 1);
    device_.status_ = TimestampReadStatus::INVALID;
    timer.begin(*state);
    timer.end(*state);
    EXPECT_EQ(device_.starts_, 2);
    EXPECT_EQ(gpuStageAccumulators()[0].sampleCount_, 0u);
    device_.status_ = TimestampReadStatus::READY;
    timer.begin(*state);
    timer.end(*state);
    EXPECT_EQ(device_.starts_, 3);
    EXPECT_EQ(gpuStageAccumulators()[0].sampleCount_, 1u);
    EXPECT_DOUBLE_EQ(gpuStageAccumulators()[0].sumMs_, 2.5);
}

TEST_F(GpuTimestampPollTest, ObserverReusesInvalidSlotWithoutRecordingZero) {
    GpuStageTimingObserver observer;
    observer.tagStage(0, gpuStageRegistry()[0]);
    observer.onBeforeTick(0);
    observer.onAfterTick(0);
    observer.onBeforeTick(0);
    observer.onAfterTick(0);
    EXPECT_EQ(device_.starts_, 1);
    device_.status_ = TimestampReadStatus::INVALID;
    observer.onBeforeTick(0);
    observer.onAfterTick(0);
    EXPECT_EQ(device_.starts_, 2);
    EXPECT_EQ(gpuStageAccumulators()[0].sampleCount_, 0u);
    device_.status_ = TimestampReadStatus::READY;
    observer.onBeforeTick(0);
    observer.onAfterTick(0);
    EXPECT_EQ(device_.starts_, 3);
    EXPECT_EQ(gpuStageAccumulators()[0].sampleCount_, 1u);
    EXPECT_DOUBLE_EQ(gpuStageAccumulators()[0].sumMs_, 2.5);
}

TEST_F(GpuTimestampPollTest, ExistingBooleanBackendAdaptsToPolling) {
    float ms = -1.0f;
    EXPECT_EQ(device_.RenderDevice::pollTimestampPairMs(1, ms), TimestampReadStatus::PENDING);
    device_.status_ = TimestampReadStatus::READY;
    EXPECT_EQ(device_.RenderDevice::pollTimestampPairMs(1, ms), TimestampReadStatus::READY);
    EXPECT_FLOAT_EQ(ms, 2.5f);
}
} // namespace

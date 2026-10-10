#include <gtest/gtest.h>
#include <irreden/render/gpu_stage_timing_observer.hpp>
#include <irreden/render/gpu_substage_timing.hpp>

#include <optional>
#include <unordered_map>
#include <vector>

#if defined(IR_GRAPHICS_OPENGL)
#include <irreden/render/opengl/opengl_types.hpp>
#endif

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
    void finish() override {
        ++finishes_;
    }
    bool supportsGpuTimestampPairs() const override {
        return supported_;
    }
    int recommendedTimestampPairsInFlight() const override {
        return recommended_;
    }
    GpuTimestampHandle createTimestampPair() override {
        ++allocations_;
        return allocations_ <= allocationLimit_ ? allocations_ : kInvalidGpuTimestampHandle;
    }
    void destroyTimestampPair(GpuTimestampHandle handle) override {
        destroyed_.push_back(handle);
    }
    void writeTimestamp(GpuTimestampHandle handle, TimestampSlot slot) override {
        if (slot == TimestampSlot::START) {
            ++starts_;
            startHandles_.push_back(handle);
        } else {
            endHandles_.push_back(handle);
        }
    }
    bool readTimestampPairMs(GpuTimestampHandle, float &ms) override {
        ms = 2.5f;
        return status_ == TimestampReadStatus::READY;
    }
    TimestampReadStatus pollTimestampPairMs(GpuTimestampHandle handle, float &ms) override {
        ms = 2.5f;
        const auto found = statuses_.find(handle);
        return found == statuses_.end() ? status_ : found->second;
    }
    TimestampReadStatus status_ = TimestampReadStatus::PENDING;
    std::unordered_map<GpuTimestampHandle, TimestampReadStatus> statuses_;
    std::vector<GpuTimestampHandle> startHandles_;
    std::vector<GpuTimestampHandle> endHandles_;
    std::vector<GpuTimestampHandle> destroyed_;
    GpuTimestampHandle allocations_ = 0;
    GpuTimestampHandle allocationLimit_ = 3;
    int recommended_ = 1;
    bool supported_ = true;
    int starts_ = 0;
    int finishes_ = 0;
};

TEST(GpuTimestampRingTest, BusySlotsSurviveUntilTheirOwnResultsComplete) {
    TimestampDevice device;
    device.recommended_ = 3;
    IRRender::detail::GpuTimestampRing ring;
    ring.initialize(&device);
    std::vector<float> samples;
    auto cycle = [&] {
        ring.begin(device, [&](float ms) { samples.push_back(ms); });
        ring.end(device);
    };
    for (int i = 0; i < 4; ++i) {
        cycle();
    }
    EXPECT_EQ(device.startHandles_, (std::vector<GpuTimestampHandle>{1, 2, 3}));
    EXPECT_EQ(device.endHandles_, device.startHandles_);
    EXPECT_TRUE(samples.empty());
    device.statuses_[2] = TimestampReadStatus::READY;
    cycle();
    EXPECT_EQ(device.startHandles_.back(), 2u);
    EXPECT_EQ(samples, (std::vector<float>{2.5f}));
    device.statuses_[2] = TimestampReadStatus::PENDING;
    device.statuses_[1] = TimestampReadStatus::INVALID;
    cycle();
    EXPECT_EQ(device.startHandles_.back(), 1u);
    EXPECT_EQ(samples.size(), 1u);
    EXPECT_EQ(device.finishes_, 0);
}

TEST(GpuTimestampRingTest, AllocationFailuresStayInertAndAreNotDestroyed) {
    for (GpuTimestampHandle limit : {0u, 1u, 2u}) {
        TimestampDevice device;
        device.recommended_ = 3;
        device.allocationLimit_ = limit;
        IRRender::detail::GpuTimestampRing ring;
        ring.initialize(&device);
        for (int i = 0; i < 4; ++i) {
            ring.begin(device, [](float) { FAIL() << "Pending samples cannot be recorded"; });
            ring.end(device);
        }
        EXPECT_EQ(device.starts_, static_cast<int>(limit));
        EXPECT_EQ(device.startHandles_, device.endHandles_);
        ring.release(&device);
        EXPECT_EQ(device.destroyed_, device.startHandles_);
    }
}

TEST(GpuTimestampRingTest, DeviceRecommendationIsClampedAndUnsupportedIsInert) {
    for (int recommended : {0, 1, 2, 3, 10}) {
        TimestampDevice device;
        device.recommended_ = recommended;
        IRRender::detail::GpuTimestampRing ring;
        ring.initialize(&device);
        EXPECT_EQ(device.allocations_, static_cast<unsigned>(IRMath::clamp(recommended, 1, 3)));
        ring.release(&device);
        EXPECT_EQ(device.destroyed_.size(), device.allocations_);
    }
    TimestampDevice device;
    device.supported_ = false;
    IRRender::detail::GpuTimestampRing ring;
    ring.initialize(nullptr);
    ring.initialize(&device);
    ring.begin(device, [](float) { FAIL() << "Unsupported devices cannot record samples"; });
    ring.end(device);
    ring.release(nullptr);
    EXPECT_EQ(device.allocations_, 0u);
    EXPECT_EQ(device.starts_, 0);
}

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

TEST_F(GpuTimestampPollTest, CardinalElectionRecordsOnlyItsOwnStage) {
    GpuSubStageTimer timer;
    auto *state = timer.acquire("voxelCardinalElect");
    ASSERT_NE(state, nullptr);
    ASSERT_NE(state->info_, nullptr);
    EXPECT_EQ(state->info_->field_, &GpuStageTiming::voxelCardinalElectMs_);
    gpuStageTiming().voxelStage1Ms_ = 1.0f;
    gpuStageTiming().voxelStage2Ms_ = 3.0f;
    gpuStageTiming().voxelPerAxisFinalizeMs_ = 4.0f;
    timer.begin(*state);
    timer.end(*state);
    device_.status_ = TimestampReadStatus::READY;
    timer.begin(*state);
    timer.end(*state);
    EXPECT_FLOAT_EQ(gpuStageTiming().voxelCardinalElectMs_, 2.5f);
    EXPECT_FLOAT_EQ(gpuStageTiming().voxelStage1Ms_, 1.0f);
    EXPECT_FLOAT_EQ(gpuStageTiming().voxelStage2Ms_, 3.0f);
    EXPECT_FLOAT_EQ(gpuStageTiming().voxelPerAxisFinalizeMs_, 4.0f);
    const auto &accumulators = gpuStageAccumulators();
    for (std::size_t i = 0; i < accumulators.size(); ++i) {
        const bool elected = i == static_cast<std::size_t>(state->registryIndex_);
        EXPECT_EQ(accumulators[i].sampleCount_, elected ? 1u : 0u);
        EXPECT_DOUBLE_EQ(accumulators[i].sumMs_, elected ? 2.5 : 0.0);
    }
}

TEST_F(GpuTimestampPollTest, ExistingBooleanBackendAdaptsToPolling) {
    float ms = -1.0f;
    EXPECT_EQ(device_.RenderDevice::pollTimestampPairMs(1, ms), TimestampReadStatus::PENDING);
    device_.status_ = TimestampReadStatus::READY;
    EXPECT_EQ(device_.RenderDevice::pollTimestampPairMs(1, ms), TimestampReadStatus::READY);
    EXPECT_FLOAT_EQ(ms, 2.5f);
}

TEST_F(GpuTimestampPollTest, LegacyObserverStillFinishesAndSubstageRemainsDisabled) {
    gpuStageTiming().legacyFinishTiming_ = true;
    {
        GpuSubStageScope scope(gpuStageRegistry()[0].name_);
    }
    EXPECT_EQ(device_.allocations_, 0u);
    GpuStageTimingObserver observer;
    observer.tagStage(0, gpuStageRegistry()[0]);
    observer.onBeforeTick(0);
    observer.onAfterTick(0);
    EXPECT_EQ(device_.finishes_, 2);
    EXPECT_EQ(device_.starts_, 0);
    EXPECT_EQ(gpuStageAccumulators()[0].sampleCount_, 1u);
}

TEST_F(GpuTimestampPollTest, DisabledGpuTimingDoesNotWriteOrFinish) {
    gpuStageTiming().enabled_ = false;
    GpuStageTimingObserver observer;
    observer.tagStage(0, gpuStageRegistry()[0]);
    observer.onBeforeTick(0);
    observer.onAfterTick(0);
    {
        GpuSubStageScope scope(gpuStageRegistry()[0].name_);
    }
    EXPECT_EQ(device_.starts_, 0);
    EXPECT_EQ(device_.finishes_, 0);
    EXPECT_EQ(gpuStageAccumulators()[0].sampleCount_, 0u);
}

TEST_F(GpuTimestampPollTest, ObserverOwnsOnlySuccessfulAllocations) {
    device_.recommended_ = 3;
    device_.allocationLimit_ = 2;
    {
        GpuStageTimingObserver observer;
        observer.tagStage(0, gpuStageRegistry()[0]);
    }
    EXPECT_EQ(device_.destroyed_, (std::vector<GpuTimestampHandle>{1, 2}));
}

TEST_F(GpuTimestampPollTest, RepeatedTagPreservesHandlesAndPendingSample) {
    {
        GpuStageTimingObserver observer;
        observer.tagStage(0, gpuStageRegistry()[0]);
        observer.onBeforeTick(0);
        observer.onAfterTick(0);
        observer.tagStage(0, gpuStageRegistry()[0]);
        EXPECT_EQ(device_.allocations_, 1u);
        EXPECT_TRUE(device_.destroyed_.empty());
        device_.status_ = TimestampReadStatus::READY;
        observer.onBeforeTick(0);
        observer.onAfterTick(0);
        EXPECT_EQ(gpuStageAccumulators()[0].sampleCount_, 1u);
        EXPECT_EQ(device_.startHandles_, (std::vector<GpuTimestampHandle>{1, 1}));
    }
    EXPECT_EQ(device_.destroyed_, (std::vector<GpuTimestampHandle>{1}));
    EXPECT_EQ(device_.finishes_, 0);
}

TEST_F(GpuTimestampPollTest, RetagReleasesOldHandlesWithoutRelabelingPendingSamples) {
    {
        GpuStageTimingObserver observer;
        observer.tagStage(0, gpuStageRegistry()[0]);
        device_.status_ = TimestampReadStatus::READY;
        observer.onBeforeTick(0);
        observer.onAfterTick(0);
        observer.onBeforeTick(0);
        observer.onAfterTick(0);
        ASSERT_EQ(gpuStageAccumulators()[0].sampleCount_, 1u);

        observer.tagStage(0, gpuStageRegistry()[1]);
        EXPECT_EQ(device_.destroyed_, (std::vector<GpuTimestampHandle>{1}));
        observer.onBeforeTick(0);
        observer.onAfterTick(0);
        EXPECT_EQ(gpuStageAccumulators()[1].sampleCount_, 0u);
        observer.onBeforeTick(0);
        observer.onAfterTick(0);
        EXPECT_EQ(gpuStageAccumulators()[0].sampleCount_, 1u);
        EXPECT_EQ(gpuStageAccumulators()[1].sampleCount_, 1u);
        EXPECT_EQ(device_.startHandles_, (std::vector<GpuTimestampHandle>{1, 1, 2, 2}));
    }
    EXPECT_EQ(device_.destroyed_, (std::vector<GpuTimestampHandle>{1, 2}));
    EXPECT_EQ(device_.finishes_, 0);
}

TEST_F(GpuTimestampPollTest, RetagAfterPartialAllocationOwnsOnlyNewValidHandles) {
    device_.recommended_ = 3;
    device_.allocationLimit_ = 2;
    {
        GpuStageTimingObserver observer;
        observer.tagStage(0, gpuStageRegistry()[0]);
        observer.onBeforeTick(0);
        observer.onAfterTick(0);
        device_.allocationLimit_ = 4;
        observer.tagStage(0, gpuStageRegistry()[1]);
        EXPECT_EQ(device_.destroyed_, (std::vector<GpuTimestampHandle>{1, 2}));
        device_.status_ = TimestampReadStatus::READY;
        observer.onBeforeTick(0);
        observer.onAfterTick(0);
        EXPECT_EQ(device_.startHandles_.back(), 4u);
        EXPECT_EQ(gpuStageAccumulators()[1].sampleCount_, 0u);
        observer.onBeforeTick(0);
        observer.onAfterTick(0);
        EXPECT_EQ(gpuStageAccumulators()[0].sampleCount_, 0u);
        EXPECT_EQ(gpuStageAccumulators()[1].sampleCount_, 1u);
    }
    EXPECT_EQ(device_.destroyed_, (std::vector<GpuTimestampHandle>{1, 2, 4}));
    EXPECT_EQ(device_.finishes_, 0);
}

TEST_F(GpuTimestampPollTest, RegistrationFollowsManagerClearAndSameAddressReconstruction) {
    device_.allocationLimit_ = 20;
    std::optional<IRSystem::SystemManager> manager;
    for (int lifetime = 0; lifetime < 3; ++lifetime) {
        manager.emplace();
        for (int phase = 0; phase < 2; ++phase) {
            auto *observer = IRRender::detail::installAndGetObserver();
            ASSERT_NE(observer, nullptr);
            ASSERT_EQ(manager->findTickObserver<GpuStageTimingObserver>(), observer);
            EXPECT_EQ(IRRender::detail::installAndGetObserver(), observer);
            tagGpuStage(0, gpuStageRegistry()[0].name_);
            observer->onBeforeTick(0);
            observer->onAfterTick(0);
            if (phase == 0) {
                manager->clearTickObservers();
                EXPECT_EQ(manager->findTickObserver<GpuStageTimingObserver>(), nullptr);
            }
        }
        manager.reset();
        EXPECT_EQ(device_.destroyed_.size(), static_cast<std::size_t>((lifetime + 1) * 2));
    }
    EXPECT_EQ(device_.allocations_, 6u);
    EXPECT_EQ(device_.destroyed_, (std::vector<GpuTimestampHandle>{1, 2, 3, 4, 5, 6}));
    EXPECT_EQ(device_.finishes_, 0);
}

TEST_F(GpuTimestampPollTest, RegistrationUsesEachLiveManagersObserver) {
    IRSystem::SystemManager first;
    auto *firstObserver = IRRender::detail::installAndGetObserver();
    ASSERT_EQ(first.findTickObserver<GpuStageTimingObserver>(), firstObserver);
    ASSERT_NE(firstObserver, nullptr);
    tagGpuStage(0, gpuStageRegistry()[0].name_);
    {
        IRSystem::SystemManager second;
        auto *secondObserver = IRRender::detail::installAndGetObserver();
        ASSERT_EQ(second.findTickObserver<GpuStageTimingObserver>(), secondObserver);
        ASSERT_NE(secondObserver, nullptr);
        EXPECT_NE(secondObserver, firstObserver);
        EXPECT_EQ(first.findTickObserver<GpuStageTimingObserver>(), firstObserver);
        tagGpuStage(0, gpuStageRegistry()[1].name_);
    }
    EXPECT_EQ(device_.destroyed_, (std::vector<GpuTimestampHandle>{2}));
    first.clearTickObservers();
    EXPECT_EQ(device_.destroyed_, (std::vector<GpuTimestampHandle>{2, 1}));
}

TEST_F(GpuTimestampPollTest, SubstageDestructionDoesNotTouchDevice) {
    {
        GpuSubStageTimer timer;
        EXPECT_NE(timer.acquire(gpuStageRegistry()[0].name_), nullptr);
    }
    EXPECT_EQ(device_.allocations_, 1u);
    EXPECT_TRUE(device_.destroyed_.empty());
}

#if defined(IR_GRAPHICS_OPENGL)
// classifyOpenGLTimestampPair is the decision OpenGLRenderDevice::pollTimestampPairMs
// delegates to; testing it directly needs no GL context.
TEST(OpenGLTimestampClassifyTest, NotYetAvailableStaysPending) {
    float ms = -1.0f;
    EXPECT_EQ(classifyOpenGLTimestampPair(false, false, 0, 0, ms), TimestampReadStatus::PENDING);
    EXPECT_EQ(classifyOpenGLTimestampPair(true, false, 100, 50, ms), TimestampReadStatus::PENDING);
}

TEST(OpenGLTimestampClassifyTest, AvailableAndInvertedIsInvalid) {
    float ms = -1.0f;
    EXPECT_EQ(classifyOpenGLTimestampPair(true, true, 200, 100, ms), TimestampReadStatus::INVALID);
}

TEST(OpenGLTimestampClassifyTest, AvailableAndOrderedIsReadyWithDurationMs) {
    float ms = -1.0f;
    EXPECT_EQ(
        classifyOpenGLTimestampPair(true, true, 1'000'000, 3'500'000, ms),
        TimestampReadStatus::READY
    );
    EXPECT_FLOAT_EQ(ms, 2.5f);
}
#endif

TEST(GpuFrameTimingTest, SplitSubmissionsKeepEnvelopeSeparateFromSpanSum) {
    GpuFrameTimingAccumulator timing;
    timing.reset(true, true);
    timing.beginFrame();
    timing.addCompletedBuffer(10.000, 10.002, true);
    timing.addCompletedBuffer(10.005, 10.008, true);
    timing.endFrame();
    const auto &stats = timing.stats();
    EXPECT_EQ(stats.attemptedFrames_, 1u);
    EXPECT_EQ(stats.validFrames_, 1u);
    EXPECT_EQ(stats.commandBuffers_, 2u);
    EXPECT_NEAR(stats.envelope_.totalMs_, 8.0, 1e-8);
    EXPECT_NEAR(stats.commandBufferSpans_.totalMs_, 5.0, 1e-8);
    timing.endFrame();
    EXPECT_EQ(stats.validFrames_, 1u);
}

TEST(GpuFrameTimingTest, FailedOrMissingIntervalsNeverPublishPartialFrames) {
    GpuFrameTimingAccumulator timing;
    timing.reset(true, true);
    for (const auto invalidEnd :
         {0.0,
          9.0,
          std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::quiet_NaN()}) {
        timing.beginFrame();
        timing.addCompletedBuffer(10.0, 10.001, true);
        timing.addCompletedBuffer(10.0, invalidEnd, true);
        timing.endFrame();
    }
    timing.beginFrame();
    timing.addCompletedBuffer(10.0, 10.001, false);
    timing.endFrame();
    timing.beginFrame();
    timing.endFrame();
    EXPECT_EQ(timing.stats().attemptedFrames_, 6u);
    EXPECT_EQ(timing.stats().invalidFrames_, 6u);
    EXPECT_EQ(timing.stats().validFrames_, 0u);
    EXPECT_EQ(timing.stats().commandBuffers_, 0u);
}

TEST(GpuFrameTimingTest, ResetExcludesOldAndOutOfFrameSubmissions) {
    GpuFrameTimingAccumulator timing;
    timing.reset(true, true);
    timing.beginFrame();
    timing.addCompletedBuffer(10.0, 10.1, true);
    timing.reset(true, true);
    timing.addCompletedBuffer(10.2, 10.3, true);
    timing.endFrame();
    EXPECT_EQ(timing.stats().attemptedFrames_, 0u);
    timing.beginFrame();
    timing.addCompletedBuffer(20.0, 20.001, true);
    timing.endFrame();
    EXPECT_EQ(timing.stats().validFrames_, 1u);
    EXPECT_NEAR(timing.stats().envelope_.totalMs_, 1.0, 1e-8);
    timing.reset(false, true);
    timing.beginFrame();
    timing.addCompletedBuffer(30.0, 30.1, true);
    timing.endFrame();
    EXPECT_EQ(timing.stats().attemptedFrames_, 0u);
    TimestampDevice unsupported;
    unsupported.setGpuFrameTimingEnabled(true);
    EXPECT_FALSE(unsupported.gpuFrameTimingStats().supported_);
}

TEST(GpuFrameTimingTest, InterruptedFrameIsCountedAsInvalid) {
    GpuFrameTimingAccumulator timing;
    timing.reset(true, true);
    timing.beginFrame();
    timing.addCompletedBuffer(10.0, 10.001, true);
    timing.beginFrame();
    timing.addCompletedBuffer(20.0, 20.002, true);
    timing.endFrame();
    EXPECT_EQ(timing.stats().attemptedFrames_, 2u);
    EXPECT_EQ(timing.stats().invalidFrames_, 1u);
    EXPECT_EQ(timing.stats().validFrames_, 1u);
    EXPECT_NEAR(timing.stats().envelope_.totalMs_, 2.0, 1e-8);
}
} // namespace

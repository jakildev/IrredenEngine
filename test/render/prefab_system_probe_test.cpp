// The two prefab headless-introspection probes — `HelpOverlay::systemOrNull()`
// and `SettingsMenu::systemOrNull()` — resolve their system through
// `IRSystem::findSystem` and must read a clean negative when the creation never
// registered it. `findSystem`'s miss answer is `IRSystem::kNullSystemId`, not
// `IREntity::kNullEntity`, because 0 is a legitimate id (the first system
// registered in a process gets it).
//
// Comparing against the wrong sentinel breaks the probe in BOTH directions, so
// each prefab is covered by a pair of arms:
//
//   * absent system  — `kNullEntity` (0) never equals the `kNullSystemId` miss
//     answer, so the guard falls through to `getSystemParams(kNullSystemId)`,
//     which trips `IR_ASSERT(system < m_nextSystemId)` in debug and indexes
//     `std::vector m_systemParams` out of bounds under `IR_RELEASE`.
//   * system registered as id 0 — the guard matches a *live* id and reports the
//     registered system as absent, a silent false negative no assert catches.
//
// The id-0 arm is not incidental: each fixture builds a fresh `SystemManager`
// whose `m_nextSystemId` starts at 0, so the first system it registers lands on
// exactly the id the wrong sentinel aliases.
//
// GL-free by construction: registering a system only records its archetype and
// params, and the probes are two hash lookups. No context, no backend, so this
// runs on every host rather than skipping on the Metal ones.

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/time/time_manager.hpp>
#include <irreden/render/glitch.hpp>
#include <irreden/render/glitch_subject.hpp>
#include <irreden/render/help_overlay.hpp>
#include <irreden/render/render_device.hpp>
#include <irreden/render/settings_menu.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_perf_stats_overlay.hpp>

#include <cstddef>
#include <cstdint>
#include <array>
#include <string>
#include <vector>

namespace IRRender {
extern RenderDevice *g_renderDevice;
} // namespace IRRender

namespace {

struct C_PrefabProbeSibling {};

class PrefabSystemProbeTest : public testing::Test {
  protected:
    PrefabSystemProbeTest()
        : m_time_manager{}
        , m_entity_manager{}
        , m_system_manager{} {}

    void expectMainThreadRegistration(IRSystem::SystemId target, const char *targetName) {
        const IRSystem::SystemAccess targetAccess = m_system_manager.getSystemAccess(target);
        EXPECT_TRUE(targetAccess.mainThreadOnly_);
        EXPECT_EQ(m_system_manager.getSystemName(target), targetName);

        const IRSystem::SystemId sibling = IRSystem::createSystem<C_PrefabProbeSibling>(
            "PrefabProbeSibling",
            [](C_PrefabProbeSibling &) {}
        );
        const IRSystem::SystemAccess accesses[]{
            targetAccess,
            m_system_manager.getSystemAccess(sibling),
        };
        EXPECT_EQ(
            IRSystem::findPipelineGroupConflict(accesses, 2).kind_,
            IRSystem::GroupConflictKind::MAIN_THREAD_IN_GROUP
        );

        m_system_manager.registerPipelineGroups(IRTime::Events::RENDER, {{target}});
        EXPECT_NO_THROW(m_system_manager.validateAllPipelineGroups());

        m_system_manager.registerPipelineGroups(IRTime::Events::RENDER, {{target, sibling}});
        EXPECT_THROW(m_system_manager.validateAllPipelineGroups(), std::runtime_error);
    }

    // Registers a no-op RENDER pipeline so `advanceRenderTick` moves the
    // RENDER event tick without running either screen pass's GPU path.
    void startRenderClock() {
        const IRSystem::SystemId sibling = IRSystem::createSystem<C_PrefabProbeSibling>(
            "PrefabProbeSibling",
            [](C_PrefabProbeSibling &) {}
        );
        m_system_manager.registerPipeline(IRTime::Events::RENDER, {sibling});
    }

    void advanceRenderTick() {
        m_system_manager.executePipeline(IRTime::Events::RENDER);
    }

    IRTime::TimeManager m_time_manager;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(PrefabSystemProbeTest, HelpOverlayProbeReadsAbsentWhenTheSystemIsNotRegistered) {
    ASSERT_EQ(IRSystem::findSystem(IRSystem::HELP_OVERLAY), IRSystem::kNullSystemId)
        << "fixture precondition: nothing registered HELP_OVERLAY";

    EXPECT_EQ(IRPrefab::HelpOverlay::systemOrNull(), nullptr);
    EXPECT_EQ(IRPrefab::HelpOverlay::builtText(), std::string{});
    EXPECT_EQ(IRPrefab::HelpOverlay::lastGlyphCommandCount(), 0);
}

TEST_F(PrefabSystemProbeTest, SettingsMenuProbeReadsAbsentWhenTheSystemIsNotRegistered) {
    ASSERT_EQ(IRSystem::findSystem(IRSystem::SETTINGS_MENU), IRSystem::kNullSystemId)
        << "fixture precondition: nothing registered SETTINGS_MENU";

    EXPECT_EQ(IRPrefab::SettingsMenu::systemOrNull(), nullptr);
    EXPECT_EQ(IRPrefab::SettingsMenu::liveRowCount(), 0);
    EXPECT_EQ(IRPrefab::SettingsMenu::quitButton(), IREntity::kNullEntity);
}

// Positive control for the pair above, and the arm that pins the sentinel: a
// probe hardcoded to `return nullptr` would pass both absent arms, and a probe
// comparing against `IREntity::kNullEntity` fails here specifically.
TEST_F(PrefabSystemProbeTest, HelpOverlayProbeResolvesTheSystemRegisteredAsIdZero) {
    const IRSystem::SystemId id = IRSystem::createSystem<IRSystem::HELP_OVERLAY>();
    ASSERT_EQ(id, IRSystem::SystemId{0})
        << "arm precondition: the first system in a fresh manager must take id "
           "0 — the id `IREntity::kNullEntity` aliases";

    const auto *system = IRPrefab::HelpOverlay::systemOrNull();
    EXPECT_NE(system, nullptr) << "a live system registered as id 0 must not read as absent";
    EXPECT_EQ(system, IRSystem::getSystemParams<IRSystem::System<IRSystem::HELP_OVERLAY>>(id));
}

TEST_F(PrefabSystemProbeTest, SettingsMenuProbeResolvesTheSystemRegisteredAsIdZero) {
    const IRSystem::SystemId id = IRSystem::createSystem<IRSystem::SETTINGS_MENU>();
    ASSERT_EQ(id, IRSystem::SystemId{0})
        << "arm precondition: the first system in a fresh manager must take id "
           "0 — the id `IREntity::kNullEntity` aliases";

    const auto *system = IRPrefab::SettingsMenu::systemOrNull();
    EXPECT_NE(system, nullptr) << "a live system registered as id 0 must not read as absent";
    EXPECT_EQ(system, IRSystem::getSystemParams<IRSystem::System<IRSystem::SETTINGS_MENU>>(id));
}

TEST_F(PrefabSystemProbeTest, PerfStatsOverlayRegistrationRequiresMainThread) {
    const IRSystem::SystemId id = IRSystem::createSystem<IRSystem::PERF_STATS_OVERLAY>();
    expectMainThreadRegistration(id, "PerfStatsOverlay");
}

TEST_F(PrefabSystemProbeTest, SettingsMenuRegistrationRequiresMainThread) {
    const IRSystem::SystemId id = IRSystem::createSystem<IRSystem::SETTINGS_MENU>();
    expectMainThreadRegistration(id, "SettingsMenu");
}

// `FRAMEBUFFER_TO_SCREEN` skips its blit only when the glitch pass published
// the CURRENT RENDER tick. Registering through `registerSystem` directly sets
// up the params without `create()`'s GPU resources.
IRSystem::SystemId registerGlitchParamsOnly() {
    return IRSystem::registerSystem<
        IRSystem::FRAMEBUFFER_GLITCH,
        IRComponents::C_TrixelCanvasFramebuffer,
        IRComponents::C_WorldTransform,
        IRComponents::C_Name>("FramebufferGlitch");
}

TEST_F(PrefabSystemProbeTest, GlitchPublicationReadsFalseWhenTheSystemIsAbsent) {
    ASSERT_EQ(IRSystem::findSystem(IRSystem::FRAMEBUFFER_GLITCH), IRSystem::kNullSystemId);
    EXPECT_EQ(IRPrefab::Glitch::systemOrNull(), nullptr);
    EXPECT_FALSE(IRPrefab::Glitch::screenCompositedForCurrentRenderTick());
}

TEST_F(PrefabSystemProbeTest, GlitchPublicationHoldsForTheCurrentRenderTickOnly) {
    const IRSystem::SystemId id = registerGlitchParamsOnly();
    ASSERT_EQ(id, IRSystem::SystemId{0});
    auto *system = IRPrefab::Glitch::systemOrNull();
    ASSERT_NE(system, nullptr);
    EXPECT_FALSE(IRPrefab::Glitch::screenCompositedForCurrentRenderTick())
        << "a freshly registered pass has published nothing";
    EXPECT_FALSE(system->ownsHistoryImage()) << "history allocates on first enabled use";

    startRenderClock();
    advanceRenderTick();
    const std::uint64_t current = IRSystem::getEventTickCount(IRTime::Events::RENDER);

    system->publishedRenderTick_ = current;
    EXPECT_TRUE(IRPrefab::Glitch::screenCompositedForCurrentRenderTick());

    advanceRenderTick();
    EXPECT_FALSE(IRPrefab::Glitch::screenCompositedForCurrentRenderTick())
        << "a publication from the previous RENDER tick is stale";
}

// Off state: no singleton, or one with zero intensity. beginTick publishes
// nothing, zeroes the live GPU row and counts no copy or draw, all without a
// GPU context.
TEST_F(PrefabSystemProbeTest, GlitchOffFrameDoesNoWorkAndZeroesItsTimingRow) {
    const IRSystem::SystemId id = registerGlitchParamsOnly();
    auto *system = IRSystem::getSystemParams<IRSystem::System<IRSystem::FRAMEBUFFER_GLITCH>>(id);
    IRRender::gpuStageTiming().framebufferGlitchMs_ = 0.42f;

    system->beginTick();
    EXPECT_FALSE(system->enabledThisFrame_);
    EXPECT_FLOAT_EQ(IRRender::gpuStageTiming().framebufferGlitchMs_, 0.0f);

    IREntity::singleton<IRComponents::C_GlitchState>().tearWeight_ = 1.0f;
    system->beginTick();
    EXPECT_FALSE(system->enabledThisFrame_) << "zero intensity is off";
    EXPECT_EQ(system->frameHistoryCopies_, 0);
    EXPECT_EQ(system->frameDraws_, 0);
    EXPECT_EQ(system->historyCopyCount_, 0u);
    EXPECT_EQ(system->drawCount_, 0u);
    EXPECT_FALSE(IRPrefab::Glitch::screenCompositedForCurrentRenderTick());
}

// The screen-base destination and resource contract. The glitch pass's real
// `encodeFrame` and the blit's real `beginFrame` / `encodeBlit` run against
// recording encoders, so every image allocation, history copy, default-target
// clear and screen-base draw either pass issues lands in one ordered log. A
// recording device appends any device call made outside the encoders to the
// same log. The systems' `tick` / `beginTick` only resolve the camera, the
// framebuffer and the GPU encoders; those need a live RenderManager and
// backend.
using GlitchSystem = IRSystem::System<IRSystem::FRAMEBUFFER_GLITCH>;
using ScreenBlitSystem = IRSystem::System<IRSystem::FRAMEBUFFER_TO_SCREEN>;

using RenderOpLog = std::vector<std::string>;

class GlitchRecordingEncoder final : public IRSystem::GlitchEncoder {
  public:
    explicit GlitchRecordingEncoder(RenderOpLog &log)
        : m_log{log} {}

    void recreateHistory(IRMath::ivec2 size) override {
        EXPECT_FALSE(IRPrefab::Glitch::screenCompositedForCurrentRenderTick())
            << "the publication must be withdrawn before its history is replaced";
        if (liveImages_ > 0) {
            m_log.push_back("glitch:destroyHistory");
            --liveImages_;
        }
        m_log.push_back("glitch:createHistory");
        ++liveImages_;
        historySize_ = size;
    }
    void copyHistory() override {
        EXPECT_GT(liveImages_, 0) << "a copy needs a history image";
        m_log.push_back("glitch:copyHistory");
    }
    void bindAndClearScreen() override {
        m_log.push_back("glitch:bindAndClearScreen");
    }
    void drawScreenBase(const IRSystem::FrameDataFramebufferGlitch &) override {
        EXPECT_FALSE(IRPrefab::Glitch::screenCompositedForCurrentRenderTick())
            << "the pass must not publish before its screen-base draw";
        m_log.push_back("glitch:drawScreenBase");
    }

    int liveImages_ = 0;
    IRMath::ivec2 historySize_{0};

  private:
    RenderOpLog &m_log;
};

class ScreenBlitRecordingEncoder final : public IRSystem::ScreenBlitEncoder {
  public:
    explicit ScreenBlitRecordingEncoder(RenderOpLog &log)
        : m_log{log} {}

    void beginScreen() override {
        m_log.push_back("blit:beginScreen");
    }
    void drawFramebuffer(
        const IRComponents::C_TrixelCanvasFramebuffer &, IRMath::vec3, bool isMainFramebuffer
    ) override {
        m_log.push_back(isMainFramebuffer ? "blit:drawMainFramebuffer" : "blit:drawFramebuffer");
    }

  private:
    RenderOpLog &m_log;
};

class RecordingRenderDevice final : public IRRender::RenderDevice {
  public:
    explicit RecordingRenderDevice(RenderOpLog &log)
        : m_log{log} {}

    void beginFrame() override {}
    void present() override {}
    void dispatchCompute(std::uint32_t, std::uint32_t, std::uint32_t) override {
        m_log.push_back("device:dispatchCompute");
    }
    void dispatchComputeIndirect(const IRRender::Buffer *, std::ptrdiff_t) override {
        m_log.push_back("device:dispatchComputeIndirect");
    }
    void memoryBarrier(IRRender::BarrierType) override {}
    void drawElements(IRRender::DrawMode, int, IRRender::IndexType) override {
        m_log.push_back("device:drawElements");
    }
    void drawElementsInstanced(IRRender::DrawMode, int, IRRender::IndexType, int) override {
        m_log.push_back("device:drawElementsInstanced");
    }
    void drawElementsInstancedIndirect(
        IRRender::DrawMode, IRRender::IndexType, const IRRender::Buffer *, std::ptrdiff_t
    ) override {
        m_log.push_back("device:drawElementsInstancedIndirect");
    }
    void drawArrays(IRRender::DrawMode, int, int) override {
        m_log.push_back("device:drawArrays");
    }
    void drawArraysInstanced(IRRender::DrawMode, int, int, int) override {
        m_log.push_back("device:drawArraysInstanced");
    }
    void copyImageSubData(
        std::uint32_t, int, int, int, int, std::uint32_t, int, int, int, int, int, int, int
    ) override {
        m_log.push_back("device:copyImageSubData");
    }
    void copyTexture2D(const IRRender::Texture2D &, IRRender::Texture2D &) override {
        m_log.push_back("device:copyTexture2D");
    }
    void setPolygonMode(IRRender::PolygonMode) override {}
    void bindDefaultFramebuffer() override {
        m_log.push_back("device:bindDefaultFramebuffer");
    }
    void clearDefaultFramebuffer() override {
        m_log.push_back("device:clearDefaultFramebuffer");
    }
    bool readDefaultFramebuffer(int, int, int, int, void *) override {
        return false;
    }
    void enableBlending() override {}
    void disableBlending() override {}
    void setDepthTest(bool) override {}
    void setDepthWrite(bool) override {}
    void clearTexImage(const IRRender::Texture2D *, int, const void *) override {
        m_log.push_back("device:clearTexImage");
    }
    void fillBuffer(const IRRender::Buffer *, std::size_t, std::uint8_t) override {}
    void finish() override {}

  private:
    RenderOpLog &m_log;
};

IRSystem::SystemId registerScreenBlitParamsOnly() {
    return IRSystem::registerSystem<
        IRSystem::FRAMEBUFFER_TO_SCREEN,
        IRComponents::C_TrixelCanvasFramebuffer,
        IRComponents::C_WorldTransform,
        IRComponents::C_Name>("FramebufferToScreen");
}

IRComponents::C_GlitchState &enableHoldGlitch() {
    auto &state = IREntity::singleton<IRComponents::C_GlitchState>();
    state.intensity_ = 1.0f;
    state.holdWeight_ = 1.0f;
    return state;
}

constexpr IRMath::ivec2 kProbeFramebufferSize{320, 180};
constexpr IRMath::ivec2 kProbeTrixelStep{2, 2};

class GlitchScheduleProbeTest : public PrefabSystemProbeTest {
  protected:
    GlitchScheduleProbeTest()
        : m_encoder{m_log}
        , m_blitEncoder{m_log}
        , m_device{m_log} {
        m_previousDevice = IRRender::g_renderDevice;
        IRRender::setDevice(&m_device);
        m_glitch = IRSystem::getSystemParams<GlitchSystem>(registerGlitchParamsOnly());
        m_blit = IRSystem::getSystemParams<ScreenBlitSystem>(registerScreenBlitParamsOnly());
        startRenderClock();
    }
    ~GlitchScheduleProbeTest() override {
        IRRender::setDevice(m_previousDevice);
    }

    // One RENDER tick of the two screen passes in pipeline order; returns
    // everything they issued. An off glitch frame runs the pass's real `tick`,
    // which must return before resolving anything.
    RenderOpLog renderFrame(IRMath::ivec2 framebufferSize = kProbeFramebufferSize) {
        m_log.clear();
        advanceRenderTick();
        m_glitch->beginTick();
        if (m_glitch->enabledThisFrame_) {
            m_glitch->encodeFrame(m_encoder, framebufferSize, kProbeTrixelStep, IRMath::mat4(1.0f));
        } else {
            m_glitch->tick(m_offscreenFramebuffer, m_cameraXform, m_mainName);
        }
        runBlit();
        return m_log;
    }

    void runBlit() {
        m_blit->beginFrame(m_blitEncoder);
        m_blit->encodeBlit(m_blitEncoder, m_offscreenFramebuffer, m_cameraXform, m_mainName);
    }

    RenderOpLog m_log;
    GlitchRecordingEncoder m_encoder;
    ScreenBlitRecordingEncoder m_blitEncoder;
    RecordingRenderDevice m_device;
    IRRender::RenderDevice *m_previousDevice = nullptr;
    GlitchSystem *m_glitch = nullptr;
    ScreenBlitSystem *m_blit = nullptr;
    // Never read: the recording encoders ignore it, and the glitch pass's
    // real `tick` runs only on off frames, where it returns first.
    const IRComponents::C_TrixelCanvasFramebuffer m_offscreenFramebuffer{};
    const IRComponents::C_WorldTransform m_cameraXform{};
    const IRComponents::C_Name m_mainName{"mainFramebuffer"};
};

TEST_F(GlitchScheduleProbeTest, EnabledFrameClearsAndDrawsTheScreenBaseOnceInPlaceOfTheBlit) {
    enableHoldGlitch();

    EXPECT_EQ(
        renderFrame(),
        (RenderOpLog{
            "glitch:createHistory",
            "glitch:copyHistory",
            "glitch:bindAndClearScreen",
            "glitch:drawScreenBase"
        })
    ) << "first use allocates and seeds the history, then clears and draws once; "
         "the ordinary blit issues nothing";
    EXPECT_TRUE(IRPrefab::Glitch::screenCompositedForCurrentRenderTick());
    EXPECT_TRUE(m_blit->screenAlreadyComposited_);
    EXPECT_EQ(m_blit->ordinaryBlitCount_, 0u);
    EXPECT_EQ(m_encoder.liveImages_, 1);
    EXPECT_EQ(m_encoder.historySize_, kProbeFramebufferSize);
    EXPECT_TRUE(m_glitch->historyValid_);

    EXPECT_EQ(renderFrame(), (RenderOpLog{"glitch:bindAndClearScreen", "glitch:drawScreenBase"}))
        << "the same hold tick reuses the image and its snapshot";

    advanceRenderTick();
    m_glitch->beginTick();
    m_glitch->clock_.holdTick_ += 1;
    m_log.clear();
    m_glitch->encodeFrame(m_encoder, kProbeFramebufferSize, kProbeTrixelStep, IRMath::mat4(1.0f));
    runBlit();
    EXPECT_EQ(
        m_log,
        (RenderOpLog{"glitch:copyHistory", "glitch:bindAndClearScreen", "glitch:drawScreenBase"})
    ) << "a new hold tick re-snapshots into the same image";

    EXPECT_EQ(m_encoder.liveImages_, 1);
    EXPECT_EQ(m_glitch->historyCopyCount_, 2u);
    EXPECT_EQ(m_glitch->drawCount_, 3u);
    EXPECT_EQ(m_blit->ordinaryBlitCount_, 0u);
}

TEST_F(GlitchScheduleProbeTest, ResizeWithdrawsPublicationAndReplacesOnlyTheHistory) {
    enableHoldGlitch();
    renderFrame();
    ASSERT_TRUE(IRPrefab::Glitch::screenCompositedForCurrentRenderTick());

    // A resize inside an already-published tick: the encoder asserts the
    // publication is gone before the history is replaced.
    const IRMath::ivec2 resized{kProbeFramebufferSize.x * 2, kProbeFramebufferSize.y + 6};
    m_log.clear();
    m_glitch->encodeFrame(m_encoder, resized, kProbeTrixelStep, IRMath::mat4(1.0f));
    EXPECT_EQ(
        m_log,
        (RenderOpLog{
            "glitch:destroyHistory",
            "glitch:createHistory",
            "glitch:copyHistory",
            "glitch:bindAndClearScreen",
            "glitch:drawScreenBase"
        })
    ) << "the replacement history is reseeded on the same hold tick";
    EXPECT_EQ(m_encoder.liveImages_, 1) << "the history is replaced, not joined";
    EXPECT_EQ(m_encoder.historySize_, resized);
    EXPECT_TRUE(IRPrefab::Glitch::screenCompositedForCurrentRenderTick());

    // Without a history reader the replacement is allocated but never copied.
    auto &state = IREntity::singleton<IRComponents::C_GlitchState>();
    state.holdWeight_ = 0.0f;
    state.tearWeight_ = 1.0f;
    EXPECT_EQ(
        renderFrame(kProbeFramebufferSize),
        (RenderOpLog{
            "glitch:destroyHistory",
            "glitch:createHistory",
            "glitch:bindAndClearScreen",
            "glitch:drawScreenBase"
        })
    );
    EXPECT_EQ(m_glitch->historyCopyCount_, 2u);
    EXPECT_EQ(m_blit->ordinaryBlitCount_, 0u);
}

TEST_F(GlitchScheduleProbeTest, OffFrameLeavesTheScreenBaseToTheOrdinaryBlit) {
    EXPECT_EQ(renderFrame(), (RenderOpLog{"blit:beginScreen", "blit:drawMainFramebuffer"}))
        << "with no glitch state the blit clears and draws the main framebuffer once";
    EXPECT_FALSE(m_blit->screenAlreadyComposited_);
    EXPECT_EQ(m_blit->ordinaryBlitCount_, 1u);
    EXPECT_EQ(m_encoder.liveImages_, 0) << "an unused pass allocates nothing";

    auto &state = enableHoldGlitch();
    renderFrame();
    ASSERT_EQ(m_blit->ordinaryBlitCount_, 1u);

    state.intensity_ = 0.0f;
    EXPECT_EQ(renderFrame(), (RenderOpLog{"blit:beginScreen", "blit:drawMainFramebuffer"}))
        << "an off tick encodes no glitch work; the blit clears and draws once";
    EXPECT_FALSE(IRPrefab::Glitch::screenCompositedForCurrentRenderTick());
    EXPECT_FALSE(m_blit->screenAlreadyComposited_);
    EXPECT_EQ(m_blit->ordinaryBlitCount_, 2u);
    EXPECT_EQ(m_glitch->frameDraws_, 0);
    EXPECT_EQ(m_glitch->frameHistoryCopies_, 0);
    EXPECT_EQ(m_encoder.liveImages_, 1) << "an off tick keeps the history image";
    EXPECT_FALSE(m_glitch->historyValid_);

    state.intensity_ = 1.0f;
    EXPECT_EQ(
        renderFrame(),
        (RenderOpLog{"glitch:copyHistory", "glitch:bindAndClearScreen", "glitch:drawScreenBase"})
    ) << "the snapshot from before the off tick is stale";
    EXPECT_EQ(m_encoder.liveImages_, 1);
    EXPECT_EQ(m_blit->ordinaryBlitCount_, 2u);
}

// ---- TRIXEL_GLITCH ------------------------------------------------------------
//
// The canvas pass's real `beginTick` / `tick` gather, `prepareFrame` decision
// and `encodeFrame` schedule run against a recording encoder that models the
// main canvas's glitch images as handles: the live colour plane starts as 1
// and the scratch destination as 2, and every swap exchanges them. The
// recording device catches any device call made outside the encoder.

using TrixelGlitchSystem = IRSystem::System<IRSystem::TRIXEL_GLITCH>;

IRSystem::SystemId registerTrixelGlitchParamsOnly() {
    return IRSystem::registerSystem<IRSystem::TRIXEL_GLITCH, IRComponents::C_GlitchSubject>(
        "TrixelGlitch"
    );
}

class TrixelGlitchRecordingEncoder final : public IRSystem::TrixelGlitchEncoder {
  public:
    explicit TrixelGlitchRecordingEncoder(RenderOpLog &log)
        : m_log{log} {}

    void createImages() override {
        if (liveImages_ > 0) {
            m_log.push_back("trixel:destroyImages");
        }
        m_log.push_back("trixel:createImages");
        liveImages_ = 3;
        liveColour_ = 1;
        scratchColour_ = 2;
    }
    void uploadSubjects(std::span<const IRMath::uvec4> words) override {
        m_log.push_back("trixel:upload");
        lastWords_.assign(words.begin(), words.end());
    }
    void copyHistoryColors() override {
        EXPECT_EQ(liveImages_, 3) << "a copy needs the history images";
        m_log.push_back("trixel:copyHistoryColors");
    }
    void copyHistoryEntityIds() override {
        EXPECT_EQ(liveImages_, 3);
        m_log.push_back("trixel:copyHistoryEntityIds");
    }
    void dispatch() override {
        EXPECT_EQ(liveImages_, 3) << "the dispatch needs its destination";
        m_log.push_back(
            "trixel:dispatch read=" + std::to_string(liveColour_) +
            " write=" + std::to_string(scratchColour_)
        );
    }
    void swapColors() override {
        m_log.push_back("trixel:swap");
        std::swap(liveColour_, scratchColour_);
    }
    void createPerAxisScratch() override {
        m_log.push_back("trixel:perAxisCreateScratch");
        perAxisScratch_ = true;
    }
    void createPerAxisHistory() override {
        m_log.push_back("trixel:perAxisCreateHistory");
        perAxisHistory_ = true;
    }
    void copyPerAxisHistoryColors(int axis) override {
        m_log.push_back("trixel:perAxisCopyColors:" + std::to_string(axis));
    }
    void copyPerAxisHistoryEntityIds(int axis) override {
        m_log.push_back("trixel:perAxisCopyIds:" + std::to_string(axis));
    }
    void dispatchPerAxis(int axis) override {
        m_log.push_back("trixel:perAxisDispatch:" + std::to_string(axis));
    }
    void swapPerAxisColors(int axis) override {
        m_log.push_back("trixel:perAxisSwap:" + std::to_string(axis));
    }
    void finishPerAxisDispatches() override {
        m_log.push_back("trixel:perAxisBarrier");
    }

    int liveImages_ = 0;
    int liveColour_ = 0;
    int scratchColour_ = 0;
    bool perAxisScratch_ = false;
    bool perAxisHistory_ = false;
    std::array<int, 3> perAxisDistanceHandles_{{11, 12, 13}};
    std::array<int, 3> perAxisIdHandles_{{21, 22, 23}};
    std::vector<IRMath::uvec4> lastWords_;

  private:
    RenderOpLog &m_log;
};

class TrixelGlitchScheduleProbeTest : public PrefabSystemProbeTest {
  protected:
    TrixelGlitchScheduleProbeTest()
        : m_encoder{m_log}
        , m_device{m_log} {
        m_previousDevice = IRRender::g_renderDevice;
        IRRender::setDevice(&m_device);
        m_trixel = IRSystem::getSystemParams<TrixelGlitchSystem>(registerTrixelGlitchParamsOnly());
    }
    ~TrixelGlitchScheduleProbeTest() override {
        IRRender::setDevice(m_previousDevice);
    }

    // One RENDER tick of the pass over `m_subjects`. An off frame runs the
    // real `endTick`, which must return before resolving the canvas; an
    // enabled one hands `encodeFrame` the recorder, with the images present
    // unless @p resized dropped them. @p tickAdvance moves the period tick.
    RenderOpLog renderFrame(
        bool resized = false,
        std::int64_t tickAdvance = 0,
        bool perAxisAllocated = false,
        int perAxisCardinal = 0
    ) {
        m_log.clear();
        m_trixel->beginTick();
        m_trixel->clock_.tick_ += m_tickOffset + tickAdvance;
        m_tickOffset += tickAdvance;
        for (const auto &[entity, subject] : m_subjects) {
            m_trixel->tick(entity, subject);
        }
        if (m_trixel->prepareFrame()) {
            if (resized) {
                m_encoder.liveImages_ = 0;
            }
            IRSystem::TrixelGlitchPerAxisFrameInput perAxisInput;
            perAxisInput.allocated_ = perAxisAllocated;
            perAxisInput.scratchAllocated_ = m_encoder.perAxisScratch_;
            perAxisInput.historyAllocated_ = m_encoder.perAxisHistory_;
            perAxisInput.size_ = IRMath::ivec2(64);
            perAxisInput.storeFrame_ = IRMath::ivec4(0, 0, perAxisCardinal, 0);
            m_trixel->encodeFrame(m_encoder, m_encoder.liveImages_ == 3, perAxisInput);
        } else {
            m_trixel->endTick();
        }
        return m_log;
    }

    static IRComponents::C_GlitchSubject holdSubject() {
        IRComponents::C_GlitchSubject subject;
        subject.holdWeight_ = 1.0f;
        subject.spill_ = 2;
        return subject;
    }

    RenderOpLog m_log;
    TrixelGlitchRecordingEncoder m_encoder;
    RecordingRenderDevice m_device;
    IRRender::RenderDevice *m_previousDevice = nullptr;
    TrixelGlitchSystem *m_trixel = nullptr;
    std::vector<std::pair<IREntity::EntityId, IRComponents::C_GlitchSubject>> m_subjects;
    std::int64_t m_tickOffset = 0;
};

TEST_F(TrixelGlitchScheduleProbeTest, NoActiveSubjectDoesNoWorkAndZeroesItsTimingRow) {
    IRRender::gpuStageTiming().trixelGlitchMs_ = 0.37f;
    EXPECT_EQ(renderFrame(), RenderOpLog{}) << "no subject: no upload, copy, dispatch or swap";
    EXPECT_FLOAT_EQ(IRRender::gpuStageTiming().trixelGlitchMs_, 0.0f);

    IRComponents::C_GlitchSubject parked = holdSubject();
    parked.weight_ = 0.0f;
    m_subjects = {{41u, parked}, {42u, IRComponents::C_GlitchSubject{}}};
    IRRender::gpuStageTiming().trixelGlitchMs_ = 0.37f;
    EXPECT_EQ(renderFrame(), RenderOpLog{}) << "a zero weight or no effect is inactive";
    EXPECT_FLOAT_EQ(IRRender::gpuStageTiming().trixelGlitchMs_, 0.0f);
    EXPECT_FALSE(m_trixel->enabledThisFrame_);
    EXPECT_EQ(m_trixel->frameUploads_, 0);
    EXPECT_EQ(m_trixel->frameDispatches_, 0);
    EXPECT_EQ(m_trixel->frameSwaps_, 0);
    EXPECT_EQ(m_encoder.liveImages_, 0)
        << "the canvas gets no glitch image before the first enabled frame";
}

TEST_F(
    TrixelGlitchScheduleProbeTest, HoldSubjectCopiesHistoryOncePerPeriodTickAndAlternatesColour
) {
    m_subjects = {{57u, holdSubject()}};
    EXPECT_EQ(
        renderFrame(),
        (RenderOpLog{
            "trixel:createImages",
            "trixel:upload",
            "trixel:copyHistoryColors",
            "trixel:copyHistoryEntityIds",
            "trixel:dispatch read=1 write=2",
            "trixel:swap"
        })
    ) << "first enabled frame: three canvas images, both history planes, one dispatch and swap";
    EXPECT_EQ(m_encoder.liveImages_, 3);
    EXPECT_EQ(m_trixel->frameHistoryCopies_, 2);
    EXPECT_TRUE(m_trixel->historyValid_);

    EXPECT_EQ(
        renderFrame(),
        (RenderOpLog{"trixel:upload", "trixel:dispatch read=2 write=1", "trixel:swap"})
    ) << "the same period tick copies nothing; the colour planes alternate";

    EXPECT_EQ(
        renderFrame(false, 1),
        (RenderOpLog{
            "trixel:upload",
            "trixel:copyHistoryColors",
            "trixel:copyHistoryEntityIds",
            "trixel:dispatch read=1 write=2",
            "trixel:swap"
        })
    ) << "the next period tick re-snapshots once";

    EXPECT_EQ(
        renderFrame(true),
        (RenderOpLog{
            "trixel:createImages",
            "trixel:upload",
            "trixel:copyHistoryColors",
            "trixel:copyHistoryEntityIds",
            "trixel:dispatch read=1 write=2",
            "trixel:swap"
        })
    ) << "a canvas resize drops the images; they return and history reseeds on the same tick";
    EXPECT_EQ(m_trixel->imageCreationCount_, 2u);
    EXPECT_EQ(m_trixel->historyCopyCount_, 6u);
    EXPECT_EQ(m_trixel->dispatchCount_, 4u);
}

TEST_F(TrixelGlitchScheduleProbeTest, HistoryFollowsTheHoldSubjectsOnly) {
    IRComponents::C_GlitchSubject tear;
    tear.tearWeight_ = 1.0f;
    m_subjects = {{57u, tear}};
    EXPECT_EQ(
        renderFrame(),
        (RenderOpLog{
            "trixel:createImages",
            "trixel:upload",
            "trixel:dispatch read=1 write=2",
            "trixel:swap"
        })
    ) << "no hold subject: no history copy";
    EXPECT_FALSE(m_trixel->historyValid_);

    m_subjects.push_back({58u, holdSubject()});
    EXPECT_EQ(
        renderFrame(),
        (RenderOpLog{
            "trixel:upload",
            "trixel:copyHistoryColors",
            "trixel:copyHistoryEntityIds",
            "trixel:dispatch read=2 write=1",
            "trixel:swap"
        })
    ) << "hold turning on mid-tick takes a snapshot";

    m_subjects.clear();
    EXPECT_EQ(renderFrame(), RenderOpLog{});
    EXPECT_FALSE(m_trixel->historyValid_) << "an off frame voids the snapshot";
    EXPECT_EQ(m_encoder.liveImages_, 3) << "an off frame keeps the images";

    m_subjects = {{58u, holdSubject()}};
    EXPECT_EQ(
        renderFrame(),
        (RenderOpLog{
            "trixel:upload",
            "trixel:copyHistoryColors",
            "trixel:copyHistoryEntityIds",
            "trixel:dispatch read=1 write=2",
            "trixel:swap"
        })
    ) << "the snapshot from before the off frame is stale";
}

TEST_F(TrixelGlitchScheduleProbeTest, SubjectsUploadSortedByIdWithTheirSlots) {
    IRComponents::C_GlitchSubject tear;
    tear.tearWeight_ = 1.0f;
    m_subjects = {{90u, tear}, {12u, holdSubject()}, {55u, tear}};
    renderFrame();
    const std::vector<IRMath::uvec4> &words = m_encoder.lastWords_;
    ASSERT_FALSE(words.empty());
    EXPECT_EQ(words[0].w, 3u);
    EXPECT_EQ(IRPrefab::Glitch::lookupSubjectSlot(words, 12u), 0);
    EXPECT_EQ(IRPrefab::Glitch::lookupSubjectSlot(words, 55u), 1);
    EXPECT_EQ(IRPrefab::Glitch::lookupSubjectSlot(words, 90u), 2);
}

TEST_F(TrixelGlitchScheduleProbeTest, AllocatedPerAxisStoreDispatchesAndSwapsEveryAxis) {
    IRComponents::C_GlitchSubject tear;
    tear.tearWeight_ = 1.0f;
    m_subjects = {{57u, tear}};
    EXPECT_EQ(
        renderFrame(false, 0, true),
        (RenderOpLog{
            "trixel:createImages",
            "trixel:upload",
            "trixel:dispatch read=1 write=2",
            "trixel:swap",
            "trixel:perAxisCreateScratch",
            "trixel:perAxisDispatch:0",
            "trixel:perAxisDispatch:1",
            "trixel:perAxisDispatch:2",
            "trixel:perAxisBarrier",
            "trixel:perAxisSwap:0",
            "trixel:perAxisSwap:1",
            "trixel:perAxisSwap:2"
        })
    );
    EXPECT_EQ(m_trixel->framePerAxisDispatches_, 3);
    EXPECT_EQ(m_trixel->framePerAxisSwaps_, 3);
    EXPECT_EQ(m_trixel->perAxisScratchCreationCount_, 1u);

    const std::array<int, 3> distancesBefore = m_encoder.perAxisDistanceHandles_;
    const std::array<int, 3> idsBefore = m_encoder.perAxisIdHandles_;
    const RenderOpLog second = renderFrame(false, 0, true);
    EXPECT_EQ(std::count(second.begin(), second.end(), "trixel:perAxisCreateScratch"), 0);
    EXPECT_EQ(m_trixel->framePerAxisDispatches_, 3);
    EXPECT_EQ(m_trixel->framePerAxisSwaps_, 3);
    EXPECT_EQ(m_encoder.perAxisDistanceHandles_, distancesBefore);
    EXPECT_EQ(m_encoder.perAxisIdHandles_, idsBefore) << "only per-axis colour handles may swap";
}

TEST_F(TrixelGlitchScheduleProbeTest, PerAxisHoldCopiesOnTickAndCardinalChanges) {
    m_subjects = {{57u, holdSubject()}};
    renderFrame(false, 0, true, 0);
    EXPECT_EQ(m_trixel->framePerAxisHistoryCopies_, 6);
    EXPECT_EQ(m_trixel->perAxisHistoryCreationCount_, 1u);

    renderFrame(false, 0, true, 0);
    EXPECT_EQ(m_trixel->framePerAxisHistoryCopies_, 0);

    renderFrame(false, 1, true, 0);
    EXPECT_EQ(m_trixel->framePerAxisHistoryCopies_, 6);

    renderFrame(false, 0, true, 1);
    EXPECT_EQ(m_trixel->framePerAxisHistoryCopies_, 6)
        << "a store rebracket invalidates hold history inside the same period tick";
}

TEST_F(TrixelGlitchScheduleProbeTest, PerAxisRouteDoesNothingWhenAbsentOrDisarmed) {
    IRComponents::C_GlitchSubject tear;
    tear.tearWeight_ = 1.0f;
    m_subjects = {{57u, tear}};
    const RenderOpLog absent = renderFrame();
    EXPECT_EQ(
        std::count_if(
            absent.begin(),
            absent.end(),
            [](const std::string &op) { return op.find("perAxis") != std::string::npos; }
        ),
        0
    );

    m_trixel->perAxisDisabled_ = true;
    const RenderOpLog disarmed = renderFrame(false, 0, true);
    EXPECT_EQ(
        std::count_if(
            disarmed.begin(),
            disarmed.end(),
            [](const std::string &op) { return op.find("perAxis") != std::string::npos; }
        ),
        0
    );
    EXPECT_EQ(m_trixel->framePerAxisDispatches_, 0);
    EXPECT_EQ(m_trixel->framePerAxisSwaps_, 0);
}

// Both glitch passes resolve one period tick from one `C_GlitchState`, so a
// creation can put a subject glitch on tick N and a screen tear on tick N + 1.
TEST_F(PrefabSystemProbeTest, GlitchPassesResolveTheSamePeriodTick) {
    auto *framebuffer = IRSystem::getSystemParams<GlitchSystem>(registerGlitchParamsOnly());
    auto *trixel = IRSystem::getSystemParams<TrixelGlitchSystem>(registerTrixelGlitchParamsOnly());
    auto &state = IREntity::singleton<IRComponents::C_GlitchState>();
    state.periodSeconds_ = 0.25;
    state.phaseSeconds_ = -1.3;
    state.seed_ = 9u;
    state.intensity_ = 1.0f;
    state.tearWeight_ = 1.0f;

    framebuffer->beginTick();
    trixel->beginTick();
    ASSERT_TRUE(framebuffer->enabledThisFrame_);
    EXPECT_EQ(framebuffer->clock_.tick_, 5) << "floor((0 + 1.3) / 0.25)";
    EXPECT_EQ(trixel->clock_.tick_, framebuffer->clock_.tick_);

    IRComponents::C_GlitchSubject subject;
    subject.tearWeight_ = 1.0f;
    trixel->tick(7u, subject);
    ASSERT_TRUE(trixel->prepareFrame());
    const IRPrefab::Glitch::TickWords words = IRPrefab::Glitch::toTickWords(5);
    EXPECT_EQ(trixel->words_[0], IRMath::uvec4(9u, words.low_, words.high_, 1u))
        << "the subject buffer carries the framebuffer pass's seed and tick words";

    state.intensity_ = 0.0f;
    framebuffer->beginTick();
    trixel->beginTick();
    EXPECT_FALSE(framebuffer->enabledThisFrame_);
    EXPECT_EQ(trixel->clock_.tick_, IRPrefab::Glitch::clock().tick_)
        << "the subject pass keeps the clock while the screen pass is off";
}

} // namespace

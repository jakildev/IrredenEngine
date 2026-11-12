#include <gtest/gtest.h>

#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/modifier.hpp>
#include <irreden/common/modifier_field_registry.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_job.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/job/job_manager.hpp>
#include <irreden/profile/logger_spd.hpp>
#include <irreden/system/system_manager.hpp>
#include <irreden/time/time_manager.hpp>

#include <spdlog/sinks/ostream_sink.h>

#include <algorithm>
#include <atomic>
#include <list>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// MODIFIER_DECAY skips its rows while the framework's decay flag is clear.
// Each test drives the real system through an UPDATE pipeline and checks the
// flag, the skip, and that every expiry lands on the pass a bare `tick`
// (the ungated reference) would remove it on.

namespace {

using IRComponents::C_Modifiers;
using IRComponents::FieldBindingId;
using IRComponents::Modifier;
using IRComponents::TransformKind;
using IREntity::EntityId;

using DecaySystem = IRSystem::System<IRSystem::MODIFIER_DECAY>;

bool decayArmed() {
    return IRPrefab::Modifier::detail::globalFieldRegistry().decayArmed();
}

class ModifierDecayGateTest : public testing::Test {
  protected:
    ModifierDecayGateTest()
        : m_time_manager{}
        , m_entity_manager{}
        , m_system_manager{} {
        m_scalar = IRPrefab::Modifier::registerField("test.decay_gate.scalar");
        m_vec3 = IRPrefab::Modifier::registerFieldVec3("test.decay_gate.vec3");
        m_quat = IRPrefab::Modifier::registerFieldQuat("test.decay_gate.quat");
        m_source = IREntity::createEntity();
    }

    void registerPipeline(std::list<IRSystem::SystemId> trailing = {}) {
        m_decay = IRSystem::createSystem<IRSystem::MODIFIER_DECAY>();
        trailing.push_front(m_decay);
        m_system_manager.registerPipeline(IRTime::Events::UPDATE, trailing);
    }

    void pass() {
        m_system_manager.executePipeline(IRTime::Events::UPDATE);
    }

    // An idle pass over a world with no finite lifetime leaves the flag
    // clear, whatever earlier tests left behind.
    void settle() {
        pass();
        ASSERT_FALSE(decayArmed());
    }

    const DecaySystem &decay() const {
        return *IRSystem::getSystemParams<DecaySystem>(m_decay);
    }

    static C_Modifiers &mods(EntityId e) {
        return IREntity::getComponent<C_Modifiers>(e);
    }

    IRTime::TimeManager m_time_manager;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
    IRSystem::SystemId m_decay{};
    FieldBindingId m_scalar{IRComponents::kInvalidFieldId};
    FieldBindingId m_vec3{IRComponents::kInvalidFieldId};
    FieldBindingId m_quat{IRComponents::kInvalidFieldId};
    EntityId m_source{IREntity::kNullEntity};
};

TEST_F(ModifierDecayGateTest, IdlePassClearsFlagAndNextPassSkipsRows) {
    registerPipeline();
    auto target = IREntity::createEntity(C_Modifiers{});
    IRPrefab::Modifier::upsertBySource(target, m_scalar, TransformKind::ADD, 1.0f, m_source);
    IRPrefab::Modifier::detail::globalFieldRegistry().armDecay();

    pass();
    EXPECT_TRUE(decay().decayedLastPass_);
    EXPECT_FALSE(decayArmed());

    pass();
    EXPECT_FALSE(decay().decayedLastPass_);
    EXPECT_FALSE(decayArmed());
    EXPECT_EQ(mods(target).modifiers_.size(), 1u);
}

TEST_F(ModifierDecayGateTest, PushWithThreeTicksArmsAndExpiresOnTodaysPass) {
    registerPipeline();
    auto target = IREntity::createEntity(C_Modifiers{});
    settle();

    IRPrefab::Modifier::push(target, m_scalar, TransformKind::ADD, 1.0f, m_source, 3);
    EXPECT_TRUE(decayArmed());

    C_Modifiers reference = mods(target);
    DecaySystem ungated;
    for (int i = 1; i <= 3; ++i) {
        pass();
        ungated.tick(reference);
        EXPECT_TRUE(decay().decayedLastPass_) << "pass " << i;
        EXPECT_EQ(mods(target).modifiers_.size(), reference.modifiers_.size()) << "pass " << i;
    }
    EXPECT_TRUE(mods(target).modifiers_.empty());
    EXPECT_FALSE(decayArmed());

    pass();
    EXPECT_FALSE(decay().decayedLastPass_);
}

TEST_F(ModifierDecayGateTest, EveryTypedPushArms) {
    registerPipeline();
    auto target = IREntity::createEntity(C_Modifiers{});
    const IRMath::vec3 offset(1.0f, 0.0f, 0.0f);
    const IRMath::vec4 identity(0.0f, 0.0f, 0.0f, 1.0f);

    settle();
    IRPrefab::Modifier::push(target, m_vec3, TransformKind::ADD, offset, m_source, 2);
    EXPECT_TRUE(decayArmed());

    pass();
    pass();
    EXPECT_TRUE(mods(target).modifiersVec3_.empty());
    EXPECT_FALSE(decayArmed());
    IRPrefab::Modifier::push(target, m_quat, TransformKind::MULTIPLY, identity, m_source, 5);
    EXPECT_TRUE(decayArmed());
}

TEST_F(ModifierDecayGateTest, LastFiniteExpiryClearsWhileSteadyModifiersStay) {
    registerPipeline();
    auto target = IREntity::createEntity(C_Modifiers{});
    IRPrefab::Modifier::upsertBySource(target, m_scalar, TransformKind::ADD, 1.0f, m_source);
    settle();

    IRPrefab::Modifier::push(target, m_scalar, TransformKind::ADD, 2.0f, m_source, 2);
    IRPrefab::Modifier::push(target, m_scalar, TransformKind::ADD, 3.0f, m_source, 4);
    for (int i = 1; i <= 3; ++i) {
        pass();
        EXPECT_TRUE(decayArmed()) << "pass " << i;
    }
    pass();
    EXPECT_FALSE(decayArmed());
    ASSERT_EQ(mods(target).modifiers_.size(), 1u);
    EXPECT_EQ(mods(target).modifiers_[0].ticksRemaining_, -1);
}

TEST_F(ModifierDecayGateTest, UpsertsDoNotArm) {
    registerPipeline();
    auto target = IREntity::createEntity(C_Modifiers{});
    settle();

    IRPrefab::Modifier::upsertBySource(target, m_scalar, TransformKind::ADD, 1.0f, m_source);
    IRPrefab::Modifier::upsertBySource(
        target,
        m_vec3,
        TransformKind::ADD,
        IRMath::vec3(1.0f),
        m_source
    );
    IRPrefab::Modifier::upsertBySourceInPlace(
        mods(target),
        m_scalar,
        TransformKind::MULTIPLY,
        2.0f,
        m_source
    );
    IRPrefab::Modifier::upsertBySourceInPlace(
        mods(target),
        m_vec3,
        TransformKind::MULTIPLY,
        IRMath::vec3(2.0f),
        m_source
    );
    EXPECT_FALSE(decayArmed());
    pass();
    EXPECT_FALSE(decay().decayedLastPass_);
}

TEST_F(ModifierDecayGateTest, InPlacePushesArm) {
    registerPipeline();
    auto target = IREntity::createEntity(C_Modifiers{});
    const IRMath::vec3 offset(1.0f);
    const IRMath::vec4 identity(0.0f, 0.0f, 0.0f, 1.0f);

    const auto expectArms = [&](auto pushInPlace, int expectedLifetime) {
        settle();
        pushInPlace(mods(target));
        EXPECT_TRUE(decayArmed());
        for (int i = 0; i < expectedLifetime; ++i) {
            pass();
        }
        EXPECT_TRUE(mods(target).modifiers_.empty());
        EXPECT_TRUE(mods(target).modifiersVec3_.empty());
        EXPECT_TRUE(mods(target).modifiersQuat_.empty());
    };
    using IRPrefab::Modifier::pushFrameLocal;
    using IRPrefab::Modifier::pushOneFrame;
    const auto add = TransformKind::ADD;
    const auto mul = TransformKind::MULTIPLY;
    expectArms([&](C_Modifiers &m) { pushFrameLocal(m, m_scalar, add, 1.0f, m_source); }, 1);
    expectArms([&](C_Modifiers &m) { pushFrameLocal(m, m_vec3, add, offset, m_source); }, 1);
    expectArms([&](C_Modifiers &m) { pushFrameLocal(m, m_quat, mul, identity, m_source); }, 1);
    expectArms([&](C_Modifiers &m) { pushOneFrame(m, m_scalar, add, 1.0f, m_source); }, 2);
    expectArms([&](C_Modifiers &m) { pushOneFrame(m, m_vec3, add, offset, m_source); }, 2);
    expectArms([&](C_Modifiers &m) { pushOneFrame(m, m_quat, mul, identity, m_source); }, 2);
}

// Workers push in place from a PARALLEL_FOR tick that runs after decay in the
// same pass; the next passes decay the pushes on the workers that hold them.
class ModifierDecayGateParallelTest : public ModifierDecayGateTest {
  protected:
    ModifierDecayGateParallelTest()
        : m_jobs{std::make_unique<IRJob::JobManager>(2)} {}

    std::unique_ptr<IRJob::JobManager> m_jobs;
};

TEST_F(ModifierDecayGateParallelTest, InPlacePushFromWorkerTickArms) {
    constexpr int kRows = 4096;
    std::atomic<bool> pushNow{false};
    std::atomic<int> offMainPushes{0};
    const FieldBindingId field = m_scalar;
    const EntityId source = m_source;
    auto pusher = IRSystem::createSystem<C_Modifiers>(
        "TestDecayGateWorkerPush",
        [&pushNow, &offMainPushes, field, source](C_Modifiers &m) {
            if (!pushNow.load(std::memory_order_relaxed) || IRJob::isMainThread()) {
                return;
            }
            IRPrefab::Modifier::pushOneFrame(m, field, TransformKind::ADD, 1.0f, source);
            offMainPushes.fetch_add(1, std::memory_order_relaxed);
        },
        nullptr,
        nullptr,
        IRSystem::RelationParams<>{},
        nullptr,
        IRSystem::Concurrency::PARALLEL_FOR,
        64
    );
    registerPipeline({pusher});
    std::vector<EntityId> rows;
    rows.reserve(kRows);
    for (int i = 0; i < kRows; ++i) {
        rows.push_back(IREntity::createEntity(C_Modifiers{}));
    }
    settle();

    pushNow = true;
    pass();
    pushNow = false;
    ASSERT_GT(offMainPushes.load(), 0) << "no pusher chunk ran on a worker";
    EXPECT_TRUE(decayArmed());

    const auto pushed =
        static_cast<std::size_t>(std::count_if(rows.begin(), rows.end(), [](EntityId e) {
            return !mods(e).modifiers_.empty();
        }));
    EXPECT_EQ(pushed, static_cast<std::size_t>(offMainPushes.load()));

    pass(); // 2 -> 1, kept on whichever executor decays the row
    EXPECT_TRUE(decayArmed());
    EXPECT_EQ(
        static_cast<std::size_t>(std::count_if(
            rows.begin(),
            rows.end(),
            [](EntityId e) { return !mods(e).modifiers_.empty(); }
        )),
        pushed
    );

    pass(); // expires everywhere
    EXPECT_FALSE(decayArmed());
    for (EntityId e : rows) {
        EXPECT_TRUE(mods(e).modifiers_.empty());
    }
}

#ifndef IR_RELEASE

// Positive control for the arming census: a finite lifetime written straight
// into a vector while the flag is clear fails the next pass on the main
// thread, names the push functions, and arms the flag so the pass after it
// decays the stray modifier.
TEST_F(ModifierDecayGateTest, UnarmedFiniteLifetimeFailsTheNextPass) {
    registerPipeline();
    auto target = IREntity::createEntity(C_Modifiers{});
    settle();

    mods(target).modifiers_.push_back(Modifier{m_scalar, TransformKind::ADD, 1.0f, m_source, 3});

    std::ostringstream captured;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_st>(captured);
    auto *logger = LoggerSpd::instance()->getGLAPILogger();
    logger->sinks().push_back(sink);
    struct SinkGuard {
        spdlog::logger *logger_;
        std::shared_ptr<spdlog::sinks::sink> sink_;
        ~SinkGuard() {
            auto &sinks = logger_->sinks();
            sinks.erase(std::remove(sinks.begin(), sinks.end(), sink_), sinks.end());
        }
    } guard{logger, sink};

    EXPECT_THROW(pass(), std::runtime_error);
    const std::string logged = captured.str();
    EXPECT_NE(logged.find("MODIFIER_DECAY"), std::string::npos) << logged;
    EXPECT_NE(logged.find("pushFrameLocal or pushOneFrame"), std::string::npos) << logged;

    ASSERT_EQ(mods(target).modifiers_.size(), 1u);
    EXPECT_EQ(mods(target).modifiers_[0].ticksRemaining_, 3);
    EXPECT_TRUE(decayArmed());

    pass();
    EXPECT_TRUE(decay().decayedLastPass_);
    EXPECT_EQ(mods(target).modifiers_[0].ticksRemaining_, 2);
}

#endif

} // namespace

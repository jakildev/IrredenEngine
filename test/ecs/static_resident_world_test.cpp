#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/components/component_static_state.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/common/modifier.hpp>
#include <irreden/common/rotation_mode.hpp>
#include <irreden/common/static_audit.hpp>
#include <irreden/common/static_residents.hpp>
#include <irreden/common/transform_modifier_fields.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/render/components/component_active_lod_level.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_reveal_settings.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/components/component_lod_tier_override.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/gizmo.hpp>
#include <irreden/render/systems/system_fog_reveal_eval.hpp>
#include <irreden/render/systems/system_fog_subject_adopt.hpp>
#include <irreden/render/systems/system_gate_voxel_sets_by_lod.hpp>
#include <irreden/system/components/component_static.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/components/component_skeleton.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>
#include <irreden/voxel/systems/system_seed_staged_voxels.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/world/main_loop_frame.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <list>
#include <stdexcept>
#include <string>
#include <vector>

// Static residents against a whole pipeline: the cutover rules of gate 4
// and the forbidden-write, mutator, trigger and fog rules of gates 5 and 9
// in docs/design/static-residents.md. Every pipeline system is followed by
// a probe, so a test can act between two dispatches of one pass.

namespace {

using IRComponents::C_ActiveLodLevel;
using IRComponents::C_CanvasFogOfWar;
using IRComponents::C_FogRevealed;
using IRComponents::C_FogRevealSettings;
using IRComponents::C_GlobalModifiers;
using IRComponents::C_LambdaModifiers;
using IRComponents::C_LocalTransform;
using IRComponents::C_LodTierOverride;
using IRComponents::C_Modifiers;
using IRComponents::C_ResolvedFields;
using IRComponents::C_Settled;
using IRComponents::C_Static;
using IRComponents::C_StaticState;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::C_WorldTransform;
using IRComponents::TransformKind;
using IREntity::EntityId;
using IRMath::Color;
using IRMath::ivec3;
using IRMath::vec3;
using IRRender::LodLevel;
using IRSystem::Exclude;
using IRSystem::SystemId;
namespace Static = IRPrefab::Static;
namespace Modifier = IRPrefab::Modifier;
namespace Fog = IRPrefab::Fog;

struct C_Mover {};
struct C_ProbeNever {};

constexpr vec3 kMove{2.0f, 5.0f, -1.0f};
constexpr vec3 kGlobalOffset{1.0f, 2.0f, 3.0f};
constexpr ivec3 kSetSize{2, 1, 2};

// The probe slot after each system, in pipeline order.
enum Slot {
    AFTER_LOD_GATE,
    AFTER_MODIFIER_DECAY,
    AFTER_GLOBAL_DECAY,
    AFTER_FIELD_SEED,
    AFTER_RESOLVE,
    AFTER_MOVER,
    AFTER_PROPAGATE,
    AFTER_ADOPT,
    AFTER_EVAL,
    AFTER_EVAL_GHOST,
    AFTER_SEED_STAGED,
    // After the last RENDER system, the grid rebuild that follows
    // UPDATE_VOXEL_SET_CHILDREN: the round has closed by then.
    AFTER_CHILDREN,
    SLOT_COUNT,
};

// The slots that follow a deriving system.
constexpr Slot kAfterDeriving[] = {
    AFTER_LOD_GATE,
    AFTER_FIELD_SEED,
    AFTER_RESOLVE,
    AFTER_PROPAGATE,
    AFTER_ADOPT,
    AFTER_EVAL,
    AFTER_EVAL_GHOST,
    AFTER_SEED_STAGED,
    AFTER_CHILDREN,
};

struct Harness {
    IREntity::EntityManager entities_;
    IRSystem::SystemManager systems_;
    EntityId canvas_ = IREntity::kNullEntity;
    IRComponents::FieldBindingId translation_ = IRComponents::kInvalidFieldId;
    SystemId mover_ = IRSystem::kNullSystemId;
    SystemId lodGate_ = IRSystem::kNullSystemId;
    SystemId eval_ = IRSystem::kNullSystemId;
    SystemId adopt_ = IRSystem::kNullSystemId;
    std::vector<std::function<void()>> hooks_;
    float nextX_ = -12.0f;

    Harness()
        : hooks_(SLOT_COUNT) {
        canvas_ = IREntity::createEntity(C_VoxelPool{ivec3(32, 32, 8)});
        IRRender::setHeadlessActiveCanvasEntity(canvas_);
        IREntity::singletonEntity<C_GlobalModifiers>();
        translation_ = IRPrefab::TransformModifier::translationField();
        const IRComponents::FieldBindingId translation = translation_;

        lodGate_ = IRSystem::createSystem<IRSystem::GATE_VOXEL_SETS_BY_LOD>();
        mover_ = IRSystem::createSystem<C_LocalTransform, C_Mover>(
            "TestMover",
            [](C_LocalTransform &local, C_Mover &) { local.translation_ += kMove; }
        );
        adopt_ = IRSystem::createSystem<IRSystem::FOG_SUBJECT_ADOPT>();
        eval_ = IRSystem::createSystem<IRSystem::FOG_REVEAL_EVAL>();
        std::list<SystemId> update;
        const auto add = [this, &update](SystemId system, Slot slot) {
            update.push_back(system);
            update.push_back(probe(slot));
        };
        add(lodGate_, AFTER_LOD_GATE);
        add(IRSystem::createSystem<IRSystem::MODIFIER_DECAY>(), AFTER_MODIFIER_DECAY);
        add(IRSystem::createSystem<IRSystem::GLOBAL_MODIFIER_DECAY>(), AFTER_GLOBAL_DECAY);
        add(
            IRSystem::createSystem<C_ResolvedFields, Exclude<C_Settled>>(
                "TestFieldSeed",
                [translation](C_ResolvedFields &resolved) {
                    resolved.resetVec3(translation, vec3(0.0f));
                }
            ),
            AFTER_FIELD_SEED
        );
        add(IRSystem::createSystem<IRSystem::MODIFIER_RESOLVE_GLOBAL>(), AFTER_RESOLVE);
        add(mover_, AFTER_MOVER);
        add(IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(), AFTER_PROPAGATE);
        add(adopt_, AFTER_ADOPT);
        add(eval_, AFTER_EVAL);
        add(IRSystem::createSystem<IRSystem::FOG_REVEAL_EVAL_GHOST>(), AFTER_EVAL_GHOST);
        add(IRSystem::createSystem<IRSystem::SEED_STAGED_VOXELS>(), AFTER_SEED_STAGED);
        systems_.registerPipeline(IRTime::Events::UPDATE, update);
        systems_.registerPipeline(
            IRTime::Events::RENDER,
            {IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
             IRSystem::createSystem<IRSystem::REBUILD_GRID_VOXELS>(),
             IRSystem::createSystem<IRSystem::REBUILD_GRID_VOXELS_IMPLICIT>(),
             probe(AFTER_CHILDREN)}
        );
    }

    ~Harness() {
        IRRender::setHeadlessActiveCanvasEntity(IREntity::kNullEntity);
    }

    SystemId probe(Slot slot) {
        return IRSystem::createSystem<C_ProbeNever>(
            "Probe" + std::to_string(static_cast<int>(slot)),
            [](C_ProbeNever &) {},
            [this, slot]() {
                if (hooks_[slot]) {
                    std::function<void()> hook = std::move(hooks_[slot]);
                    hooks_[slot] = nullptr;
                    hook();
                }
            }
        );
    }

    // Runs `hook` once, right after the system `slot` follows next dispatches.
    void after(Slot slot, std::function<void()> hook) {
        hooks_[slot] = std::move(hook);
    }

    EntityId makeBody(bool isStatic) {
        const vec3 position{nextX_, 3.0f, 1.0f};
        nextX_ += 4.0f;
        const C_VoxelSetNew set{kSetSize, Color{200, 120, 60, 255}, false, canvas_};
        return isStatic ? IREntity::createEntity(
                              C_LocalTransform{position},
                              set,
                              C_Modifiers{},
                              C_ResolvedFields{},
                              C_FogRevealed{},
                              C_Static{}
                          )
                        : IREntity::createEntity(
                              C_LocalTransform{position},
                              set,
                              C_Modifiers{},
                              C_ResolvedFields{},
                              C_FogRevealed{}
                          );
    }

    // A static body and its always-dynamic twin, both shifted by the same
    // offset from then on so their voxels never share a cell.
    struct Pair {
        EntityId entity_;
        EntityId twin_;
    };
    Pair makePair() {
        const EntityId entity = makeBody(true);
        const EntityId twin = makeBody(false);
        return {entity, twin};
    }

    void update() {
        systems_.executePipeline(IRTime::Events::UPDATE);
        Static::drain();
    }
    void render() {
        systems_.executePipeline(IRTime::Events::RENDER);
        Static::drain();
    }
    void frame() {
        update();
        Static::audit();
        render();
    }
    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            frame();
        }
    }
    // Frames until `entity` is settled, bounded.
    int framesUntilSettled(EntityId entity, int limit = 12) {
        for (int i = 1; i <= limit; ++i) {
            frame();
            if (Static::isSettled(entity)) {
                return i;
            }
        }
        return -1;
    }

    C_VoxelPool &pool() {
        return IREntity::getComponent<C_VoxelPool>(canvas_);
    }
    C_VoxelSetNew &set(EntityId entity) {
        return IREntity::getComponent<C_VoxelSetNew>(entity);
    }
    std::uint64_t stamp(EntityId entity) {
        return IREntity::getComponent<C_Static>(entity).settleStamp_;
    }
    Static::Stats stats() {
        return Static::stats();
    }

    // Local position, relative to the entity, of each voxel.
    std::vector<vec3> relativeGlobals(EntityId entity) {
        const C_VoxelSetNew &voxels = set(entity);
        const vec3 origin = IREntity::getComponent<C_WorldTransform>(entity).translation_;
        std::vector<vec3> out;
        for (int v = 0; v < voxels.numVoxels_; ++v) {
            const std::size_t slot = voxels.voxelStartIdx_ + static_cast<std::size_t>(v);
            out.push_back(pool().getPositionGlobals()[slot].pos_ - origin);
        }
        return out;
    }

    std::vector<std::uint32_t> gateBits(EntityId entity) {
        const C_VoxelSetNew &voxels = set(entity);
        std::vector<std::uint32_t> out;
        for (int v = 0; v < voxels.numVoxels_; ++v) {
            const std::size_t slot = voxels.voxelStartIdx_ + static_cast<std::size_t>(v);
            const std::uint32_t active = (pool().getActiveMask()[slot / 32] >> (slot % 32)) & 1u;
            out.push_back(active);
            out.push_back(
                pool().getColors()[slot].reserved_ & IRComponents::VoxelReserved::kFogCarrierMask
            );
        }
        return out;
    }

    // The twin sits four cells along x from the entity for its whole life,
    // so every derived quantity must match once that offset is removed.
    void expectTwin(const Pair &pair, const std::string &step) {
        const C_WorldTransform &world = IREntity::getComponent<C_WorldTransform>(pair.entity_);
        const C_WorldTransform &twin = IREntity::getComponent<C_WorldTransform>(pair.twin_);
        EXPECT_EQ(world.translation_ + vec3(4.0f, 0.0f, 0.0f), twin.translation_) << step;
        EXPECT_EQ(world.rotation_, twin.rotation_) << step;
        EXPECT_EQ(world.scale_, twin.scale_) << step;
        EXPECT_EQ(relativeGlobals(pair.entity_), relativeGlobals(pair.twin_)) << step;
        EXPECT_FALSE(relativeGlobals(pair.entity_).empty()) << step;
        EXPECT_EQ(set(pair.entity_).lodCulled_, set(pair.twin_).lodCulled_) << step;
        EXPECT_EQ(set(pair.entity_).visible_, set(pair.twin_).visible_) << step;
        EXPECT_EQ(set(pair.entity_).ghostHeld_, set(pair.twin_).ghostHeld_) << step;
        EXPECT_EQ(
            IREntity::getComponent<C_FogRevealed>(pair.entity_).shown_,
            IREntity::getComponent<C_FogRevealed>(pair.twin_).shown_
        ) << step;
        EXPECT_EQ(gateBits(pair.entity_), gateBits(pair.twin_)) << step;
    }

    Pair makeSettledPair() {
        const Pair pair = makePair();
        frames(2);
        EXPECT_TRUE(Static::isSettled(pair.entity_));
        return pair;
    }
};

// ---- Gate 4: cutover -----------------------------------------------------

TEST(StaticWorld, FreezePromotesAfterExactlyOneCompleteRound) {
    Harness h;
    const EntityId entity = h.makeBody(false);
    h.frames(3);
    ASSERT_EQ(Static::freeze(entity), Static::FreezeResult::FROZEN);
    // The freeze applies at the first flush of the next pass, mid-round, so
    // that round does not count and the next complete one promotes.
    h.frame();
    EXPECT_TRUE(Static::isStatic(entity));
    EXPECT_FALSE(Static::isSettled(entity));
    EXPECT_EQ(h.stamp(entity), h.systems_.roundsCompleted() + 1);
    h.frame();
    EXPECT_TRUE(Static::isSettled(entity));
}

void expectMidRoundChangeRestartsTheWait(
    const std::function<void(Harness &, const Harness::Pair &)> &change
) {
    for (const Slot slot : kAfterDeriving) {
        Harness h;
        const Harness::Pair pair = h.makePair();
        h.frame();
        ASSERT_FALSE(Static::isSettled(pair.entity_));
        const std::string where = "after slot " + std::to_string(static_cast<int>(slot));

        h.after(slot, [&]() { change(h, pair); });
        h.update();
        if (slot == AFTER_CHILDREN) {
            h.render();
        }
        EXPECT_FALSE(Static::isSettled(pair.entity_)) << where;
        EXPECT_EQ(h.stamp(pair.entity_), h.systems_.roundsOpened() + 1) << where;
        if (slot != AFTER_CHILDREN) {
            h.render();
        }
        EXPECT_FALSE(Static::isSettled(pair.entity_))
            << where << ": the round that was open when the input changed never counts";
        h.frame();
        EXPECT_TRUE(Static::isSettled(pair.entity_)) << where;
        h.expectTwin(pair, where);
    }
}

TEST(StaticWorld, MidRoundRawWriteAndResettleRestartsTheWait) {
    expectMidRoundChangeRestartsTheWait([](Harness &, const Harness::Pair &pair) {
        IREntity::getComponent<C_LocalTransform>(pair.entity_).translation_ += kMove;
        IREntity::getComponent<C_LocalTransform>(pair.twin_).translation_ += kMove;
        Static::resettle(pair.entity_);
    });
}

TEST(StaticWorld, MidRoundVoxelEditRestartsTheWait) {
    expectMidRoundChangeRestartsTheWait([](Harness &h, const Harness::Pair &pair) {
        h.set(pair.entity_).changeVoxelColorAll(Color{10, 20, 30, 255});
        h.set(pair.twin_).changeVoxelColorAll(Color{10, 20, 30, 255});
    });
}

TEST(StaticWorld, MidRoundLodBandChangeRestartsTheWait) {
    expectMidRoundChangeRestartsTheWait([](Harness &h, const Harness::Pair &pair) {
        h.set(pair.entity_).setLodBand(LodLevel::LOD_0, LodLevel::LOD_0);
        h.set(pair.twin_).setLodBand(LodLevel::LOD_0, LodLevel::LOD_0);
    });
}

TEST(StaticWorld, MidRoundFogSetterRestartsTheWait) {
    expectMidRoundChangeRestartsTheWait([](Harness &, const Harness::Pair &pair) {
        Fog::setOverride(pair.entity_, IRComponents::FogOverride::FORCE_HIDDEN);
        Fog::setOverride(pair.twin_, IRComponents::FogOverride::FORCE_HIDDEN);
    });
}

TEST(StaticWorld, ResettleOnASettlingEntityMovesPromotionOneRoundLater) {
    Harness h;
    const EntityId control = h.makeBody(true);
    const EntityId resettled = h.makeBody(true);
    h.frame();
    h.after(AFTER_PROPAGATE, [&]() { Static::resettle(resettled); });
    h.frame();
    EXPECT_TRUE(Static::isSettled(control));
    EXPECT_FALSE(Static::isSettled(resettled));
    EXPECT_EQ(h.stats().counters_.restarted_, 1u);
    h.frame();
    EXPECT_TRUE(Static::isSettled(resettled));
}

void expectGlobalPushHoldsPromotion(Slot slot) {
    Harness h;
    const Harness::Pair pair = h.makePair();
    h.frame();
    // In this frame the entity's own stamp is satisfied; only the floor a
    // global change raises can hold it.
    h.after(slot, [&]() {
        Modifier::pushGlobal(h.translation_, TransformKind::ADD, kGlobalOffset, 0);
    });
    h.frame();
    EXPECT_FALSE(Static::isSettled(pair.entity_)) << "slot " << static_cast<int>(slot);
    EXPECT_GE(h.stats().counters_.heldByGlobal_, 1u);
    h.frame();
    EXPECT_TRUE(Static::isSettled(pair.entity_));
    const vec3 resolved = IREntity::getComponent<C_ResolvedFields>(pair.entity_)
                              .getVec3(h.translation_, vec3(0.0f));
    EXPECT_EQ(resolved, kGlobalOffset) << "the promoted values include the global";
    h.expectTwin(pair, "global push");
}

TEST(StaticWorld, GlobalPushAfterTheResolverHoldsPromotion) {
    expectGlobalPushHoldsPromotion(AFTER_RESOLVE);
}

TEST(StaticWorld, GlobalPushAfterTheRoundClosedHoldsPromotion) {
    expectGlobalPushHoldsPromotion(AFTER_CHILDREN);
}

TEST(StaticWorld, GlobalExpiryWhileSettlingHoldsPromotion) {
    Harness h;
    const Harness::Pair pair = h.makePair();
    Modifier::pushGlobal(h.translation_, TransformKind::ADD, kGlobalOffset, 0, 2);
    h.frame();
    h.frame();
    EXPECT_FALSE(Static::isSettled(pair.entity_)) << "the global expired in this frame's decay";
    h.frame();
    EXPECT_TRUE(Static::isSettled(pair.entity_));
    EXPECT_EQ(
        IREntity::getComponent<C_ResolvedFields>(pair.entity_).getVec3(h.translation_, vec3(9.0f)),
        vec3(0.0f)
    );
    h.expectTwin(pair, "global expiry");
}

TEST(StaticWorld, AGlobalPushedEveryUpdateHoldsEverySettlingEntity) {
    Harness h;
    const Harness::Pair pair = h.makePair();
    for (int i = 0; i < 6; ++i) {
        Modifier::pushGlobal(h.translation_, TransformKind::ADD, kGlobalOffset, 0, 2);
        h.frame();
        EXPECT_EQ(h.stats().settled_, 0u) << "frame " << i;
    }
    EXPECT_GE(h.stats().counters_.heldByGlobal_, 4u);
    EXPECT_GT(h.framesUntilSettled(pair.entity_), 0);
    h.expectTwin(pair, "after the pushes stop");
}

TEST(StaticWorld, AStagedSetIsHeldUntilItIsSeeded) {
    Harness h;
    const std::vector<IRComponents::C_Voxel> records(4, IRComponents::C_Voxel{Color{9, 9, 9, 255}});
    IRRender::setHeadlessActiveCanvasEntity(IREntity::kNullEntity);
    const EntityId entity = IREntity::createEntity(
        C_LocalTransform{vec3(1.0f, 2.0f, 0.0f)},
        C_VoxelSetNew{
            C_VoxelSetNew::StagedInit{},
            kSetSize,
            ivec3(0),
            records,
            IREntity::kNullEntity
        },
        C_Static{}
    );
    h.frame();
    EXPECT_EQ(h.set(entity).ownerEntityId_, entity)
        << "a staged set, as a snapshot load leaves it, is adopted at the first drain";
    h.frames(3);
    EXPECT_FALSE(Static::isSettled(entity));
    EXPECT_GE(h.stats().counters_.held_, 3u);
    EXPECT_EQ(h.set(entity).numVoxels_, 0);

    IRRender::setHeadlessActiveCanvasEntity(h.canvas_);
    EXPECT_EQ(h.framesUntilSettled(entity), 2) << "the first complete round after the seed";
    EXPECT_GT(h.set(entity).numVoxels_, 0);
    EXPECT_EQ(h.set(entity).ownerEntityId_, entity);
}

TEST(StaticWorld, AppendingADerivingSystemMidRoundDelaysPromotionUntilItHasRun) {
    Harness h;
    const EntityId entity = h.makeBody(true);
    h.frame();
    int visitsBeforePromotion = 0;
    h.update();
    const SystemId late = IRSystem::createSystem<const C_WorldTransform, C_Static, Exclude<C_Settled>>(
        "LateDeriving",
        [&visitsBeforePromotion](const C_WorldTransform &, C_Static &) {
            ++visitsBeforePromotion;
        }
    );
    IRSystem::appendToPipeline(IRTime::Events::UPDATE, late);
    h.render();
    EXPECT_FALSE(Static::isSettled(entity)) << "the abandoned round does not promote";
    EXPECT_EQ(visitsBeforePromotion, 0);
    h.frame();
    EXPECT_TRUE(Static::isSettled(entity));
    EXPECT_EQ(visitsBeforePromotion, 1) << "the promoting round included the new system";
}

TEST(StaticWorld, MutationRightAfterPromotionLeavesTheEntitySettling) {
    Harness h;
    const Harness::Pair resettled = h.makeSettledPair();
    bool settledAfterNextFlush = true;
    h.after(AFTER_LOD_GATE, [&]() { Static::resettle(resettled.entity_); });
    h.after(AFTER_MODIFIER_DECAY, [&]() {
        settledAfterNextFlush = Static::isSettled(resettled.entity_);
    });
    h.update();
    EXPECT_FALSE(settledAfterNextFlush) << "a resettle applies at the next group flush";
    h.render();
    h.frame();
    EXPECT_TRUE(Static::isSettled(resettled.entity_));
    h.expectTwin(resettled, "resettle after promotion");

    const Harness::Pair edited = h.makeSettledPair();
    bool settledBeforeDrain = false;
    h.after(AFTER_LOD_GATE, [&]() {
        h.set(edited.entity_).changeVoxelColorAll(Color{1, 2, 3, 255});
        h.set(edited.twin_).changeVoxelColorAll(Color{1, 2, 3, 255});
    });
    h.after(AFTER_SEED_STAGED, [&]() { settledBeforeDrain = Static::isSettled(edited.entity_); });
    h.update();
    EXPECT_TRUE(settledBeforeDrain) << "a component-method note applies at the drain";
    EXPECT_FALSE(Static::isSettled(edited.entity_));
    h.render();
    h.frame();
    EXPECT_TRUE(Static::isSettled(edited.entity_));
    h.expectTwin(edited, "voxel edit after promotion");
}

// Freeze, settle, resettle, settle, with a global push in between; returns
// the frame on which each settle happened.
std::vector<int> runCounterScenario(std::uint64_t seed, std::uint64_t &stampOut, std::uint64_t &floorOut) {
    Harness h;
    h.systems_.seedRoundCountersForTest(seed);
    const EntityId entity = h.makeBody(false);
    h.frame();
    Static::freeze(entity);
    std::vector<int> settledOn;
    int frame = 0;
    const auto runUntilSettled = [&]() {
        for (int i = 0; i < 12; ++i) {
            h.frame();
            ++frame;
            if (Static::isSettled(entity)) {
                settledOn.push_back(frame);
                return;
            }
        }
    };
    runUntilSettled();
    Modifier::pushGlobal(h.translation_, TransformKind::ADD, kGlobalOffset, 0);
    Static::resettle(entity);
    h.frame();
    ++frame;
    stampOut = h.stamp(entity);
    floorOut = IREntity::singleton<C_StaticState>().globalFloor_;
    runUntilSettled();
    return settledOn;
}

TEST(StaticWorld, StampsFloorAndCountersCrossThirtyTwoBitsWithoutChangingBehaviour) {
    std::uint64_t stamp = 0;
    std::uint64_t floor = 0;
    const std::vector<int> unseeded = runCounterScenario(0, stamp, floor);
    const std::uint64_t boundary = std::uint64_t{1} << 32;
    const std::vector<int> seeded = runCounterScenario(boundary - 3, stamp, floor);
    ASSERT_EQ(unseeded.size(), 2u);
    EXPECT_EQ(seeded, unseeded);
    EXPECT_GT(stamp, boundary);
    EXPECT_GT(floor, boundary);
}

TEST(StaticWorld, ARotatedGridEntityIsRasterizedWhenItSettlesAndMatchesItsTwin) {
    Harness h;
    const Harness::Pair pair = h.makePair();
    const IRMath::vec4 quarterTurn = IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), IRMath::kHalfPi);
    for (const EntityId entity : {pair.entity_, pair.twin_}) {
        IREntity::getComponent<C_LocalTransform>(entity).rotation_ = quarterTurn;
    }
    h.frames(3);
    ASSERT_TRUE(Static::isSettled(pair.entity_));
    EXPECT_EQ(h.set(pair.entity_).allocatedVoxels_, h.set(pair.twin_).allocatedVoxels_);
    h.expectTwin(pair, "rotated and settled");
    h.frames(3);
    h.expectTwin(pair, "later frames");
}

// A set that cannot render keeps whatever raster its pool span last held. A
// span still holding a rotated raster is not the identity raster, so the
// entity may not settle until the rebuild has restored it.
TEST(StaticWorld, AHiddenSetStillHoldingARotatedRasterIsHeldUntilItIsShown) {
    Harness h;
    const Harness::Pair pair{h.makeBody(false), h.makeBody(false)};
    const IRMath::vec4 quarterTurn = IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), IRMath::kHalfPi);
    for (const EntityId entity : {pair.entity_, pair.twin_}) {
        h.set(entity).setLodBand(LodLevel::LOD_4, LodLevel::LOD_3);
        IREntity::getComponent<C_LocalTransform>(entity).rotation_ = quarterTurn;
    }
    h.frames(2);
    ASSERT_FALSE(h.set(pair.entity_).rotationSourceVoxels_.empty());

    const LodLevel shownTier = IREntity::singleton<C_ActiveLodLevel>().current_;
    IREntity::singleton<C_ActiveLodLevel>().current_ = LodLevel::LOD_0;
    for (const EntityId entity : {pair.entity_, pair.twin_}) {
        IREntity::getComponent<C_LocalTransform>(entity).rotation_ = IRMath::vec4(0, 0, 0, 1);
    }
    h.frame();
    ASSERT_TRUE(h.set(pair.entity_).lodCulled_);
    ASSERT_FALSE(h.set(pair.entity_).rotationSourceVoxels_.empty())
        << "the culled set was not rasterized back to identity";

    ASSERT_EQ(Static::freeze(pair.entity_), Static::FreezeResult::FROZEN);
    h.frames(4);
    EXPECT_FALSE(Static::isSettled(pair.entity_));
    EXPECT_GT(h.stats().counters_.held_, 0u);

    IREntity::singleton<C_ActiveLodLevel>().current_ = shownTier;
    h.frames(3);
    EXPECT_TRUE(Static::isSettled(pair.entity_));
    EXPECT_TRUE(h.set(pair.entity_).rotationSourceVoxels_.empty());
    h.expectTwin(pair, "shown again at identity");
}

TEST(StaticWorld, AGlobalModifierPushedAfterSettlingDoesNotReachTheEntity) {
    Harness h;
    const Harness::Pair pair = h.makeSettledPair();
    Modifier::pushGlobal(h.translation_, TransformKind::ADD, kGlobalOffset, 0);
    h.frames(3);
    EXPECT_TRUE(Static::isSettled(pair.entity_));
    const auto resolved = [&h](EntityId entity) {
        return IREntity::getComponent<C_ResolvedFields>(entity).getVec3(h.translation_, vec3(9.0f));
    };
    EXPECT_EQ(resolved(pair.entity_), vec3(0.0f)) << "the settled values are those of its round";
    EXPECT_EQ(resolved(pair.twin_), kGlobalOffset);

    Static::thaw(pair.entity_);
    h.frames(2);
    EXPECT_EQ(resolved(pair.entity_), kGlobalOffset) << "a thawed entity resolves it again";
    h.expectTwin(pair, "thawed under a global");
}

TEST(StaticWorld, AStaticEntitysModifierLifetimesDoNotAdvance) {
    Harness h;
    const Harness::Pair pair = h.makePair();
    for (const EntityId entity : {pair.entity_, pair.twin_}) {
        IREntity::getComponent<C_Modifiers>(entity).modifiersVec3_.push_back(
            IRComponents::ModifierVec3{h.translation_, TransformKind::ADD, kMove, 0, 3}
        );
    }
    h.frames(6);
    const auto &frozen = IREntity::getComponent<C_Modifiers>(pair.entity_).modifiersVec3_;
    ASSERT_EQ(frozen.size(), 1u) << "MODIFIER_DECAY never matches a static entity";
    EXPECT_EQ(frozen[0].ticksRemaining_, 3);
    EXPECT_TRUE(IREntity::getComponent<C_Modifiers>(pair.twin_).modifiersVec3_.empty());
}

// ---- Parents and children ------------------------------------------------

TEST(StaticWorld, StaticChildOfStaticParentAndDynamicChildMatchTheirTwins) {
    Harness h;
    const Harness::Pair parents = h.makePair();
    const Harness::Pair staticChild = h.makePair();
    const Harness::Pair dynamicChild{h.makeBody(false), h.makeBody(false)};
    for (const Harness::Pair &child : {staticChild, dynamicChild}) {
        IREntity::setParent(child.entity_, parents.entity_);
        IREntity::setParent(child.twin_, parents.twin_);
    }
    const auto expectChildren = [&](const char *step) {
        for (const Harness::Pair &child : {staticChild, dynamicChild}) {
            const vec3 world =
                IREntity::getComponent<C_WorldTransform>(child.entity_).translation_;
            const vec3 twin = IREntity::getComponent<C_WorldTransform>(child.twin_).translation_;
            // The parents are four cells apart and so are the children.
            EXPECT_EQ(world + vec3(8.0f, 0.0f, 0.0f), twin) << step;
            EXPECT_EQ(h.relativeGlobals(child.entity_), h.relativeGlobals(child.twin_)) << step;
        }
    };
    h.frames(3);
    EXPECT_TRUE(Static::isSettled(parents.entity_));
    EXPECT_TRUE(Static::isSettled(staticChild.entity_));
    EXPECT_FALSE(Static::isStatic(dynamicChild.entity_));
    expectChildren("settled");

    Static::thaw(parents.entity_);
    h.frame();
    EXPECT_FALSE(Static::isStatic(parents.entity_));
    EXPECT_FALSE(Static::isStatic(staticChild.entity_)) << "thaw cascades to static descendants";
    IREntity::getComponent<C_LocalTransform>(parents.entity_).translation_ += kMove;
    IREntity::getComponent<C_LocalTransform>(parents.twin_).translation_ += kMove;
    h.frame();
    expectChildren("parent moved");

    EXPECT_EQ(Static::freeze(staticChild.entity_), Static::FreezeResult::PARENT_NOT_STATIC);
    EXPECT_EQ(Static::freeze(parents.entity_), Static::FreezeResult::FROZEN);
    h.frame();
    EXPECT_EQ(Static::freeze(staticChild.entity_), Static::FreezeResult::FROZEN);
    h.frames(3);
    EXPECT_TRUE(Static::isSettled(staticChild.entity_));
    expectChildren("frozen again");
}

// A root with a child and a grandchild at fixed local offsets, so a static
// tree and a dynamic twin tree agree relative to whatever the child hangs
// under.
struct Tree {
    EntityId root_;
    EntityId child_;
    EntityId grandchild_;
};
constexpr vec3 kChildLocal{1.0f, 2.0f, 0.0f};
constexpr vec3 kGrandchildLocal{0.0f, 1.0f, 1.0f};

Tree makeTree(Harness &h, bool isStatic) {
    const Tree tree{h.makeBody(isStatic), h.makeBody(isStatic), h.makeBody(isStatic)};
    IREntity::getComponent<C_LocalTransform>(tree.child_).translation_ = kChildLocal;
    IREntity::getComponent<C_LocalTransform>(tree.grandchild_).translation_ = kGrandchildLocal;
    IREntity::setParent(tree.child_, tree.root_);
    IREntity::setParent(tree.grandchild_, tree.child_);
    return tree;
}

vec3 worldOf(EntityId entity) {
    return IREntity::getComponent<C_WorldTransform>(entity).translation_;
}

// The child and grandchild sit where `parent` (or the origin) puts them,
// and the static tree's voxels match the twin tree's.
void expectTreeUnder(
    Harness &h, const Tree &tree, const Tree &twin, vec3 parentWorld, vec3 twinParentWorld,
    const char *step
) {
    EXPECT_EQ(worldOf(tree.child_), parentWorld + kChildLocal) << step;
    EXPECT_EQ(worldOf(tree.grandchild_), parentWorld + kChildLocal + kGrandchildLocal) << step;
    EXPECT_EQ(worldOf(twin.child_), twinParentWorld + kChildLocal) << step;
    EXPECT_EQ(worldOf(twin.grandchild_), twinParentWorld + kChildLocal + kGrandchildLocal) << step;
    EXPECT_EQ(h.relativeGlobals(tree.child_), h.relativeGlobals(twin.child_)) << step;
    EXPECT_EQ(h.relativeGlobals(tree.grandchild_), h.relativeGlobals(twin.grandchild_)) << step;
}

TEST(StaticWorld, AStaticTreeCreatedWithItsParentsSettlesAndStaysStatic) {
    Harness h;
    const Tree tree = makeTree(h, true);
    const Tree twin = makeTree(h, false);
    h.frames(3);
    for (const EntityId entity : {tree.root_, tree.child_, tree.grandchild_}) {
        EXPECT_TRUE(Static::isSettled(entity));
    }
    EXPECT_EQ(h.stats().counters_.refused_, 0u);
    expectTreeUnder(h, tree, twin, worldOf(tree.root_), worldOf(twin.root_), "settled");
}

void expectReparentResettlesTheSubtree(bool settledTree) {
    Harness h;
    const Tree tree = makeTree(h, true);
    const Tree twin = makeTree(h, false);
    const EntityId other = h.makeBody(true);
    const EntityId twinOther = h.makeBody(false);
    const auto reparent = [&]() {
        IREntity::setParent(tree.child_, other);
        IREntity::setParent(twin.child_, twinOther);
    };
    if (settledTree) {
        h.frames(3);
        ASSERT_TRUE(Static::isSettled(tree.grandchild_));
        reparent();
        EXPECT_TRUE(Static::isSettled(tree.root_)) << "the old root is untouched";
        EXPECT_TRUE(Static::isSettled(other));
    } else {
        // A settling tree, after PROPAGATE_TRANSFORM has dispatched in the
        // open round.
        h.frame();
        h.after(AFTER_PROPAGATE, reparent);
        h.update();
        h.render();
    }
    EXPECT_FALSE(Static::isSettled(tree.child_));
    EXPECT_FALSE(Static::isSettled(tree.grandchild_));
    Static::drain();
    EXPECT_FALSE(Static::isSettled(tree.child_)) << "not promoted at the drain that follows";
    EXPECT_FALSE(Static::isSettled(tree.grandchild_));
    h.frames(2);
    EXPECT_TRUE(Static::isSettled(tree.child_));
    EXPECT_TRUE(Static::isSettled(tree.grandchild_));
    EXPECT_TRUE(Static::isStatic(tree.child_));
    expectTreeUnder(h, tree, twin, worldOf(other), worldOf(twinOther), "under the new parent");
}

TEST(StaticWorld, ReparentingASettledSubtreeResettlesItUnderTheNewParent) {
    expectReparentResettlesTheSubtree(true);
}

TEST(StaticWorld, ReparentingASettlingSubtreeRestartsItsWait) {
    expectReparentResettlesTheSubtree(false);
}

TEST(StaticWorld, UnparentingResettlesTheSubtreeAsARoot) {
    for (const bool detach : {false, true}) {
        Harness h;
        const Tree tree = makeTree(h, true);
        const Tree twin = makeTree(h, false);
        h.frames(3);
        ASSERT_TRUE(Static::isSettled(tree.grandchild_));
        if (detach) {
            h.entities_.detachChildren(tree.root_);
            h.entities_.detachChildren(twin.root_);
        } else {
            h.entities_.clearParent(tree.child_);
            h.entities_.clearParent(twin.child_);
        }
        EXPECT_FALSE(Static::isSettled(tree.child_));
        EXPECT_FALSE(Static::isSettled(tree.grandchild_));
        h.frames(2);
        EXPECT_TRUE(Static::isSettled(tree.child_));
        EXPECT_TRUE(Static::isSettled(tree.grandchild_));
        expectTreeUnder(h, tree, twin, vec3(0.0f), vec3(0.0f), detach ? "detached" : "cleared");
    }
}

TEST(StaticWorld, ReparentingUnderADynamicEntityMakesTheSubtreeDynamic) {
    Harness h;
    const Tree tree = makeTree(h, true);
    const Tree twin = makeTree(h, false);
    const EntityId mover = h.makeBody(false);
    const EntityId twinMover = h.makeBody(false);
    h.frames(3);
    ASSERT_TRUE(Static::isSettled(tree.grandchild_));
    IREntity::setParent(tree.child_, mover);
    IREntity::setParent(twin.child_, twinMover);
    h.frame();
    EXPECT_FALSE(Static::isStatic(tree.child_));
    EXPECT_FALSE(Static::isStatic(tree.grandchild_));
    EXPECT_EQ(h.stats().counters_.refused_, 2u);
    for (const EntityId entity : {mover, twinMover}) {
        IREntity::getComponent<C_LocalTransform>(entity).translation_ += kMove;
    }
    h.frame();
    expectTreeUnder(h, tree, twin, worldOf(mover), worldOf(twinMover), "following the mover");
}

void destroyRoots(Harness &h, const Tree &tree, const Tree &twin, bool eager) {
    for (const EntityId root : {tree.root_, twin.root_}) {
        if (eager) {
            h.entities_.destroyEntity(root);
        } else {
            IREntity::destroyEntity(root);
        }
    }
    if (!eager) {
        h.entities_.destroyMarkedEntities();
    }
}

TEST(StaticWorld, ADestroyedParentsSurvivorsKeepTheirLinkAndDeriveAsRoots) {
    for (const bool eager : {true, false}) {
        Harness h;
        const Tree tree = makeTree(h, true);
        const Tree twin = makeTree(h, false);
        h.frames(3);
        ASSERT_TRUE(Static::isSettled(tree.grandchild_));
        ASSERT_NE(worldOf(tree.child_), kChildLocal) << "the root sits away from the origin";

        destroyRoots(h, tree, twin, eager);

        for (const EntityId entity : {tree.child_, tree.grandchild_}) {
            EXPECT_TRUE(Static::isStatic(entity));
            EXPECT_FALSE(Static::isSettled(entity));
        }
        for (const Tree &each : {tree, twin}) {
            EXPECT_EQ(
                h.entities_.getParent(each.child_) & IR_ENTITY_ID_BITS,
                each.root_ & IR_ENTITY_ID_BITS
            );
            EXPECT_EQ(h.entities_.findRecord(each.root_), nullptr);
            const std::vector<EntityId> orphans = h.entities_.getChildren(each.root_);
            ASSERT_EQ(orphans.size(), 1u);
            EXPECT_EQ(orphans[0] & IR_ENTITY_ID_BITS, each.child_ & IR_ENTITY_ID_BITS);
            EXPECT_EQ(
                h.entities_.getParent(each.grandchild_) & IR_ENTITY_ID_BITS,
                each.child_ & IR_ENTITY_ID_BITS
            );
        }

        Static::drain();
        EXPECT_FALSE(Static::isSettled(tree.child_)) << "restamped, not promoted, at the drain";
        EXPECT_FALSE(Static::isSettled(tree.grandchild_));
        EXPECT_EQ(h.stamp(tree.child_), h.systems_.roundsOpened() + 1);

        h.frames(2);
        for (const EntityId entity : {tree.child_, tree.grandchild_}) {
            EXPECT_TRUE(Static::isStatic(entity));
            EXPECT_TRUE(Static::isSettled(entity));
        }
        EXPECT_EQ(h.stats().counters_.refused_, 0u);
        expectTreeUnder(h, tree, twin, vec3(0.0f), vec3(0.0f), eager ? "eager" : "deferred");
    }
}

TEST(StaticWorld, APreDestroyHookThatReparentsASurvivorLeavesItUnderTheNewRoot) {
    Harness h;
    const Tree tree = makeTree(h, true);
    const Tree twin = makeTree(h, false);
    const EntityId other = h.makeBody(true);
    const EntityId twinOther = h.makeBody(false);
    h.frames(3);
    ASSERT_TRUE(Static::isSettled(tree.grandchild_));

    const auto hook = h.entities_.registerPreDestroyHook([&](EntityId dying) {
        if ((dying & IR_ENTITY_ID_BITS) == (tree.root_ & IR_ENTITY_ID_BITS)) {
            IREntity::setParent(tree.child_, other);
        } else if ((dying & IR_ENTITY_ID_BITS) == (twin.root_ & IR_ENTITY_ID_BITS)) {
            IREntity::setParent(twin.child_, twinOther);
        }
    });
    destroyRoots(h, tree, twin, true);
    h.entities_.unregisterPreDestroyHook(hook);

    EXPECT_EQ(h.entities_.getParent(tree.child_) & IR_ENTITY_ID_BITS, other & IR_ENTITY_ID_BITS);
    EXPECT_FALSE(Static::isSettled(tree.child_));
    EXPECT_FALSE(Static::isSettled(tree.grandchild_));
    EXPECT_TRUE(Static::isSettled(other));
    h.frames(2);
    EXPECT_TRUE(Static::isSettled(tree.child_));
    EXPECT_TRUE(Static::isSettled(tree.grandchild_));
    expectTreeUnder(h, tree, twin, worldOf(other), worldOf(twinOther), "re-parented by the hook");
}

// A one-shot query, as a command body runs it between fixed updates, never
// counts as a derivation: it moves no round and no stamp, and it reaches no
// static row it could leave half-derived.
TEST(StaticWorld, AQueryBetweenUpdatesNeitherDerivesNorDelaysASettlingEntity) {
    const auto framesToSettle = [](bool runQueries) {
        Harness h;
        const EntityId entity = h.makeBody(true);
        const EntityId dynamic = h.makeBody(false);
        int frames = 0;
        while (!Static::isSettled(entity) && frames < 12) {
            if (runQueries) {
                const std::uint64_t opened = h.systems_.roundsOpened();
                const std::uint64_t completed = h.systems_.roundsCompleted();
                const std::uint64_t stamp = h.stamp(entity);
                int derivedRows = 0;
                IRSystem::executeQuery<C_WorldTransform, IRSystem::Exclude<C_Settled>>(
                    [&derivedRows](C_WorldTransform &) { ++derivedRows; }
                );
                int runtimeRows = 0;
                IRSystem::executeQueryDynamic(
                    IREntity::getArchetype<C_WorldTransform>(),
                    IREntity::getArchetype<C_Settled>(),
                    [&runtimeRows](IREntity::ArchetypeNode *node) {
                        runtimeRows += node->length_;
                    }
                );
                int readRows = 0;
                IRSystem::executeQuery<const C_LocalTransform, const C_VoxelSetNew>(
                    [&readRows](const C_LocalTransform &, const C_VoxelSetNew &) { ++readRows; }
                );
                EXPECT_EQ(derivedRows, runtimeRows);
                EXPECT_EQ(readRows, 2) << "a reader visits the settling body and the dynamic one";
                EXPECT_EQ(h.systems_.roundsOpened(), opened);
                EXPECT_EQ(h.systems_.roundsCompleted(), completed);
                EXPECT_EQ(h.stamp(entity), stamp);
            }
            h.frame();
            ++frames;
        }
        EXPECT_FALSE(Static::isStatic(dynamic));
        return frames;
    };
    const int undisturbed = framesToSettle(false);
    EXPECT_GT(undisturbed, 0);
    EXPECT_LT(undisturbed, 12);
    EXPECT_EQ(framesToSettle(true), undisturbed);
}

TEST(StaticWorld, ReparentingToTheSameParentLeavesTheChildSettled) {
    Harness h;
    const Tree tree = makeTree(h, true);
    h.frames(3);
    IREntity::setParent(tree.child_, tree.root_);
    EXPECT_TRUE(Static::isSettled(tree.child_));
    EXPECT_TRUE(Static::isSettled(tree.grandchild_));
}

TEST(StaticWorld, AnAttachOnALongSettledEntityWaitsForARoundThatOpensAfterIt) {
    struct C_Unrelated {};
    Harness h;
    const EntityId entity = h.makeBody(true);
    h.frames(6);
    ASSERT_TRUE(Static::isSettled(entity));
    IREntity::setComponent(entity, C_Unrelated{});
    Static::drain();
    EXPECT_FALSE(Static::isSettled(entity)) << "its old stamp is long satisfied";
    EXPECT_EQ(h.stamp(entity), h.systems_.roundsOpened() + 1);
    h.update();
    EXPECT_FALSE(Static::isSettled(entity)) << "the round that opened after it is still open";
    h.render();
    EXPECT_TRUE(Static::isSettled(entity));
}

// One main-loop frame as World runs it: INPUT then UPDATE for each due
// tick, then RENDER, with the drain after every pipeline.
struct LoopStages {
    Harness &h_;
    EntityId watched_;
    int dueTicks_;
    bool promotedBeforeRender_ = false;
    std::uint64_t completedAtStart_ = 0;
    bool completedMovedBeforeRender_ = false;

    void note() {
        promotedBeforeRender_ = promotedBeforeRender_ || Static::isSettled(watched_);
        completedMovedBeforeRender_ = completedMovedBeforeRender_ ||
                                      h_.systems_.roundsCompleted() != completedAtStart_;
    }
    bool shouldUpdate() {
        return dueTicks_-- > 0;
    }
    void pollEvents() {}
    void input() {
        h_.systems_.executePipeline(IRTime::Events::INPUT);
        Static::drain();
        note();
    }
    bool holdForFirstInput() {
        return false;
    }
    void update() {
        h_.update();
        note();
    }
    void render() {
        h_.render();
    }
    bool halted() {
        return false;
    }
};

TEST(StaticWorld, ARoundSpansThePipelinesAndOnlyTheRenderJoinClosesIt) {
    for (const bool frameWithoutUpdate : {false, true}) {
        Harness h;
        const Harness::Pair pair = h.makePair();
        h.frame();
        ASSERT_FALSE(Static::isSettled(pair.entity_));
        if (frameWithoutUpdate) {
            LoopStages renderOnly{h, pair.entity_, 0};
            renderOnly.completedAtStart_ = h.systems_.roundsCompleted();
            IREngine::runMainLoopFrame(renderOnly);
            EXPECT_FALSE(Static::isSettled(pair.entity_))
                << "the UPDATE groups stay under the cursor";
            EXPECT_EQ(h.systems_.roundsCompleted(), renderOnly.completedAtStart_);
        }
        LoopStages stages{h, pair.entity_, 3};
        stages.completedAtStart_ = h.systems_.roundsCompleted();
        EXPECT_EQ(IREngine::runMainLoopFrame(stages), 3u);
        EXPECT_FALSE(stages.promotedBeforeRender_)
            << "no INPUT or UPDATE drain of the round may promote";
        EXPECT_FALSE(stages.completedMovedBeforeRender_);
        EXPECT_TRUE(Static::isSettled(pair.entity_)) << "promoted at the RENDER drain";
        h.expectTwin(pair, "after the cross-pipeline round");
    }
}

// ---- Gate 5: forbidden writes ---------------------------------------------

TEST(StaticWorld, ARegisteredMoverNeverVisitsAStaticEntity) {
    Harness h;
    const EntityId entity = h.makeBody(true);
    const EntityId twin = h.makeBody(false);
    IREntity::setComponent(entity, C_Mover{});
    IREntity::setComponent(twin, C_Mover{});
    const vec3 start = IREntity::getComponent<C_LocalTransform>(entity).translation_;
    h.systems_.setTimingEnabled(true);
    h.frames(4);
    EXPECT_EQ(IREntity::getComponent<C_LocalTransform>(entity).translation_, start);
    EXPECT_EQ(
        IREntity::getComponent<C_LocalTransform>(twin).translation_,
        start + vec3(4.0f, 0.0f, 0.0f) + kMove * 4.0f
    );
    EXPECT_EQ(h.systems_.getTimingAccum(h.mover_).totalWalkedCount_, 4u)
        << "the mover walked the dynamic twin only";
    EXPECT_TRUE(Static::isSettled(entity));
}

TEST(StaticWorld, AModifierPushThawsAndReachesTheWorldTransformLikeOnATwin) {
    Harness h;
    const Harness::Pair pair = h.makeSettledPair();
    Modifier::push(pair.entity_, h.translation_, TransformKind::ADD, kGlobalOffset, 0);
    Modifier::push(pair.twin_, h.translation_, TransformKind::ADD, kGlobalOffset, 0);
    h.frame();
    EXPECT_FALSE(Static::isStatic(pair.entity_));
    EXPECT_EQ(h.stats().counters_.thawed_, 1u);
    h.expectTwin(pair, "the update after the push");
}

#ifndef IR_RELEASE
TEST(StaticWorld, TheAuditCatchesARawWriteThatIsNeverResettled) {
    Harness h;
    const Harness::Pair pair = h.makeSettledPair();
    IREntity::getComponent<C_LocalTransform>(pair.entity_).translation_ += kMove;
    const auto auditOnePeriod = [] {
        for (std::uint64_t i = 0; i < Static::kAuditPeriod; ++i) {
            Static::audit();
        }
    };
    EXPECT_THROW(auditOnePeriod(), std::runtime_error);

    Static::resettle(pair.entity_);
    IREntity::getComponent<C_LocalTransform>(pair.twin_).translation_ += kMove;
    h.frames(3);
    EXPECT_NO_THROW(auditOnePeriod());
    h.expectTwin(pair, "resettled");
}

TEST(StaticWorld, TheAuditCatchesARawLodBandAssignment) {
    Harness h;
    const Harness::Pair pair = h.makeSettledPair();
    h.set(pair.entity_).lodMin_ = LodLevel::LOD_0;
    h.set(pair.entity_).lodMax_ = LodLevel::LOD_0;
    h.systems_.executePipeline(IRTime::Events::UPDATE);
    EXPECT_FALSE(h.set(pair.entity_).lodCulled_) << "a raw assignment is not derived";
    const auto auditOnePeriod = [] {
        for (std::uint64_t i = 0; i < Static::kAuditPeriod; ++i) {
            Static::audit();
        }
    };
    EXPECT_THROW(auditOnePeriod(), std::runtime_error);
}
#endif

// ---- Gate 5: engine mutators ----------------------------------------------

TEST(StaticWorld, EveryVoxelSetMutatorResettlesItsOwner) {
    using Mutator = std::function<void(C_VoxelSetNew &)>;
    const std::vector<std::pair<const char *, Mutator>> mutators = {
        {"changeVoxelColor",
         [](C_VoxelSetNew &s) { s.changeVoxelColor(ivec3(0), Color{1, 2, 3, 255}); }},
        {"changeVoxelColorAll", [](C_VoxelSetNew &s) { s.changeVoxelColorAll(Color{4, 5, 6, 255}); }},
        {"changeVoxelPriority", [](C_VoxelSetNew &s) { s.changeVoxelPriority(ivec3(0), 1); }},
        {"changeVoxelPriorityAll", [](C_VoxelSetNew &s) { s.changeVoxelPriorityAll(2); }},
        {"deactivateAll", [](C_VoxelSetNew &s) { s.deactivateAll(); }},
        {"activateAll", [](C_VoxelSetNew &s) { s.activateAll(); }},
        {"fillPlane", [](C_VoxelSetNew &s) { s.fillPlane(1, 0, Color{7, 8, 9, 255}); }},
        {"reshape", [](C_VoxelSetNew &s) { s.reshape(IRMath::Shape3D::RECTANGULAR_PRISM); }},
        {"editVoxels",
         [](C_VoxelSetNew &s) {
             s.editVoxels([](int, IRComponents::C_Voxel &voxel, vec3) {
                 voxel.color_ = Color{9, 8, 7, 255};
             });
         }},
        {"carve", [](C_VoxelSetNew &s) { s.carve([](vec3 local) { return local.x > 0.5f; }); }},
        {"resyncAfterRawEdits",
         [](C_VoxelSetNew &s) {
             s.voxels_[0].color_ = Color{3, 3, 3, 255};
             s.resyncAfterRawEdits();
         }},
        {"resyncAfterAuthoredEdits",
         [](C_VoxelSetNew &s) {
             s.mutableAuthoredRecords()[0].color_ = Color{5, 5, 5, 255};
             s.resyncAfterAuthoredEdits();
         }},
        {"setLodBand", [](C_VoxelSetNew &s) { s.setLodBand(LodLevel::LOD_0, LodLevel::LOD_0); }},
    };
    for (const auto &[name, mutate] : mutators) {
        Harness h;
        const Harness::Pair pair = h.makeSettledPair();
        ASSERT_TRUE(h.set(pair.entity_).ownerStatic_) << name;
        mutate(h.set(pair.entity_));
        mutate(h.set(pair.twin_));
        Static::drain();
        EXPECT_FALSE(Static::isSettled(pair.entity_)) << name;
        EXPECT_TRUE(Static::isStatic(pair.entity_)) << name;
        EXPECT_EQ(h.stats().counters_.resettled_, 1u) << name;
        h.frames(2);
        EXPECT_TRUE(Static::isSettled(pair.entity_)) << name;
        h.expectTwin(pair, name);
    }
}

TEST(StaticWorld, ADynamicEntitysVoxelMutationStagesNoNote) {
    Harness h;
    const EntityId entity = h.makeBody(false);
    h.frames(2);
    EXPECT_FALSE(h.set(entity).ownerStatic_);
    h.set(entity).changeVoxelColorAll(Color{4, 5, 6, 255});
    std::vector<EntityId> notes;
    h.entities_.takeEntityNotes(notes);
    EXPECT_TRUE(notes.empty());
}

TEST(StaticWorld, AStaleOwnerStaticBitCostsOneDroppedNote) {
    Harness h;
    const Harness::Pair pair = h.makeSettledPair();
    Static::thaw(pair.entity_);
    h.frame();
    ASSERT_TRUE(h.set(pair.entity_).ownerStatic_);
    h.set(pair.entity_).changeVoxelColorAll(Color{4, 5, 6, 255});
    Static::drain();
    EXPECT_FALSE(h.set(pair.entity_).ownerStatic_) << "the drain clears the hint with the note";
    EXPECT_FALSE(Static::isStatic(pair.entity_));
}

TEST(StaticWorld, OwnerAdoptionCoversAnEmptySetAndAnEditBeforeTheFirstDrain) {
    Harness h;
    const EntityId empty = IREntity::createEntity(C_VoxelSetNew{}, C_Static{});
    const Harness::Pair pair = h.makePair();
    h.after(AFTER_PROPAGATE, [&]() {
        h.set(pair.entity_).changeVoxelColorAll(Color{4, 5, 6, 255});
        h.set(pair.twin_).changeVoxelColorAll(Color{4, 5, 6, 255});
    });
    h.update();
    EXPECT_EQ(h.set(empty).ownerEntityId_, empty);
    EXPECT_TRUE(h.set(empty).ownerStatic_);
    EXPECT_EQ(h.set(pair.entity_).ownerEntityId_, pair.entity_);
    h.render();
    h.frames(2);
    EXPECT_TRUE(Static::isSettled(pair.entity_));
    h.expectTwin(pair, "edit before adoption");
}

TEST(StaticWorld, ModifierMutatorsThawOrResettleTheirTarget) {
    Harness h;
    const IRComponents::FieldBindingId scalar = Modifier::registerField("static_test_scalar");
    const auto settledBody = [&h]() {
        const EntityId entity = h.makeBody(true);
        IREntity::setComponent(entity, C_LambdaModifiers{});
        h.frames(3);
        EXPECT_TRUE(Static::isSettled(entity));
        return entity;
    };
    const EntityId source = h.makeBody(false);
    using Push = std::function<void(EntityId)>;
    const std::vector<std::pair<const char *, Push>> thawing = {
        {"push",
         [&](EntityId e) { Modifier::push(e, h.translation_, TransformKind::ADD, kMove, source); }},
        {"pushFrameLocal",
         [&](EntityId e) {
             Modifier::pushFrameLocal(e, h.translation_, TransformKind::ADD, kMove, source);
         }},
        {"pushOneFrame",
         [&](EntityId e) {
             Modifier::pushOneFrame(e, h.translation_, TransformKind::ADD, kMove, source);
         }},
        {"upsertBySource",
         [&](EntityId e) {
             Modifier::upsertBySource(e, h.translation_, TransformKind::ADD, kMove, source);
         }},
        {"pushLambda",
         [&](EntityId e) { Modifier::pushLambda(e, scalar, [](float v) { return v; }, source); }},
    };
    for (const auto &[name, push] : thawing) {
        const EntityId entity = settledBody();
        push(entity);
        IREntity::flushStructuralChanges();
        EXPECT_FALSE(Static::isStatic(entity)) << name;
    }

    // A modifier that predates the freeze stays, frozen; sweeping its source
    // shortens the vector and the entity is derived again without it.
    const Harness::Pair pair = h.makePair();
    for (const EntityId entity : {pair.entity_, pair.twin_}) {
        IREntity::getComponent<C_Modifiers>(entity).modifiersVec3_.push_back(
            IRComponents::ModifierVec3{h.translation_, TransformKind::ADD, kMove, source, -1}
        );
    }
    h.frames(3);
    ASSERT_TRUE(Static::isSettled(pair.entity_));
    h.expectTwin(pair, "frozen with a modifier");
    Modifier::removeBySource(source);
    IREntity::flushStructuralChanges();
    EXPECT_TRUE(Static::isStatic(pair.entity_));
    EXPECT_FALSE(Static::isSettled(pair.entity_)) << "removeBySource resettles";
    h.frames(3);
    EXPECT_TRUE(Static::isSettled(pair.entity_));
    h.expectTwin(pair, "after the source sweep");
}

TEST(StaticWorld, GlobalModifierMutatorsAdvanceTheRevision) {
    Harness h;
    const C_GlobalModifiers &globals = IREntity::singleton<C_GlobalModifiers>();
    const EntityId source = h.makeBody(false);
    std::uint64_t seen = globals.revision_;
    const auto expectAdvanced = [&](const char *what) {
        EXPECT_GT(globals.revision_, seen) << what;
        seen = globals.revision_;
    };
    Modifier::pushGlobal(h.translation_, TransformKind::ADD, kMove, source);
    expectAdvanced("pushGlobal");
    Modifier::upsertBySourceGlobal(h.translation_, TransformKind::ADD, kMove, source);
    expectAdvanced("upsertBySourceGlobal");
    Modifier::removeBySource(source);
    expectAdvanced("removeBySource");
    Modifier::removeBySource(source);
    EXPECT_EQ(globals.revision_, seen) << "a sweep that removed nothing";

    Modifier::pushGlobal(h.translation_, TransformKind::ADD, kMove, 0, 2);
    expectAdvanced("pushGlobal with a lifetime");
    h.update();
    EXPECT_EQ(globals.revision_, seen) << "a countdown alone changes no composed value";
    h.update();
    expectAdvanced("expiry in GLOBAL_MODIFIER_DECAY");
}

TEST(StaticWorld, RotationModeAndGizmoAttachThaw) {
    Harness h;
    const EntityId rotated = h.makeBody(true);
    const EntityId anchored = h.makeBody(true);
    h.frames(2);
    ASSERT_TRUE(Static::isSettled(rotated));
    IRPrefab::RotationMode::setMode(rotated, IRComponents::RotationMode::GRID);
    IRPrefab::Gizmo::createTranslateGizmoForAnchor(anchored);
    IREntity::flushStructuralChanges();
    EXPECT_FALSE(Static::isStatic(rotated));
    EXPECT_FALSE(Static::isStatic(anchored));
}

TEST(StaticWorld, AnotherArchetypeMoveResettlesAndAnIneligibleOneIsRefused) {
    Harness h;
    const Harness::Pair pair = h.makeSettledPair();
    for (const EntityId entity : {pair.entity_, pair.twin_}) {
        h.set(entity).setLodBand(LodLevel::LOD_4, LodLevel::LOD_3);
    }
    h.frames(2);
    ASSERT_TRUE(Static::isSettled(pair.entity_));
    h.expectTwin(pair, "band set");

    for (const EntityId entity : {pair.entity_, pair.twin_}) {
        IREntity::setComponent(entity, C_LodTierOverride{LodLevel::LOD_0});
    }
    EXPECT_FALSE(Static::isSettled(pair.entity_)) << "attaching the override";
    h.frames(2);
    EXPECT_TRUE(Static::isSettled(pair.entity_));
    EXPECT_TRUE(h.set(pair.entity_).lodCulled_) << "the pinned tier is outside the band";
    h.expectTwin(pair, "override attached");

    for (const EntityId entity : {pair.entity_, pair.twin_}) {
        IREntity::removeComponent<C_LodTierOverride>(entity);
    }
    EXPECT_FALSE(Static::isSettled(pair.entity_)) << "detaching the override";
    h.frames(2);
    EXPECT_TRUE(Static::isSettled(pair.entity_));
    EXPECT_FALSE(h.set(pair.entity_).lodCulled_);
    h.expectTwin(pair, "override detached");

    IREntity::setComponent(pair.entity_, IRComponents::C_Skeleton{});
    h.frame();
    EXPECT_FALSE(Static::isStatic(pair.entity_)) << "a skeleton root is not eligible";
    EXPECT_EQ(h.stats().counters_.refused_, 1u);
}

TEST(StaticWorld, AnIneligibleEntityCreatedStaticIsRefusedAtItsFirstDrain) {
    Harness h;
    const EntityId detached = IREntity::createEntity(
        C_LocalTransform{vec3(0.0f)},
        IRComponents::C_RotationMode{IRComponents::RotationMode::DETACHED},
        C_Static{}
    );
    const EntityId parent = h.makeBody(false);
    const EntityId child = h.makeBody(true);
    IREntity::setParent(child, parent);
    h.frame();
    EXPECT_FALSE(Static::isStatic(detached));
    EXPECT_FALSE(Static::isStatic(child)) << "a static child needs a static parent";
    EXPECT_EQ(h.stats().counters_.refused_, 2u);
    EXPECT_EQ(Static::freeze(detached), Static::FreezeResult::NOT_GRID);
    EXPECT_EQ(Static::freeze(IREntity::EntityId{123456}), Static::FreezeResult::MISSING);
}

TEST(StaticWorld, FogSettersResettleTheSubject) {
    using Setter = std::function<void(EntityId)>;
    const std::vector<std::pair<const char *, Setter>> setters = {
        {"setHiddenPolicy",
         [](EntityId e) { Fog::setHiddenPolicy(e, IRComponents::FogHiddenPolicy::GHOST); }},
        {"setSubjectClass", [](EntityId e) { Fog::setSubjectClass(e, Fog::FogSubjectClass::EXEMPT); }},
        {"setEntityRevealGoverned", [](EntityId e) { Fog::setEntityRevealGoverned(e, false); }},
        {"setOverride",
         [](EntityId e) { Fog::setOverride(e, IRComponents::FogOverride::FORCE_HIDDEN); }},
        {"setChannels", [](EntityId e) { Fog::setChannels(e, 2u); }},
    };
    for (const auto &[name, setter] : setters) {
        Harness h;
        const EntityId entity = h.makeBody(true);
        h.frames(2);
        ASSERT_TRUE(Static::isSettled(entity)) << name;
        setter(entity);
        IREntity::flushStructuralChanges();
        Static::drain();
        EXPECT_TRUE(Static::isStatic(entity)) << name;
        EXPECT_FALSE(Static::isSettled(entity)) << name;
        EXPECT_GT(h.framesUntilSettled(entity), 0) << name;
    }
}

// ---- Gates 5 and 9: fog and LOD triggers ----------------------------------

struct FogHarness : Harness {
    FogHarness() {
        IREntity::setComponent(canvas_, C_CanvasFogOfWar{C_CanvasFogOfWar::HeadlessInit{}});
        systems_.setTimingEnabled(true);
    }

    C_CanvasFogOfWar &fog() {
        return IREntity::getComponent<C_CanvasFogOfWar>(canvas_);
    }

    void seeAt(vec3 centre) {
        fog().clearVisionCircles();
        fog().addVisionCircle(centre.x, centre.y, 6.0f);
    }

    // Rows FOG_REVEAL_EVAL walked over the next update.
    std::uint64_t evalWalked() {
        systems_.resetTimingStats();
        update();
        return systems_.getTimingAccum(eval_).totalWalkedCount_;
    }
};

TEST(StaticWorld, ASettledSubjectIsWalkedOnlyWhenAFogInputChanged) {
    FogHarness h;
    const EntityId entity = h.makeBody(true);
    h.frames(4);
    ASSERT_TRUE(Static::isSettled(entity));
    EXPECT_EQ(h.evalWalked(), 0u) << "nothing changed";

    h.fog().setCell(3, 3, IRComponents::kFogStateVisible);
    EXPECT_EQ(h.evalWalked(), 1u) << "a field cell changed";
    EXPECT_EQ(h.evalWalked(), 0u);

    h.seeAt(vec3(40.0f, 40.0f, 0.0f));
    EXPECT_EQ(h.evalWalked(), 1u) << "a vision source changed";
    EXPECT_EQ(h.evalWalked(), 0u);

    ++h.fog().revision_;
    EXPECT_EQ(h.evalWalked(), 1u) << "a line-of-sight publish";

    IREntity::singleton<C_FogRevealSettings>().showThreshold_ = 0.6f;
    EXPECT_EQ(h.evalWalked(), 1u) << "the reveal settings changed";
    EXPECT_EQ(h.evalWalked(), 0u);

    IREntity::removeComponent<C_CanvasFogOfWar>(h.canvas_);
    EXPECT_EQ(h.evalWalked(), 1u) << "fog was detached from the active canvas";
    EXPECT_EQ(h.evalWalked(), 0u);
}

TEST(StaticWorld, AReplacedFogComponentArmsThePassAtAnEqualRevision) {
    FogHarness h;
    const EntityId entity = h.makeBody(true);
    h.frames(4);
    ASSERT_TRUE(Static::isSettled(entity));
    ASSERT_EQ(h.evalWalked(), 0u);
    const std::uint64_t revision = h.fog().revision_;

    IREntity::setComponent(h.canvas_, C_CanvasFogOfWar{C_CanvasFogOfWar::HeadlessInit{}});
    h.fog().revision_ = revision;
    EXPECT_EQ(h.evalWalked(), 1u) << "a new field behind the same revision";
    EXPECT_EQ(h.evalWalked(), 0u);
}

TEST(StaticWorld, AnArmedPassCoversEveryStaggeredRowOnce) {
    FogHarness h;
    IREntity::singleton<C_FogRevealSettings>().staggerPeriod_ = 3;
    const EntityId entity = h.makeBody(true);
    h.frames(6);
    ASSERT_TRUE(Static::isSettled(entity));
    h.fog().setCell(3, 3, IRComponents::kFogStateVisible);
    EXPECT_EQ(h.evalWalked(), 1u);
    EXPECT_EQ(h.evalWalked(), 1u);
    EXPECT_EQ(h.evalWalked(), 1u);
    EXPECT_EQ(h.evalWalked(), 0u) << "armed for staggerPeriod dispatches";
}

TEST(StaticWorld, ASettledSubjectsFogVerdictStillFollowsTheFog) {
    FogHarness h;
    const Harness::Pair pair = h.makePair();
    const vec3 here = IREntity::getComponent<C_LocalTransform>(pair.entity_).translation_;
    const vec3 away{200.0f, 200.0f, 0.0f};

    h.seeAt(here);
    h.frames(4);
    ASSERT_TRUE(Static::isSettled(pair.entity_));
    EXPECT_TRUE(h.set(pair.entity_).visible_) << "inside the vision circle";
    h.expectTwin(pair, "shown");

    h.seeAt(away);
    h.frames(2);
    EXPECT_TRUE(Static::isSettled(pair.entity_)) << "a fog change does not unsettle";
    EXPECT_FALSE(h.set(pair.entity_).visible_) << "the circle moved away";
    h.expectTwin(pair, "hidden");

    h.seeAt(here);
    h.frames(2);
    EXPECT_TRUE(h.set(pair.entity_).visible_) << "the circle returned";
    h.expectTwin(pair, "shown again");
}

TEST(StaticWorld, ASettledSetsLodGateFollowsTheActiveTier) {
    Harness h;
    const Harness::Pair pair = h.makePair();
    for (const EntityId entity : {pair.entity_, pair.twin_}) {
        h.set(entity).setLodBand(LodLevel::LOD_4, LodLevel::LOD_3);
    }
    h.frames(3);
    ASSERT_TRUE(Static::isSettled(pair.entity_));
    EXPECT_FALSE(h.set(pair.entity_).lodCulled_);

    h.systems_.setTimingEnabled(true);
    h.systems_.resetTimingStats();
    h.frames(2);
    EXPECT_EQ(h.systems_.getTimingAccum(h.lodGate_).totalWalkedCount_, 2u)
        << "only the dynamic twin is walked while the tier is unchanged";

    IREntity::singleton<C_ActiveLodLevel>().current_ = LodLevel::LOD_0;
    h.frame();
    EXPECT_TRUE(Static::isSettled(pair.entity_));
    EXPECT_TRUE(h.set(pair.entity_).lodCulled_) << "the tier left the band";
    h.expectTwin(pair, "tier changed");
}

} // namespace

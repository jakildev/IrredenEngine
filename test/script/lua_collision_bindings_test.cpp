#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/script/lua_script.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/update/components/component_collider_iso3d_aabb.hpp>
#include <irreden/update/components/component_collision_layer.hpp>
#include <irreden/update/components/component_contact_event.hpp>
#include <irreden/update/systems/system_collision_event_clear.hpp>
#include <irreden/update/systems/system_collision_note_platform.hpp>
#include <irreden/update/systems/system_dispatch_lua_overlap.hpp>

#include <sol/sol.hpp>

#include <cstdint>
#include <string>

namespace {

using IRComponents::C_ColliderIso3DAABB;
using IRComponents::C_CollisionLayer;
using IRComponents::C_ContactEvent;
using IRComponents::C_WorldTransform;
using IREntity::EntityId;
using IRMath::vec3;
using IRMath::vec4;

// Arcade-style raw integer layers (deliberately NOT the four built-in
// COLLISION_LAYER_* bits) — exercises the raw-int handler path the issue
// requires so creations aren't limited to the named layers.
constexpr std::uint32_t kProjectile = 1u << 4;
constexpr std::uint32_t kEnemy = 1u << 5;
constexpr std::uint32_t kPickup = 1u << 6;
constexpr std::uint32_t kPlayer = 1u << 7;

// Owns the EntityManager + SystemManager + LuaScript needed to drive the
// collision overlap surface end-to-end. Declaration order matters — the Lua
// state destructs first so any captured handler closures finish lua_unref
// while the lua_State is open (same rationale as lua_pipeline_register_test).
class LuaCollisionBindingsTest : public testing::Test {
  protected:
    LuaCollisionBindingsTest()
        : m_lua{}
        , m_entity_manager{}
        , m_system_manager{} {
        m_lua.bindLuaDrivenEcs();
        // Registering COLLISION_NOTE_PLATFORM creates the shared pair-buffer
        // singleton; registering the dispatch system makes its params
        // reachable from the IRCollision binding.
        m_lua.registerPrefabSystems<
            IRSystem::COLLISION_EVENT_CLEAR,
            IRSystem::COLLISION_NOTE_PLATFORM,
            IRSystem::DISPATCH_LUA_OVERLAP>();

        sol::state &lua = m_lua.lua();
        lua["PROJECTILE"] = kProjectile;
        lua["ENEMY"] = kEnemy;
        lua["PICKUP"] = kPickup;
        lua["PLAYER"] = kPlayer;
    }

    EntityId makeCollider(vec3 pos, std::uint32_t layer, std::uint32_t collidesWith) {
        return IREntity::createEntity(
            C_WorldTransform{pos, vec4(0.0f, 0.0f, 0.0f, 1.0f), vec3(1.0f)},
            C_ColliderIso3DAABB{vec3(0.5f)},
            C_CollisionLayer{layer, collidesWith, true},
            C_ContactEvent{}
        );
    }

    void buildPipeline() {
        m_lua.lua().script(R"lua(
            IRSystem.registerPipeline(IRTime.UPDATE, {
                IRSystem.systemId(IRSystem.SystemName.COLLISION_EVENT_CLEAR),
                IRSystem.systemId(IRSystem.SystemName.COLLISION_NOTE_PLATFORM),
                IRSystem.systemId(IRSystem.SystemName.DISPATCH_LUA_OVERLAP),
            })
        )lua");
    }

    void tick() {
        m_system_manager.executePipeline(IRTime::Events::UPDATE);
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

// ---- Layer enum table ------------------------------------------------------

TEST_F(LuaCollisionBindingsTest, LayerTableMirrorsCppEnum) {
    sol::state &lua = m_lua.lua();
    EXPECT_EQ(
        lua.script("return IRCollision.Layer.COLLISION_LAYER_DEFAULT").get<std::uint32_t>(),
        static_cast<std::uint32_t>(IRComponents::COLLISION_LAYER_DEFAULT)
    );
    EXPECT_EQ(
        lua.script("return IRCollision.Layer.COLLISION_LAYER_NOTE_PLATFORM").get<std::uint32_t>(),
        static_cast<std::uint32_t>(IRComponents::COLLISION_LAYER_NOTE_PLATFORM)
    );
    EXPECT_EQ(
        lua.script("return IRCollision.Layer.COLLISION_LAYER_PARTICLE").get<std::uint32_t>(),
        static_cast<std::uint32_t>(IRComponents::COLLISION_LAYER_PARTICLE)
    );
}

// ---- Acceptance criteria: projectile-vs-enemy AND pickup-vs-player ---------

// The headline acceptance criterion: two distinct Lua handlers, keyed by
// layer pair, both fire on overlap and both receive the two entity ids in the
// registered argument order.
TEST_F(LuaCollisionBindingsTest, ProjectileEnemyAndPickupPlayerOverlapsFireFromLua) {
    m_lua.lua().script(R"lua(
        _log = {}
        IRCollision.onOverlapEnter(PROJECTILE, ENEMY, function(a, b)
            table.insert(_log, { tag = "proj_enemy", a = a, b = b })
        end)
        IRCollision.onOverlapEnter(PICKUP, PLAYER, function(a, b)
            table.insert(_log, { tag = "pickup_player", a = a, b = b })
        end)
    )lua");

    // Two overlapping pairs, far enough apart (and on non-colliding layers)
    // that the pairs never cross.
    const EntityId projectile = makeCollider(vec3(0.0f), kProjectile, kEnemy);
    const EntityId enemy = makeCollider(vec3(0.0f), kEnemy, kProjectile);
    const EntityId pickup = makeCollider(vec3(20.0f), kPickup, kPlayer);
    const EntityId player = makeCollider(vec3(20.0f), kPlayer, kPickup);

    buildPipeline();
    tick();

    sol::table log = m_lua.lua()["_log"];
    ASSERT_EQ(log.size(), 2u);

    bool sawProjEnemy = false;
    bool sawPickupPlayer = false;
    for (std::size_t i = 1; i <= log.size(); ++i) {
        sol::table entry = log[i];
        const std::string tag = entry["tag"];
        const EntityId a = entry["a"].get<EntityId>();
        const EntityId b = entry["b"].get<EntityId>();
        if (tag == "proj_enemy") {
            sawProjEnemy = true;
            // Argument order matches the registered (PROJECTILE, ENEMY) order,
            // independent of which entity id is numerically smaller.
            EXPECT_EQ(a, projectile);
            EXPECT_EQ(b, enemy);
        } else if (tag == "pickup_player") {
            sawPickupPlayer = true;
            EXPECT_EQ(a, pickup);
            EXPECT_EQ(b, player);
        } else {
            ADD_FAILURE() << "unexpected overlap tag: " << tag;
        }
    }
    EXPECT_TRUE(sawProjEnemy);
    EXPECT_TRUE(sawPickupPlayer);
}

// Enter fires exactly once for a sustained overlap (not every frame), and exit
// fires the frame the overlap ends.
TEST_F(LuaCollisionBindingsTest, EnterFiresOnceExitFiresWhenOverlapEnds) {
    m_lua.lua().script(R"lua(
        enters = 0
        exits = 0
        IRCollision.onOverlapEnter(PROJECTILE, ENEMY, function(a, b) enters = enters + 1 end)
        IRCollision.onOverlapExit (PROJECTILE, ENEMY, function(a, b) exits = exits + 1 end)
    )lua");

    const EntityId projectile = makeCollider(vec3(0.0f), kProjectile, kEnemy);
    makeCollider(vec3(0.0f), kEnemy, kProjectile);
    buildPipeline();

    // Frame 1: overlap begins → one ENTER, no EXIT.
    tick();
    EXPECT_EQ(m_lua.lua()["enters"].get<int>(), 1);
    EXPECT_EQ(m_lua.lua()["exits"].get<int>(), 0);

    // Frame 2: still overlapping → no new ENTER (sustained), no EXIT.
    tick();
    EXPECT_EQ(m_lua.lua()["enters"].get<int>(), 1);
    EXPECT_EQ(m_lua.lua()["exits"].get<int>(), 0);

    // Move the projectile away → overlap ends this frame.
    IREntity::getComponent<C_WorldTransform>(projectile).translation_ = vec3(100.0f);
    tick();
    EXPECT_EQ(m_lua.lua()["enters"].get<int>(), 1);
    EXPECT_EQ(m_lua.lua()["exits"].get<int>(), 1);
}

// A handler keyed by a layer pair that never overlaps must not fire.
TEST_F(LuaCollisionBindingsTest, UnmatchedLayerPairDoesNotFire) {
    m_lua.lua().script(R"lua(
        fired = 0
        IRCollision.onOverlapEnter(PROJECTILE, PLAYER, function(a, b) fired = fired + 1 end)
    )lua");

    // Projectile overlaps the enemy, not the player — and projectile's mask
    // doesn't include PLAYER, so no PROJECTILE/PLAYER pair is ever produced.
    makeCollider(vec3(0.0f), kProjectile, kEnemy);
    makeCollider(vec3(0.0f), kEnemy, kProjectile);
    buildPipeline();
    tick();

    EXPECT_EQ(m_lua.lua()["fired"].get<int>(), 0);
}

// Registering a handler before the dispatch system exists raises a clear
// Lua-visible error rather than silently dropping it.
TEST(LuaCollisionBindingsUnregisteredTest, OnOverlapEnterRaisesWithoutDispatchSystem) {
    IRScript::LuaScript lua;
    IREntity::EntityManager entityManager;
    IRSystem::SystemManager systemManager;
    lua.bindLuaDrivenEcs(); // binds IRCollision, but no prefab system registered

    auto result = lua.lua().safe_script(
        "IRCollision.onOverlapEnter(1, 2, function(a, b) end)",
        sol::script_pass_on_error
    );
    EXPECT_FALSE(result.valid());
}

} // namespace

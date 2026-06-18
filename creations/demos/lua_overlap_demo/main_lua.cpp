// #1817 sample creation — Lua-facing collision/trigger overlap events.
//
// The C++ side spawns four invisible collider entities forming two arcade
// pairs (projectile/enemy and pickup/player), registers the three collision
// prefab systems + the two render-composite stages, and runs main.lua.
// `main.lua` registers an `IRCollision.onOverlapEnter` handler per pair and
// draws a HUD disc that turns green once its handler has fired — so the
// `--auto-screenshot` frame is a direct visual proof that both overlap
// callbacks ran from Lua. Console lines ("overlap: ...") give the same proof
// headlessly.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_video.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/update/components/component_collider_iso3d_aabb.hpp>
#include <irreden/update/components/component_collision_layer.hpp>
#include <irreden/update/components/component_contact_event.hpp>

// Prefab-system specializations — visible at the registerPrefabSystem<N>()
// call sites so System<N>::create() can instantiate.
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/update/systems/system_collision_event_clear.hpp>
#include <irreden/update/systems/system_collision_note_platform.hpp>
#include <irreden/update/systems/system_dispatch_lua_overlap.hpp>

using IRComponents::C_ColliderIso3DAABB;
using IRComponents::C_CollisionLayer;
using IRComponents::C_ContactEvent;
using IRComponents::C_WorldTransform;
using IRMath::vec3;
using IRMath::vec4;

namespace {

// Arcade layer bits — raw integers (NOT the music-demo COLLISION_LAYER_*
// vocabulary), shared with main.lua via Lua globals so there's one source.
constexpr std::uint32_t kLayerProjectile = 1u << 4;
constexpr std::uint32_t kLayerEnemy = 1u << 5;
constexpr std::uint32_t kLayerPickup = 1u << 6;
constexpr std::uint32_t kLayerPlayer = 1u << 7;

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, vec2(0, 0), 0.0f, "lua_overlap_demo_zoom1"},
};

IREntity::EntityId makeCollider(vec3 pos, std::uint32_t layer, std::uint32_t collidesWith) {
    return IREntity::createEntity(
        C_WorldTransform{pos, vec4(0.0f, 0.0f, 0.0f, 1.0f), vec3(1.0f)},
        C_ColliderIso3DAABB{vec3(0.5f)},
        C_CollisionLayer{layer, collidesWith, true},
        C_ContactEvent{}
    );
}

void registerLuaBindings() {
    IREngine::registerLuaBindings([](IRScript::LuaScript &script) {
        script.bindLuaDrivenEcs();

        // Collision overlap dispatch (#1817) + its producer + clear, plus the
        // two render-composite stages the HUD needs.
        script.registerPrefabSystems<
            IRSystem::COLLISION_NOTE_PLATFORM,
            IRSystem::COLLISION_DISPATCH_LUA_OVERLAP,
            IRSystem::COLLISION_EVENT_CLEAR,
            IRSystem::TRIXEL_TO_FRAMEBUFFER,
            IRSystem::FRAMEBUFFER_TO_SCREEN>();

        // Hand the arcade layer bits to main.lua.
        auto &lua = script.lua();
        lua["PROJECTILE"] = kLayerProjectile;
        lua["ENEMY"] = kLayerEnemy;
        lua["PICKUP"] = kLayerPickup;
        lua["PLAYER"] = kLayerPlayer;
    });
}

} // namespace

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: lua_overlap_demo");

    int autoWarmupFrames = 0;
    IRVideo::parseAutoScreenshotArgv(argc, argv, &autoWarmupFrames);

    registerLuaBindings();
    IREngine::init(argv[0], "config.lua");

    // Two overlapping arcade pairs. The colliders are invisible logic entities;
    // the HUD discs in main.lua are the only visuals.
    makeCollider(vec3(0.0f), kLayerProjectile, kLayerEnemy);
    makeCollider(vec3(0.0f), kLayerEnemy, kLayerProjectile);
    makeCollider(vec3(5.0f, 0.0f, 0.0f), kLayerPickup, kLayerPlayer);
    makeCollider(vec3(5.0f, 0.0f, 0.0f), kLayerPlayer, kLayerPickup);

    IREngine::runScript("main.lua");

    if (autoWarmupFrames > 0) {
        IRVideo::AutoScreenshotConfig cfg{};
        cfg.warmupFrames_ = autoWarmupFrames;
        cfg.settleFrames_ = 3;
        cfg.shots_ = kShots;
        cfg.numShots_ = sizeof(kShots) / sizeof(kShots[0]);
        IRSystem::appendToPipeline(
            IRTime::Events::RENDER,
            IRVideo::createAutoScreenshotSystem(cfg)
        );
    }

    IREngine::gameLoop();
    return 0;
}

// collision_overlap_demo (engine #1817) — proves the Lua-facing overlap
// callback surface end-to-end. The C++ side only registers the Lua bindings,
// the prefab systems, and a typed `IREntity.spawnCollidable` helper; `main.lua`
// spawns the entities, composes the UPDATE/RENDER pipelines, and registers the
// `IRCollision.onOverlapEnter/onOverlapExit` handlers. The handlers `print`
// when they fire, so a headless `--auto-screenshot` run is a deterministic,
// grep-able proof (see the demo's README / PR body).
//
// Acceptance proof: a projectile-vs-enemy ENTER + EXIT and a pickup-vs-player
// ENTER all dispatch to Lua with both entity ids, in the author's arg order.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>

// Components the demo spawns from Lua (the *_lua.hpp headers pull in the base
// component + the kHasLuaBinding trait).
#include <irreden/common/components/component_local_transform_lua.hpp>
#include <irreden/update/components/component_collider_iso3d_aabb_lua.hpp>
#include <irreden/update/components/component_collision_layer_lua.hpp>
#include <irreden/update/components/component_contact_event_lua.hpp>
#include <irreden/update/components/component_velocity_3d_lua.hpp>

// Prefab systems composed into the pipelines (visible so System<N>::create()
// can instantiate at the registerPrefabSystems<> call site).
#include <irreden/update/systems/system_velocity.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/update/systems/system_collision_event_clear.hpp>
#include <irreden/update/systems/system_collision_note_platform.hpp>
#include <irreden/update/systems/system_dispatch_lua_overlap.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>

namespace {

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, vec2(0, 0), 0.0f, "collision_overlap_demo"},
};

void registerLuaBindings() {
    IREngine::registerLuaBindings([](IRScript::LuaScript &script) {
        using namespace IRComponents;

        // Wire IRCollision + IRSystem/IRComponent/IRTime/... onto the state.
        script.bindLuaDrivenEcs();

        // `vec3` is a per-creation math usertype (not bound by the ECS surface);
        // main.lua needs it to construct C_LocalTransform.
        script.registerType<IRMath::vec3, IRMath::vec3(float, float, float)>(
            "vec3",
            "x",
            &IRMath::vec3::x,
            "y",
            &IRMath::vec3::y,
            "z",
            &IRMath::vec3::z
        );

        // Lua-construct the collision component bundle.
        script.registerTypesFromTraits<
            C_LocalTransform,
            C_ColliderIso3DAABB,
            C_CollisionLayer,
            C_ContactEvent,
            C_Velocity3D>();

        // IREntity.spawnCollidable(localTransform, collider, layer, contact,
        // velocity) — createEntity auto-adds C_WorldTransform so the entity
        // lands in the collision archetype.
        script.registerCreateEntityFunction<
            C_LocalTransform,
            C_ColliderIso3DAABB,
            C_CollisionLayer,
            C_ContactEvent,
            C_Velocity3D>("spawnCollidable");

        // Prefab systems main.lua composes via IRSystem.systemId(SystemName.X).
        // Registering DISPATCH_LUA_OVERLAP creates the C_OverlapContactBatch
        // singleton, which is what switches pair emission on in the producer.
        script.registerPrefabSystems<
            IRSystem::VELOCITY_3D,
            IRSystem::PROPAGATE_TRANSFORM,
            IRSystem::COLLISION_EVENT_CLEAR,
            IRSystem::COLLISION_NOTE_PLATFORM,
            IRSystem::DISPATCH_LUA_OVERLAP,
            IRSystem::TRIXEL_TO_FRAMEBUFFER,
            IRSystem::FRAMEBUFFER_TO_SCREEN>();
    });
}

} // namespace

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: collision_overlap_demo");

    int autoWarmupFrames = 0;
    registerLuaBindings();
    IREngine::init(argc, argv, "config.lua");
    autoWarmupFrames = IREngine::args().autoScreenshotWarmupFrames();
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

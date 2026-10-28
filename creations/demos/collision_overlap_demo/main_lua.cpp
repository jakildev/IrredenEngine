// collision_overlap_demo — proves the Lua-facing overlap
// callback surface end-to-end. The C++ side only registers the Lua bindings,
// the prefab systems, and a typed `IREntity.spawnCollidable` helper; `main.lua`
// spawns the entities, composes the UPDATE/RENDER pipelines, and registers the
// `IRCollision.onOverlapEnter/onOverlapExit` handlers. The handlers `print`
// when they fire, so a headless `--auto-screenshot` run is a deterministic,
// grep-able proof (see the demo's README / PR body).
//
// Acceptance proof: a projectile-vs-enemy ENTER + EXIT and a pickup-vs-player
// ENTER all dispatch to Lua with both entity ids, in the author's arg order.
//
// `--response-stress` runs `response_stress.lua` instead: 2000 collider-only
// bodies under COLLISION_RESPONSE drop onto 40 static boxes and a floor plane.
// `--auto-profile [N]` turns on frame timing and closes after N UPDATE ticks,
// printing `RESPONSE_STRESS bodies=<b> solids=<s> resting=<n>` in the stress
// scene; the per-system rows land in save_files/profile_report.txt.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>

// Components the demo spawns from Lua (the *_lua.hpp headers pull in the base
// component + the kHasLuaBinding trait).
#include <irreden/common/components/component_local_transform_lua.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/update/components/component_collider_iso3d_aabb_lua.hpp>
#include <irreden/update/components/component_collision_layer_lua.hpp>
#include <irreden/update/components/component_contact_event_lua.hpp>
#include <irreden/update/components/component_velocity_3d_lua.hpp>
#include <irreden/update/components/component_acceleration_3d.hpp>
#include <irreden/update/components/component_collision_response.hpp>
#include <irreden/script/lua_bindings_default.hpp>

// Prefab systems composed into the pipelines (visible so System<N>::create()
// can instantiate at the registerPrefabSystems<> call site).
#include <irreden/update/systems/system_acceleration.hpp>
#include <irreden/update/systems/system_collision_response.hpp>
#include <irreden/update/systems/system_velocity.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/update/systems/system_collision_event_clear.hpp>
#include <irreden/update/systems/system_collision_note_platform.hpp>
#include <irreden/update/systems/system_dispatch_lua_overlap.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_gui_canvas_to_screen.hpp>

#include <cstdio>

namespace {

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, vec2(0, 0), 0.0f, "collision_overlap_demo"},
};

constexpr int kStressBodiesX = 50;
constexpr int kStressBodiesY = 40;
constexpr int kStressBoxesX = 8;
constexpr int kStressBoxesY = 5;
constexpr float kStressFloorZ = 0.0f;
// Rest needs gravity * dt < restSpeed_: 60 * (1/60) = 1 < 2.
constexpr float kStressGravity = 60.0f;

int g_autoProfileTicks = 0;
int g_autoProfileCount = 0;
bool g_responseStress = false;

void registerArgs() {
    IRArgs::Parser &args = IREngine::args();
    args.flag(
        "--response-stress",
        "Run the COLLISION_RESPONSE stress scene (2000 bodies, 40 boxes) instead of main.lua"
    );
    args.optionalInt(
        "--auto-profile",
        "Per-system frame timing for N UPDATE ticks (default 300 when bare); writes the profile "
        "report on exit",
        300
    );
}

// Static boxes resting on the floor plane (+z is down), in an 8 x 5 grid.
void spawnStressBoxes() {
    using namespace IRComponents;
    for (int bx = 0; bx < kStressBoxesX; ++bx) {
        for (int by = 0; by < kStressBoxesY; ++by) {
            IREntity::createEntity(
                C_LocalTransform{
                    vec3(-28.0f + 8.0f * bx, -16.0f + 8.0f * by, kStressFloorZ - 1.0f)
                },
                C_ColliderIso3DAABB{vec3(2.0f, 2.0f, 1.0f)},
                C_CollisionLayer{COLLISION_LAYER_DEFAULT, 0u, true}
            );
        }
    }
}

// Single-voxel bodies over the boxes at staggered heights, with a
// deterministic spread of restitution, friction and sideways drift.
void spawnStressBodies() {
    using namespace IRComponents;
    for (int ix = 0; ix < kStressBodiesX; ++ix) {
        for (int iy = 0; iy < kStressBodiesY; ++iy) {
            const int n = ix * kStressBodiesY + iy;
            IREntity::createEntity(
                C_LocalTransform{
                    vec3(-30.6f + 1.25f * ix, -24.4f + 1.25f * iy, -6.0f - 0.5f * (n % 8))
                },
                C_Velocity3D{0.5f * static_cast<float>((n % 5) - 2), 0.0f, 0.0f},
                C_Acceleration3D{0.0f, 0.0f, kStressGravity},
                C_ColliderIso3DAABB{vec3(0.5f)},
                C_CollisionLayer{COLLISION_LAYER_PARTICLE, COLLISION_LAYER_DEFAULT, true},
                C_CollisionResponse{
                    0.2f + 0.1f * static_cast<float>(n % 4),
                    0.1f + 0.1f * static_cast<float>(n % 3),
                    true,
                    kStressFloorZ
                }
            );
        }
    }
}

void printResponseStressSummary() {
    int bodies = 0;
    int resting = 0;
    IREntity::forEachComponent<IRComponents::C_CollisionResponse>(
        [&](const IRComponents::C_CollisionResponse &response) {
            ++bodies;
            resting += response.resting_ ? 1 : 0;
        }
    );
    const auto *response =
        IRSystem::getSystemParams<IRSystem::System<IRSystem::COLLISION_RESPONSE>>(
            IRSystem::findSystem(IRSystem::COLLISION_RESPONSE)
        );
    std::printf(
        "RESPONSE_STRESS bodies=%d solids=%zu resting=%d\n",
        bodies,
        response->solidCount(),
        resting
    );
    std::fflush(stdout);
}

// Counts UPDATE ticks, not render frames, so N is the number of profiled
// UPDATE calls whatever the render rate.
IRSystem::SystemId createAutoProfileCloser() {
    return IRSystem::createSystem<IRComponents::C_Name>(
        "AutoProfile",
        [](IRComponents::C_Name &) {},
        []() {
            if (++g_autoProfileCount != g_autoProfileTicks) {
                return;
            }
            IR_LOG_INFO("Auto-profile: {} UPDATE ticks collected, exiting", g_autoProfileTicks);
            if (g_responseStress) {
                printResponseStressSummary();
            }
            IRWindow::closeWindow();
        }
    );
}

void registerLuaBindings() {
    IREngine::registerLuaBindings([](IRScript::LuaScript &script) {
        using namespace IRComponents;

        // Wire IRCollision + IRSystem/IRComponent/IRTime/... onto the state.
        script.bindLuaDrivenEcs();

        IRScript::registerStandardBindings(script);

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

        script.lua()["IREntity"]["spawnResponseStress"] = []() {
            spawnStressBoxes();
            spawnStressBodies();
        };

        // Prefab systems main.lua composes via IRSystem.systemId(SystemName.X).
        // Registering DISPATCH_LUA_OVERLAP creates the C_OverlapContactBatch
        // singleton, which is what switches pair emission on in the producer.
        script.registerPrefabSystems<
            IRSystem::ACCELERATION_3D,
            IRSystem::VELOCITY_3D,
            IRSystem::PROPAGATE_TRANSFORM,
            IRSystem::COLLISION_EVENT_CLEAR,
            IRSystem::COLLISION_NOTE_PLATFORM,
            IRSystem::DISPATCH_LUA_OVERLAP,
            IRSystem::COLLISION_RESPONSE,
            IRSystem::TRIXEL_TO_FRAMEBUFFER,
            IRSystem::FRAMEBUFFER_TO_SCREEN,
            IRSystem::GUI_CANVAS_TO_SCREEN>();
    });
}

} // namespace

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: collision_overlap_demo");

    int autoWarmupFrames = 0;
    registerArgs();
    registerLuaBindings();
    IREngine::init(argc, argv, "config.lua");
    const IRArgs::Parser &args = IREngine::args();
    autoWarmupFrames = args.autoScreenshotWarmupFrames();
    g_responseStress = args.getFlag("--response-stress");
    if (args.wasProvided("--auto-profile")) {
        g_autoProfileTicks = args.getInt("--auto-profile");
    }
    IREngine::runScript(g_responseStress ? "response_stress.lua" : "main.lua");

    if (g_autoProfileTicks > 0) {
        IREngine::enableFrameTiming(true);
        IRSystem::appendToPipeline(IRTime::Events::UPDATE, createAutoProfileCloser());
    }

    if (autoWarmupFrames > 0) {
        // The RENDER pipeline was already registered by the Lua-composed
        // scripts above, so this appends post-hoc via appendToPipeline rather
        // than pushing into a not-yet-registered pipeline vector — the
        // one-table-vector shape appendAutoScreenshotIfRequested assumes
        // doesn't apply here (engine/video/CLAUDE.md "Auto-screenshot
        // helper").
        IRVideo::AutoScreenshotConfig cfg{};
        cfg.warmupFrames_ = autoWarmupFrames;
        cfg.settleFrames_ = 3;
        IRVideo::setAutoScreenshotShots(cfg, kShots);
        IRSystem::appendToPipeline(
            IRTime::Events::RENDER,
            IRVideo::createAutoScreenshotSystem(cfg)
        );
    }

    IREngine::gameLoop();
    return 0;
}

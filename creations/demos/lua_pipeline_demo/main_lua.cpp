// T-102 sample creation. Entire `initSystems` lives in `scripts/main.lua`:
// the C++ side only registers the Lua-binding callback and runs the
// script. The callback registers the prefab systems + the modifier
// resolver pipeline so Lua can spell them via
// `IRSystem.systemId(SystemName.X)`. Lua then composes the UPDATE /
// RENDER pipelines via `IRSystem.registerPipeline(...)`, mixing prefab
// systems with one Lua-defined system, and exercises `IRModifier.add`
// against a Lua-defined `Hp` component to verify resolved-field
// composition.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>

#include <irreden/common/components/component_position_3d_lua.hpp>
#include <irreden/common/modifier.hpp>

// Prefab-system specializations — must be visible at the
// `registerPrefabSystem<N>()` call sites so `System<N>::create()` can
// instantiate. The modifier-resolver systems are pulled in by
// modifier.hpp above.
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/update/systems/system_lifetime.hpp>
#include <irreden/update/systems/system_update_positions_global.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>

namespace {

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, vec2(0, 0), "lua_pipeline_demo_zoom1"},
};

void registerLuaBindings() {
    IREngine::registerLuaBindings([](IRScript::LuaScript &script) {
        // Required to use `IRComponent.register`, `IRSystem.registerSystem`,
        // `IRSystem.registerPipeline`, `IRModifier.*`, and `IRTime.*` from
        // Lua. The single call wires every Lua-driven ECS surface.
        script.bindLuaDrivenEcs();

        // Component pack: bind C_Position3D so the Lua-defined system
        // can read it via `arch.C_Position3D:at(i)`.
        script.registerType<IRComponents::C_Position3D, IRComponents::C_Position3D(vec3)>(
            "C_Position3D",
            "x",
            [](IRComponents::C_Position3D &p) { return p.pos_.x; },
            "y",
            [](IRComponents::C_Position3D &p) { return p.pos_.y; },
            "z",
            [](IRComponents::C_Position3D &p) { return p.pos_.z; }
        );

        // Make every prefab system the demo wants to compose into
        // pipelines available to Lua via `IRSystem.systemId(SystemName.X)`.
        script.registerPrefabSystems<
            IRSystem::INPUT_KEY_MOUSE,
            IRSystem::LIFETIME,
            IRSystem::GLOBAL_POSITION_3D,
            IRSystem::FRAMEBUFFER_TO_SCREEN>();

        // Modifier resolver pipeline. `registerResolverPipeline()` does
        // three things in one call: creates the singleton globals
        // entity, registers the pre-destroy hook that sweeps modifiers
        // attributed to a destroyed source, and creates the six
        // resolver SystemIds. Caching those SystemIds under their enum
        // names lets Lua spell them via `SystemName.MODIFIER_*` without
        // re-creating duplicates.
        const auto resolver = IRPrefab::Modifier::registerResolverPipeline();
        script.registerPrefabSystemId(IRSystem::MODIFIER_DECAY, resolver.modifierDecay_);
        script.registerPrefabSystemId(
            IRSystem::GLOBAL_MODIFIER_DECAY,
            resolver.globalModifierDecay_
        );
        script.registerPrefabSystemId(
            IRSystem::LAMBDA_MODIFIER_DECAY,
            resolver.lambdaModifierDecay_
        );
        script.registerPrefabSystemId(
            IRSystem::MODIFIER_RESOLVE_GLOBAL,
            resolver.modifierResolveGlobal_
        );
        script.registerPrefabSystemId(
            IRSystem::MODIFIER_RESOLVE_EXEMPT,
            resolver.modifierResolveExempt_
        );
        script.registerPrefabSystemId(
            IRSystem::MODIFIER_RESOLVE_LAMBDA,
            resolver.modifierResolveLambda_
        );
    });
}

} // namespace

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: lua_pipeline_demo");

    int autoWarmupFrames = 0;
    IRVideo::parseAutoScreenshotArgv(argc, argv, &autoWarmupFrames);

    registerLuaBindings();
    IREngine::init(argv[0], "config.lua");
    IREngine::runScript("main.lua");

    if (autoWarmupFrames > 0) {
        IRVideo::AutoScreenshotConfig cfg{};
        cfg.warmupFrames_ = autoWarmupFrames;
        cfg.settleFrames_ = 3;
        cfg.shots_ = kShots;
        cfg.numShots_ = sizeof(kShots) / sizeof(kShots[0]);
        IRSystem::registerPipeline(
            IRTime::Events::RENDER,
            {IRVideo::createAutoScreenshotSystem(cfg)}
        );
    }

    IREngine::gameLoop();
    return 0;
}

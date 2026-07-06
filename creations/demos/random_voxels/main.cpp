// random_voxels — interactive voxel-recoloring demo (#17 / PR #2210).
//
// Builds an isometric field of voxel cubes and binds a family of one-shot
// recolor commands to keys. Each runs the #2210 query primitive —
// `IRSystem::executeQuery<C_VoxelSetNew, Exclude<C_Locked>>`, the run-now
// counterpart to a system tick — and rewrites every active voxel's color:
//   R  RANDOMIZE VOXELS  (the engine prefab Command<RANDOMIZE_VOXELS>)
//   H  CYCLE CHANNELS    (r,g,b) -> (g,b,r), a cheap hue-rotation-like shuffle
//   I  INVERT            255 - each channel
//   M  GRAYSCALE         Rec.601 luminance
// H / I / M are composed right here in the demo from the same public primitive,
// showing it as a reusable building block — no engine changes needed to add a
// new "visualize a mutation" command.
//
// Four corner pillars are tagged `C_Locked`, so every recolor skips them (the
// query's `Exclude<C_Locked>` filter) and they stay white while the field
// changes: a live, self-explaining demonstration of the exclude semantics.
//
// The help text in the top-left is the engine's built-in command-list overlay
// (`IRCommand::buildCommandListText`, rasterized by TEXT_TO_TRIXEL whenever the
// GUI is visible). It lists every registered key binding, so the scene is
// self-documenting with no bespoke HUD code. This is the growth seam: any new
// scene-mutation command registered in `initCommands()` automatically appears
// in that overlay — add a command, get a labelled hotkey for free.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_command.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

// COMPONENTS
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_locked.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

// SYSTEMS
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/render/systems/system_lod_update.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_build_light_occlusion_grid.hpp>
#include <irreden/render/systems/system_compute_voxel_ao.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>
#include <irreden/render/systems/system_compute_sun_shadow.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_lighting_to_trixel.hpp>
#include <irreden/render/systems/system_fog_to_trixel.hpp>
#include <irreden/render/systems/system_text_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_sprites_to_screen.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/camera_controls.hpp>

// COMMANDS
#include <irreden/render/commands/command_toggle_gui.hpp>
#include <irreden/voxel/commands/command_randomize_voxels.hpp>

#include <list>

using namespace IRComponents;

namespace {

// Isometric field: a gridN x gridN carpet of small cubes, centered on origin.
constexpr int kGridN = 6;                     // cubes per side
constexpr IRMath::ivec3 kCubeDims{4, 4, 4};   // voxels per field cube
constexpr IRMath::ivec3 kPillarDims{4, 12, 4}; // taller corner landmark
constexpr float kSpacing = 6.0f;              // world-unit gap between cube centers

// Neutral color for the locked corner pillars — random recoloring never lands
// on flat white, so persistent white pillars read as intentionally fixed.
constexpr IRMath::Color kLockedColor{235, 238, 245, 255};

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {2.0f, IRMath::vec2(0, 0), 0.0f, "zoom2_random_voxels"},
    {4.0f, IRMath::vec2(0, 0), 0.0f, "zoom4_random_voxels"},
};

int g_autoWarmupFrames = 0;

// Per-voxel color transforms for the demo-local recolor commands. Each is a
// pure function of the input color; alpha is preserved by registerVoxelRecolor,
// so these only need to move RGB.
using ColorFn = IRMath::Color (*)(IRMath::Color);

IRMath::Color cycleChannels(IRMath::Color c) {
    return IRMath::Color{c.green_, c.blue_, c.red_, c.alpha_};
}

IRMath::Color invertColor(IRMath::Color c) {
    return IRMath::Color{
        static_cast<uint8_t>(255 - c.red_),
        static_cast<uint8_t>(255 - c.green_),
        static_cast<uint8_t>(255 - c.blue_),
        c.alpha_
    };
}

IRMath::Color grayscaleColor(IRMath::Color c) {
    // Integer Rec.601 luminance: (77r + 150g + 29b) / 256 (weights sum to 256,
    // so a white voxel maps back to 255). Stays in integer math — no glm/std.
    const auto lum =
        static_cast<uint8_t>((77 * c.red_ + 150 * c.green_ + 29 * c.blue_) >> 8);
    return IRMath::Color{lum, lum, lum, c.alpha_};
}

// Bind a PRESSED key to a one-shot recolor over every active voxel, mirroring
// Command<RANDOMIZE_VOXELS>' contract (skip C_Locked sets, skip carved alpha-0
// voxels, preserve alpha) but parameterized by `transform`. Passing `name` to
// the manager registers the binding in the top-left command overlay.
void registerVoxelRecolor(int button, const char *name, ColorFn transform) {
    IRCommand::getCommandManager().createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        button,
        [transform]() {
            IRSystem::executeQuery<C_VoxelSetNew, IRSystem::Exclude<C_Locked>>(
                [transform](C_VoxelSetNew &set) {
                    set.editVoxels([transform](int, C_Voxel &voxel, IRMath::vec3) {
                        if (voxel.color_.alpha_ == 0) {
                            return; // carved / inactive — keep the mask stable
                        }
                        const uint8_t alpha = voxel.color_.alpha_;
                        voxel.color_ = transform(voxel.color_);
                        voxel.color_.alpha_ = alpha; // preserve original alpha
                    });
                }
            );
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        name
    );
}

} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: random_voxels");
    IREngine::init(argc, argv);
    g_autoWarmupFrames = IREngine::args().autoScreenshotWarmupFrames();

    initSystems();
    initCommands();
    initEntities();

    // Show the command-list overlay from the first frame so the hotkeys are
    // discoverable without pressing anything; TOGGLE GUI (G) hides it.
    IRRender::setGuiVisible(true);

    IREngine::gameLoop();
    return 0;
}

// INPUT feeds key/mouse state; UPDATE keeps the voxel sets bound to the canvas
// pool (UPDATE_VOXEL_SET_CHILDREN sets each set's canvasEntity_ and uploads
// positions, which RANDOMIZE_VOXELS' `editVoxels` resync writes back into);
// RENDER mirrors scene_reset's proven full lighting stack, with TEXT_TO_TRIXEL
// inserted before the framebuffer composite so the overlay lands on screen.
void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );

    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {
            IRSystem::createSystem<IRSystem::LOD_UPDATE>(),
            IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
            IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
            IRSystem::createSystem<IRSystem::REBUILD_GRID_VOXELS>(),
        }
    );

    std::list<IRSystem::SystemId> renderPipeline = IRPrefab::Camera::standardControlSystems();
    renderPipeline.insert(
        renderPipeline.end(),
        {
            IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
            IRSystem::createSystem<IRSystem::BUILD_LIGHT_OCCLUSION_GRID>(),
            IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
            IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::COMPUTE_VOXEL_AO>(),
            IRSystem::createSystem<IRSystem::BAKE_SUN_SHADOW_MAP>(),
            IRSystem::createSystem<IRSystem::COMPUTE_SUN_SHADOW>(),
            IRSystem::createSystem<IRSystem::COMPUTE_LIGHT_VOLUME>(),
            IRSystem::createSystem<IRSystem::LIGHTING_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::FOG_TO_TRIXEL>(),
            // Overlay text -> gui canvas -> composite: TEXT_TO_TRIXEL must run
            // before TRIXEL_TO_FRAMEBUFFER for the HUD to land on screen.
            IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
            IRSystem::createSystem<IRSystem::SPRITE_TO_SCREEN>(),
        }
    );

    if (g_autoWarmupFrames > 0) {
        IRVideo::AutoScreenshotConfig cfg{};
        cfg.warmupFrames_ = g_autoWarmupFrames;
        cfg.settleFrames_ = 3;
        cfg.shots_ = kShots;
        cfg.numShots_ = sizeof(kShots) / sizeof(kShots[0]);
        renderPipeline.push_back(IRVideo::createAutoScreenshotSystem(cfg));
    }

    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}

// Scene-mutation commands come first so they head the top-left overlay; the
// standard camera keys (WASD / +- / ESC) fill out the rest. Register a new
// mutating command here and it auto-appears in the overlay with its hotkey —
// the intended growth seam for more "visualize a mutation" commands.
void initCommands() {
    // R: full random recolor — the engine prefab Command<RANDOMIZE_VOXELS>.
    IRCommand::createCommand<IRCommand::RANDOMIZE_VOXELS>(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonR
    );
    // H / I / M: more one-shot recolors, composed from the same #2210 query
    // primitive but parameterized by a per-color transform.
    registerVoxelRecolor(IRInput::kKeyButtonH, "CYCLE CHANNELS", cycleChannels);
    registerVoxelRecolor(IRInput::kKeyButtonI, "INVERT", invertColor);
    registerVoxelRecolor(IRInput::kKeyButtonM, "GRAYSCALE", grayscaleColor);

    IRCommand::createCommand<IRCommand::TOGGLE_GUI>(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonG
    );

    IRPrefab::Camera::registerStandardKeyboardCommands();
}

// A gridN x gridN carpet of gradient-colored cubes. The four corners are taller
// pillars tagged C_Locked, so RANDOMIZE_VOXELS' `Exclude<C_Locked>` skips them
// and they stay white while the rest of the field reshuffles.
void initEntities() {
    const float half = (kGridN - 1) * 0.5f;
    // Two-axis gradient so the initial field reads as intentional and the
    // recolor is an obvious shuffle away from it.
    const auto channel = [](int v) {
        return static_cast<uint8_t>(40 + (200 * v) / (kGridN - 1));
    };
    for (int i = 0; i < kGridN; ++i) {
        for (int j = 0; j < kGridN; ++j) {
            const IRMath::vec3 pos{(i - half) * kSpacing, 0.0f, (j - half) * kSpacing};

            const bool corner = (i == 0 || i == kGridN - 1) && (j == 0 || j == kGridN - 1);
            if (corner) {
                IREntity::createEntity(
                    C_LocalTransform{pos},
                    C_VoxelSetNew{kPillarDims, kLockedColor, true},
                    C_Locked{}
                );
                continue;
            }

            const IRMath::Color color{channel(i), channel(j), 170, 255};
            IREntity::createEntity(C_LocalTransform{pos}, C_VoxelSetNew{kCubeDims, color, true});
        }
    }
}

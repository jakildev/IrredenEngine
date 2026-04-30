#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_video.hpp>

// Components
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/render/components/component_text_segment.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_text_style.hpp>

// Systems
#include <irreden/update/systems/system_update_positions_global.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_text_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_camera_mouse_pan.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>

// Commands
#include <irreden/common/command_suite_camera.hpp>
#include <irreden/common/command_suite_capture.hpp>

// Modifier framework
#include <irreden/common/modifier.hpp>

#include <cmath>
#include <list>

// ---------------------------------------------------------------------------
// Demo-wide global state
// ---------------------------------------------------------------------------
namespace IRModifierDemo {

constexpr int kNumCubes = 8;
constexpr float kBaseSpeed = 0.05f;
constexpr float kMaxX = 30.0f;
constexpr float kMinX = -30.0f;
constexpr float kRowSpacingY = 8.0f;

IRComponents::FieldBindingId g_speedField = IRComponents::kInvalidFieldId;
IREntity::EntityId g_cubes[kNumCubes + 1] = {};  // 1-indexed [1..8]
IREntity::EntityId g_sourceEntity7 = IREntity::kNullEntity;

bool g_lambda6Active = false;
bool g_source7Killed = false;
uint32_t g_tick = 0;

// One color per cube (index 0 unused).
const Color kCubeColors[kNumCubes + 1] = {
    {},
    {100, 220, 220, 255},  // [1] cyan    — Haste
    {220, 140,  60, 255},  // [2] orange  — Stun
    { 80, 200,  80, 255},  // [3] green   — Slow
    {160,  80, 220, 255},  // [4] purple  — Stack
    {220, 220,  60, 255},  // [5] yellow  — GlobalSlow
    {220,  80, 180, 255},  // [6] magenta — LambdaSine
    {220,  60,  60, 255},  // [7] red     — SourceKill
    {200, 200, 200, 255},  // [8] white   — Clamp
};

} // namespace IRModifierDemo

// ---------------------------------------------------------------------------
// Auto-screenshot configuration
// ---------------------------------------------------------------------------
namespace {
constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {2.0f, vec2(0, 0), "zoom2"},
    {4.0f, vec2(0, 0), "zoom4"},
};
} // namespace

int g_autoWarmupFrames = 0;

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &g_autoWarmupFrames);
    IR_LOG_INFO("Starting creation: modifier_demo");
    IREngine::init(argv[0]);
    initSystems();
    initCommands();
    initEntities();
    IREngine::gameLoop();
    return 0;
}

// ---------------------------------------------------------------------------
// Systems
// ---------------------------------------------------------------------------
void initSystems() {
    using namespace IRComponents;

    IRModifierDemo::g_speedField = IRPrefab::Modifier::registerField("speed");
    auto resolver = IRPrefab::Modifier::registerResolverPipeline();

    // Seed + input: seeds the speed field each frame and checks key presses.
    // beginTick fires once per pipeline execution, before per-entity ticks.
    auto seedId = IRSystem::createSystem<C_ResolvedFields>(
        "ModDemo_Seed",
        [](C_ResolvedFields &rf) {
            rf.reset(IRModifierDemo::g_speedField, IRModifierDemo::kBaseSpeed);
        },
        []() {
            ++IRModifierDemo::g_tick;

            // [1] Haste: MULTIPLY 1.5x for 300 ticks
            if (IRInput::checkKeyMouseButton(IRInput::kKeyButton1, IRInput::PRESSED)) {
                IRPrefab::Modifier::push(
                    IRModifierDemo::g_cubes[1], IRModifierDemo::g_speedField,
                    TransformKind::MULTIPLY, 1.5f,
                    IRModifierDemo::g_cubes[1], 300
                );
            }
            // [2] Stun: SET 0 for 180 ticks
            if (IRInput::checkKeyMouseButton(IRInput::kKeyButton2, IRInput::PRESSED)) {
                IRPrefab::Modifier::push(
                    IRModifierDemo::g_cubes[2], IRModifierDemo::g_speedField,
                    TransformKind::SET, 0.0f,
                    IRModifierDemo::g_cubes[2], 180
                );
            }
            // [3] Slow: MULTIPLY 0.3x for 300 ticks
            if (IRInput::checkKeyMouseButton(IRInput::kKeyButton3, IRInput::PRESSED)) {
                IRPrefab::Modifier::push(
                    IRModifierDemo::g_cubes[3], IRModifierDemo::g_speedField,
                    TransformKind::MULTIPLY, 0.3f,
                    IRModifierDemo::g_cubes[3], 300
                );
            }
            // [4] Stack: Haste then Slow = net 0.45x for 300 ticks
            if (IRInput::checkKeyMouseButton(IRInput::kKeyButton4, IRInput::PRESSED)) {
                IRPrefab::Modifier::push(
                    IRModifierDemo::g_cubes[4], IRModifierDemo::g_speedField,
                    TransformKind::MULTIPLY, 1.5f,
                    IRModifierDemo::g_cubes[4], 300
                );
                IRPrefab::Modifier::push(
                    IRModifierDemo::g_cubes[4], IRModifierDemo::g_speedField,
                    TransformKind::MULTIPLY, 0.3f,
                    IRModifierDemo::g_cubes[4], 300
                );
            }
            // [5] GlobalSlow: MULTIPLY 0.5x on all cubes via singleton for 600 ticks
            if (IRInput::checkKeyMouseButton(IRInput::kKeyButton5, IRInput::PRESSED)) {
                IRPrefab::Modifier::pushGlobal(
                    IRModifierDemo::g_speedField,
                    TransformKind::MULTIPLY, 0.5f,
                    IRPrefab::Modifier::globalsEntity(), 600
                );
            }
            // [6] LambdaSine: toggle sinusoidal speed on cube 6
            if (IRInput::checkKeyMouseButton(IRInput::kKeyButton6, IRInput::PRESSED)) {
                if (!IRModifierDemo::g_lambda6Active) {
                    IRPrefab::Modifier::pushLambda(
                        IRModifierDemo::g_cubes[6], IRModifierDemo::g_speedField,
                        [](float v) -> float {
                            float phase = static_cast<float>(IRModifierDemo::g_tick) * 0.05f;
                            return v * (1.0f + std::sin(phase));
                        },
                        IRModifierDemo::g_cubes[6], -1
                    );
                    IRModifierDemo::g_lambda6Active = true;
                } else {
                    IRPrefab::Modifier::removeBySource(IRModifierDemo::g_cubes[6]);
                    IRModifierDemo::g_lambda6Active = false;
                }
            }
            // [7] SourceKill: destroy the source entity that holds cube 7's Haste.
            // registerResolverPipeline() registers a pre-destroy hook that
            // auto-sweeps all modifier vectors for the destroyed entity's source ID.
            if (IRInput::checkKeyMouseButton(IRInput::kKeyButton7, IRInput::PRESSED)
                && !IRModifierDemo::g_source7Killed
                && IRModifierDemo::g_sourceEntity7 != IREntity::kNullEntity)
            {
                IREntity::destroyEntity(IRModifierDemo::g_sourceEntity7);
                IRModifierDemo::g_source7Killed = true;
            }
            // [8] Clamp: MULTIPLY 2x, then CLAMP_MAX base speed — net effect: no change.
            // Demonstrates that a clamp modifier can cancel an acceleration.
            if (IRInput::checkKeyMouseButton(IRInput::kKeyButton8, IRInput::PRESSED)) {
                IRPrefab::Modifier::push(
                    IRModifierDemo::g_cubes[8], IRModifierDemo::g_speedField,
                    TransformKind::MULTIPLY, 2.0f,
                    IRModifierDemo::g_cubes[8], 300
                );
                IRPrefab::Modifier::push(
                    IRModifierDemo::g_cubes[8], IRModifierDemo::g_speedField,
                    TransformKind::CLAMP_MAX, IRModifierDemo::kBaseSpeed,
                    IRModifierDemo::g_cubes[8], 300
                );
            }
        }
    );

    // Consume: read resolved speed and advance the cube's X position.
    auto consumeId = IRSystem::createSystem<C_Position3D, C_ResolvedFields>(
        "ModDemo_Consume",
        [](C_Position3D &pos, C_ResolvedFields &rf) {
            float speed = rf.get(IRModifierDemo::g_speedField, IRModifierDemo::kBaseSpeed);
            pos.pos_.x += speed;
            if (pos.pos_.x > IRModifierDemo::kMaxX) {
                pos.pos_.x = IRModifierDemo::kMinX;
            }
        }
    );

    IRSystem::registerPipeline(IRTime::Events::UPDATE, {
        seedId,
        resolver.modifierDecay_,
        resolver.globalModifierDecay_,
        resolver.modifierResolveGlobal_,
        resolver.modifierResolveLambda_,
        consumeId,
        IRSystem::createSystem<IRSystem::GLOBAL_POSITION_3D>(),
    });

    IRSystem::registerPipeline(IRTime::Events::INPUT, {
        IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>(),
    });

    std::list<IRSystem::SystemId> renderPipeline = {
        IRSystem::createSystem<IRSystem::CAMERA_MOUSE_PAN>(),
        IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
        IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
        IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>(),
        IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
        IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
    };

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

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
void initCommands() {
    IRCommand::registerCameraCommands();
    IRCommand::registerCaptureCommands();
}

// ---------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------
void initEntities() {
    using namespace IRComponents;

    // Eight cubes, one per row, each at a different Y to form horizontal tracks.
    for (int i = 1; i <= IRModifierDemo::kNumCubes; ++i) {
        float y = static_cast<float>(i - 1) * IRModifierDemo::kRowSpacingY;
        vec3 pos{IRModifierDemo::kMinX, y, 0.0f};
        Color color = IRModifierDemo::kCubeColors[i];

        if (i == 6) {
            IRModifierDemo::g_cubes[i] = IREntity::createEntity(
                C_Position3D{pos},
                C_ShapeDescriptor{IRRender::ShapeType::BOX, vec4(5, 5, 5, 0), color},
                C_Modifiers{},
                C_LambdaModifiers{},
                C_ResolvedFields{}
            );
        } else {
            IRModifierDemo::g_cubes[i] = IREntity::createEntity(
                C_Position3D{pos},
                C_ShapeDescriptor{IRRender::ShapeType::BOX, vec4(5, 5, 5, 0), color},
                C_Modifiers{},
                C_ResolvedFields{}
            );
        }
    }

    IRModifierDemo::g_sourceEntity7 = IREntity::createEntity();
    IRPrefab::Modifier::push(
        IRModifierDemo::g_cubes[7], IRModifierDemo::g_speedField,
        TransformKind::MULTIPLY, 2.0f,
        IRModifierDemo::g_sourceEntity7, -1
    );

    // HUD: key legend displayed at top-left in screen space.
    IREntity::createEntity(
        C_TextSegment{
            "[1] Haste       MULTIPLY x1.5  (300t)\n"
            "[2] Stun        SET 0          (180t)\n"
            "[3] Slow        MULTIPLY x0.3  (300t)\n"
            "[4] Stack       x1.5 * x0.3   (300t)\n"
            "[5] GlobalSlow  x0.5 all cubes (600t)\n"
            "[6] LambdaSine  sine wave       toggle\n"
            "[7] SourceKill  destroy Haste source\n"
            "[8] Clamp       x2 capped at base"
        },
        C_GuiPosition{4, 4},
        C_TextStyle{}
    );
}

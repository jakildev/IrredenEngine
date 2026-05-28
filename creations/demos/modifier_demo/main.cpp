#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_video.hpp>

// Components
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/render/components/component_text_segment.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_text_style.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>

// Systems
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_text_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/camera_controls.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>

// Commands
#include <irreden/common/command_suite_capture.hpp>
#include <irreden/render/commands/command_toggle_gui.hpp>
#include <irreden/render/commands/command_gui_zoom.hpp>

// Modifier framework
#include <irreden/common/modifier.hpp>

#include <cmath>
#include <cstdio>
#include <list>

// ---------------------------------------------------------------------------
// Demo-wide global state
// ---------------------------------------------------------------------------
namespace IRModifierDemo {

constexpr int kNumCubes = 8;
constexpr float kBaseSpeed = 0.05f;
// Each cube animates along the world (+x, -y) diagonal so its motion is
// purely horizontal on the iso screen (iso.x changes, iso.y stays fixed).
// kAnimRange is the half-range of that animation parameter.
constexpr float kAnimRange = 25.0f;
// Per-row vertical screen offset, expressed in iso-y trixel units.
constexpr float kRowIsoSpacing = 18.0f;
constexpr float kCubeSize = 3.5f;

// One static label per cube; also used as the row legend in the HUD so the
// viewer can read what each row does without opening the help overlay.
struct CubeInfo {
    const char *name_;   // short name, 10 chars or fewer to fit the HUD column
    const char *effect_; // one-shot description of what pressing the digit does
};

constexpr CubeInfo kCubes[kNumCubes + 1] = {
    {},
    {"Haste", "MULT x1.5"},        // [1]
    {"Stun", "SET 0"},             // [2]
    {"Slow", "MULT x0.3"},         // [3]
    {"Stack", "x1.5*x0.3"},        // [4]
    {"GlobalSlow", "global x0.5"}, // [5]
    {"LambdaSine", "sine wave"},   // [6]
    {"SrcKill", "kill src"},       // [7]
    {"Clamp", "x2 cap"},           // [8]
};

// Per-cube color (index 0 unused).
constexpr Color kCubeColors[kNumCubes + 1] = {
    {},
    {100, 220, 220, 255}, // [1] cyan    - Haste
    {220, 140, 60, 255},  // [2] orange  - Stun
    {80, 200, 80, 255},   // [3] green   - Slow
    {160, 80, 220, 255},  // [4] purple  - Stack
    {220, 220, 60, 255},  // [5] yellow  - GlobalSlow
    {220, 80, 180, 255},  // [6] magenta - LambdaSine
    {220, 60, 60, 255},   // [7] red     - SourceKill
    {200, 200, 200, 255}, // [8] white   - Clamp
};

IRComponents::FieldBindingId g_speedField = IRComponents::kInvalidFieldId;
IREntity::EntityId g_cubes[kNumCubes + 1] = {}; // 1-indexed [1..8]
IREntity::EntityId g_sourceEntity7 = IREntity::kNullEntity;
IREntity::EntityId g_hudStatusEntity = IREntity::kNullEntity;
IREntity::EntityId g_hudHelpEntity = IREntity::kNullEntity;

bool g_lambda6Active = false;
bool g_source7Killed = false;
bool g_showHelp = false;
uint32_t g_tick = 0;

// Per-row world center for cube i (1..kNumCubes). With each cube animating
// along the (+x, -y) world diagonal, iso.y for the row stays fixed at
// `rowIsoY(i)` while iso.x sweeps across the screen.
inline float rowIsoY(int i) {
    return (static_cast<float>(i) - (kNumCubes + 1) * 0.5f) * kRowIsoSpacing;
}

// Solve for the row's world center such that pos3DtoPos2DIso(center) =
// (0, rowIsoY(i)). With z = 0, iso.x = -x + y = 0, so y = x; and
// iso.y = -x - y = -2x, so x = -rowIsoY(i)/2.
inline vec3 rowWorldCenter(int i) {
    float xy = -rowIsoY(i) * 0.5f;
    return vec3{xy, xy, 0.0f};
}

} // namespace IRModifierDemo

// ---------------------------------------------------------------------------
// Auto-screenshot configuration
// ---------------------------------------------------------------------------
// Screenshot shots: an overview of the floor and a closer cube-lane view.
namespace {
constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {2.0f, vec2(20.0f, 0.0f), 0.0f, "overview"},
    {2.5f, vec2(40.0f, 0.0f), 0.0f, "cubes_focus"},
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

    // Each row's iso.x sweeps in [-kAnimRange*2, +kAnimRange*2] because iso.x
    // changes by 2 * anim as the cube moves along world (+x, -y). The floor
    // extends symmetrically around iso (0, 0). Frame the cube lanes
    // by sitting the camera at iso (20, 0), slightly right of center so
    // the active animation envelope is centered in the viewport.
    IRRender::setCameraPosition2DIso(vec2(20.0f, 0.0f));
    IRRender::setCameraZoom(2.0f);

    IREngine::gameLoop();
    return 0;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace IRModifierDemo {

// Brighten a base color by a 0..1 factor, additively toward white.
inline Color tintToward(Color base, float factor, Color toward = {255, 255, 255, 255}) {
    factor = factor < 0.0f ? 0.0f : (factor > 1.0f ? 1.0f : factor);
    auto blend = [&](uint8_t a, uint8_t b) -> uint8_t {
        return static_cast<uint8_t>(
            static_cast<float>(a) * (1.0f - factor) + static_cast<float>(b) * factor
        );
    };
    return Color{
        blend(base.red_, toward.red_),
        blend(base.green_, toward.green_),
        blend(base.blue_, toward.blue_),
        base.alpha_,
    };
}

} // namespace IRModifierDemo

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

            // [H] Toggle the extended help overlay.
            if (IRInput::checkKeyMouseButton(IRInput::kKeyButtonH, IRInput::PRESSED)) {
                IRModifierDemo::g_showHelp = !IRModifierDemo::g_showHelp;
            }

            // [1] Haste: MULTIPLY 1.5x for 300 ticks
            if (IRInput::checkKeyMouseButton(IRInput::kKeyButton1, IRInput::PRESSED)) {
                IRPrefab::Modifier::push(
                    IRModifierDemo::g_cubes[1],
                    IRModifierDemo::g_speedField,
                    TransformKind::MULTIPLY,
                    1.5f,
                    IRModifierDemo::g_cubes[1],
                    300
                );
            }
            // [2] Stun: SET 0 for 180 ticks
            if (IRInput::checkKeyMouseButton(IRInput::kKeyButton2, IRInput::PRESSED)) {
                IRPrefab::Modifier::push(
                    IRModifierDemo::g_cubes[2],
                    IRModifierDemo::g_speedField,
                    TransformKind::SET,
                    0.0f,
                    IRModifierDemo::g_cubes[2],
                    180
                );
            }
            // [3] Slow: MULTIPLY 0.3x for 300 ticks
            if (IRInput::checkKeyMouseButton(IRInput::kKeyButton3, IRInput::PRESSED)) {
                IRPrefab::Modifier::push(
                    IRModifierDemo::g_cubes[3],
                    IRModifierDemo::g_speedField,
                    TransformKind::MULTIPLY,
                    0.3f,
                    IRModifierDemo::g_cubes[3],
                    300
                );
            }
            // [4] Stack: Haste then Slow = net 0.45x for 300 ticks
            if (IRInput::checkKeyMouseButton(IRInput::kKeyButton4, IRInput::PRESSED)) {
                IRPrefab::Modifier::push(
                    IRModifierDemo::g_cubes[4],
                    IRModifierDemo::g_speedField,
                    TransformKind::MULTIPLY,
                    1.5f,
                    IRModifierDemo::g_cubes[4],
                    300
                );
                IRPrefab::Modifier::push(
                    IRModifierDemo::g_cubes[4],
                    IRModifierDemo::g_speedField,
                    TransformKind::MULTIPLY,
                    0.3f,
                    IRModifierDemo::g_cubes[4],
                    300
                );
            }
            // [5] GlobalSlow: MULTIPLY 0.5x on all cubes via singleton for 600 ticks
            if (IRInput::checkKeyMouseButton(IRInput::kKeyButton5, IRInput::PRESSED)) {
                IRPrefab::Modifier::pushGlobal(
                    IRModifierDemo::g_speedField,
                    TransformKind::MULTIPLY,
                    0.5f,
                    IRPrefab::Modifier::globalsEntity(),
                    600
                );
            }
            // [6] LambdaSine: toggle sinusoidal speed on cube 6
            if (IRInput::checkKeyMouseButton(IRInput::kKeyButton6, IRInput::PRESSED)) {
                if (!IRModifierDemo::g_lambda6Active) {
                    IRPrefab::Modifier::pushLambda(
                        IRModifierDemo::g_cubes[6],
                        IRModifierDemo::g_speedField,
                        [](float v) -> float {
                            float phase = static_cast<float>(IRModifierDemo::g_tick) * 0.05f;
                            return v * (1.0f + std::sin(phase));
                        },
                        IRModifierDemo::g_cubes[6],
                        -1
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
            if (IRInput::checkKeyMouseButton(IRInput::kKeyButton7, IRInput::PRESSED) &&
                !IRModifierDemo::g_source7Killed &&
                IRModifierDemo::g_sourceEntity7 != IREntity::kNullEntity) {
                IREntity::destroyEntity(IRModifierDemo::g_sourceEntity7);
                IRModifierDemo::g_source7Killed = true;
            }
            // [8] Clamp: MULTIPLY 2x, then CLAMP_MAX base speed; net effect is no change.
            if (IRInput::checkKeyMouseButton(IRInput::kKeyButton8, IRInput::PRESSED)) {
                IRPrefab::Modifier::push(
                    IRModifierDemo::g_cubes[8],
                    IRModifierDemo::g_speedField,
                    TransformKind::MULTIPLY,
                    2.0f,
                    IRModifierDemo::g_cubes[8],
                    300
                );
                IRPrefab::Modifier::push(
                    IRModifierDemo::g_cubes[8],
                    IRModifierDemo::g_speedField,
                    TransformKind::CLAMP_MAX,
                    IRModifierDemo::kBaseSpeed,
                    IRModifierDemo::g_cubes[8],
                    300
                );
            }
        }
    );

    // Consume: read resolved speed, advance the cube along the world (+x,-y)
    // diagonal so motion is purely horizontal in iso space, and tint the
    // cube color toward white when it has any active modifier so the viewer
    // can see at a glance which cubes are currently being modulated.
    //
    // The animation parameter is recoverable as `(x - y) * 0.5f`; the
    // row's iso.y is `-(x + y)` (z = 0), independent of animation phase.
    auto consumeId =
        IRSystem::createSystem<C_LocalTransform, C_ResolvedFields, C_Modifiers, C_ShapeDescriptor>(
            "ModDemo_Consume",
            [](C_LocalTransform &pos,
               C_ResolvedFields &rf,
               C_Modifiers &mods,
               C_ShapeDescriptor &shape) {
                float speed = rf.get(IRModifierDemo::g_speedField, IRModifierDemo::kBaseSpeed);
                pos.translation_.x += speed;
                pos.translation_.y -= speed;

                const float anim = (pos.translation_.x - pos.translation_.y) * 0.5f;
                if (anim > IRModifierDemo::kAnimRange) {
                    pos.translation_.x -= 2.0f * IRModifierDemo::kAnimRange;
                    pos.translation_.y += 2.0f * IRModifierDemo::kAnimRange;
                }

                // Recover the cube index from the row's iso.y, which stays
                // constant under the (+x,-y) animation. The row mapping is
                // `rowIsoY(i) = (i - (N+1)/2) * kRowIsoSpacing`.
                const float isoY = -(pos.translation_.x + pos.translation_.y);
                const int rowIdx = static_cast<int>(std::round(
                    isoY / IRModifierDemo::kRowIsoSpacing + (IRModifierDemo::kNumCubes + 1) * 0.5f
                ));
                Color baseColor = (rowIdx >= 1 && rowIdx <= IRModifierDemo::kNumCubes)
                                      ? IRModifierDemo::kCubeColors[rowIdx]
                                      : Color{200, 200, 200, 255};

                const bool hasMod = !mods.modifiers_.empty();
                shape.color_ = hasMod ? IRModifierDemo::tintToward(baseColor, 0.45f) : baseColor;
            }
        );

    // This demo renders only SDF shapes; the voxel stage that normally clears
    // the main canvas does not run, so clear it explicitly before drawing.
    // Use clearCanvasAndDistances rather than textures.clear(): on Metal,
    // the distance texture has a sibling "image atomic scratch buffer" that
    // backs imageAtomicMin from the shape/voxel compute shaders. Only
    // IRRender::device()->clearTexImage() (which clearCanvasAndDistances
    // wraps) mirrors the clear value into that scratch buffer; a plain
    // textures.clear() leaves stale depth values around and shapes whose
    // distance loses the atomic-min compare silently fail to render.
    auto clearMainCanvasId = IRSystem::createSystem<C_TriangleCanvasTextures>(
        "ModDemo_ClearMainCanvas",
        [](IREntity::EntityId entity, C_TriangleCanvasTextures &textures) {
            if (entity != IRRender::getActiveCanvasEntity()) {
                return;
            }
            IRSystem::clearCanvasAndDistances(entity, textures);
        }
    );

    // HUD: combined per-cube status panel + extended help overlay.
    // Typed on C_TextSegment so beginTick fires once per tick.
    auto hudId = IRSystem::createSystem<C_TextSegment>(
        "ModDemo_HudUpdate",
        [](const C_TextSegment &) {},
        []() {
            using namespace IRModifierDemo;

            if (g_hudStatusEntity == IREntity::kNullEntity)
                return;

            // Detect whether any global modifiers are active so we can show
            // a "G" indicator on every row when GlobalSlow is in effect.
            int globalCount = 0;
            {
                IREntity::EntityId gE = IRPrefab::Modifier::globalsEntity();
                if (gE != IREntity::kNullEntity) {
                    auto *gMods =
                        IREntity::getComponentOptional<IRComponents::C_GlobalModifiers>(gE)
                            .value_or(nullptr);
                    if (gMods)
                        globalCount = static_cast<int>(gMods->modifiers_.size());
                }
            }

            char buf[1024];
            int n = std::snprintf(
                buf,
                sizeof(buf),
                "MODIFIERS  base=%.3f  tick=%u\n"
                "                                \n"
                "#  effect       speed  active\n",
                kBaseSpeed,
                static_cast<unsigned>(g_tick)
            );

            for (int i = 1; i <= kNumCubes; ++i) {
                const auto &rf = IREntity::getComponent<IRComponents::C_ResolvedFields>(g_cubes[i]);
                const auto &mods = IREntity::getComponent<IRComponents::C_Modifiers>(g_cubes[i]);
                float spd = rf.get(g_speedField, kBaseSpeed);

                int localMods = static_cast<int>(mods.modifiers_.size());
                char marker[8];
                int mi = 0;
                for (int k = 0; k < localMods && mi < 4; ++k, ++mi)
                    marker[mi] = '*';
                if (i == 6 && g_lambda6Active && mi < 6)
                    marker[mi++] = 'L';
                for (int k = 0; k < globalCount && mi < 6; ++k, ++mi)
                    marker[mi] = 'G';
                if (mi == 0)
                    marker[mi++] = '.';
                marker[mi] = '\0';

                if (n > 0 && n < (int)sizeof(buf)) {
                    n += std::snprintf(
                        buf + n,
                        sizeof(buf) - n,
                        "%d  %-11s  %5.3f  %s\n",
                        i,
                        kCubes[i].name_,
                        spd,
                        marker
                    );
                }
            }
            if (n > 0 && n < (int)sizeof(buf)) {
                std::snprintf(
                    buf + n,
                    sizeof(buf) - n,
                    "                                \n"
                    "[1-8] apply  [H] help  [`] cmds\n"
                    "WASD pan   +/- zoom   F8 shot"
                );
            }
            IREntity::getComponent<IRComponents::C_TextSegment>(g_hudStatusEntity).text_ = buf;

            // Help overlay: only populated when toggled on.
            if (g_hudHelpEntity != IREntity::kNullEntity) {
                if (g_showHelp) {
                    IREntity::getComponent<IRComponents::C_TextSegment>(g_hudHelpEntity).text_ =
                        "MODIFIER REFERENCE\n"
                        "                                          \n"
                        "1 Haste       MULTIPLY x1.5    300 ticks\n"
                        "2 Stun        SET 0            180 ticks\n"
                        "3 Slow        MULTIPLY x0.3    300 ticks\n"
                        "4 Stack       x1.5 then x0.3   300 ticks\n"
                        "5 GlobalSlow  global x0.5      600 ticks\n"
                        "6 LambdaSine  v*(1+sin tick)   toggle\n"
                        "7 SrcKill     destroy haste source (once)\n"
                        "8 Clamp       x2 then CLAMP_MAX(base)\n"
                        "                                          \n"
                        "* = local modifier   L = lambda   G = global\n"
                        "Cubes brighten while a modifier is active.\n"
                        "                                          \n"
                        "WASD     pan camera        +/-  zoom\n"
                        "Ctrl +/- GUI scale         `    cmd list\n"
                        "F7  canvas shot   F8  screenshot   F9  rec\n"
                        "                                          \n"
                        "Press [H] to close this help.";
                } else {
                    IREntity::getComponent<IRComponents::C_TextSegment>(g_hudHelpEntity).text_ = "";
                }
            }
        }
    );

    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {
            seedId,
            resolver.modifierDecay_,
            resolver.globalModifierDecay_,
            resolver.modifierResolveGlobal_,
            resolver.modifierResolveExempt_,
            resolver.modifierResolveLambda_,
            consumeId,
            hudId,
            IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
        }
    );

    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {
            IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>(),
        }
    );

    std::list<IRSystem::SystemId> renderPipeline = IRPrefab::Camera::standardControlSystems();
    renderPipeline.insert(
        renderPipeline.end(),
        {
            IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
            clearMainCanvasId,
            IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
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

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
void initCommands() {
    IRPrefab::Camera::registerStandardKeyboardCommands();
    IRCommand::registerCaptureCommands();
    IRCommand::createCommand<IRCommand::TOGGLE_GUI>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonGraveAccent
    );
    IRCommand::createCommand<IRCommand::GUI_ZOOM_IN>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonEqual,
        IRInput::kModifierControl
    );
    IRCommand::createCommand<IRCommand::GUI_ZOOM_OUT>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonMinus,
        IRInput::kModifierControl
    );
}

// ---------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------
void initEntities() {
    using namespace IRComponents;

    // Eight cubes, one per row. Each row is positioned so its iso.x = 0 and
    // its iso.y = rowIsoY(i); the consume system animates each cube along
    // the world (+x,-y) diagonal so it sweeps horizontally across iso space
    // while staying on its row.
    //
    // Cubes start staggered across the lanes so the demo opens as a readable
    // diagonal. As modifiers are applied, divergence in the line-up is the
    // visual story of the framework.
    for (int i = 1; i <= IRModifierDemo::kNumCubes; ++i) {
        vec3 rowCenter = IRModifierDemo::rowWorldCenter(i);
        // Stagger initial animation phase so the cubes spread across the
        // lanes from the start instead of stacking at one rail. A linear
        // ramp from -kAnimRange to +kAnimRange across the rows makes the
        // initial layout read as a clear isometric diagonal.
        const float t =
            static_cast<float>(i - 1) / static_cast<float>(IRModifierDemo::kNumCubes - 1);
        float startAnim = (t * 2.0f - 1.0f) * IRModifierDemo::kAnimRange;
        vec3 pos{rowCenter.x + startAnim, rowCenter.y - startAnim, 0.0f};
        Color color = IRModifierDemo::kCubeColors[i];
        vec4 size{
            IRModifierDemo::kCubeSize,
            IRModifierDemo::kCubeSize,
            IRModifierDemo::kCubeSize,
            0.0f
        };

        if (i == 6) {
            IRModifierDemo::g_cubes[i] = IREntity::createEntity(
                C_LocalTransform{pos},
                C_ShapeDescriptor{IRRender::ShapeType::BOX, size, color},
                C_Modifiers{},
                C_LambdaModifiers{},
                C_ResolvedFields{}
            );
        } else {
            IRModifierDemo::g_cubes[i] = IREntity::createEntity(
                C_LocalTransform{pos},
                C_ShapeDescriptor{IRRender::ShapeType::BOX, size, color},
                C_Modifiers{},
                C_ResolvedFields{}
            );
        }
    }

    IRModifierDemo::g_sourceEntity7 = IREntity::createEntity();
    IRPrefab::Modifier::push(
        IRModifierDemo::g_cubes[7],
        IRModifierDemo::g_speedField,
        TransformKind::MULTIPLY,
        2.0f,
        IRModifierDemo::g_sourceEntity7,
        -1
    );

    // ---------------- Floor + lane markers ----------------
    // Floor slab the cubes appear to slide across. +Z is downward in iso, so
    // a positive Z places the slab below the cube row. Sized to fully contain
    // the animation envelope (anim in [-kAnimRange, +kAnimRange] rotated into
    // world (+x,-y), plus row spread about N*kRowIsoSpacing/2 in either iso axis).
    {
        constexpr float kFloorZ = 5.0f;
        constexpr float kFloorThickness = 2.0f;
        const float floorSpan = IRModifierDemo::kAnimRange * 2.0f +
                                IRModifierDemo::kNumCubes * IRModifierDemo::kRowIsoSpacing * 0.5f +
                                20.0f;
        IREntity::createEntity(
            C_LocalTransform{vec3{0.0f, 0.0f, kFloorZ}},
            C_ShapeDescriptor{
                IRRender::ShapeType::BOX,
                vec4{floorSpan, floorSpan, kFloorThickness, 0.0f},
                Color{55, 60, 80, 255}
            }
        );
    }

    // Per-row lane endpoint pillars: a pair of taller blocks at each row's
    // +/-kAnimRange world position so the eye reads each cube as travelling
    // along a distinct rail. Placed at z slightly above the floor so the
    // animated cubes pass between them at the same vertical level.
    for (int i = 1; i <= IRModifierDemo::kNumCubes; ++i) {
        vec3 rowCenter = IRModifierDemo::rowWorldCenter(i);
        const Color pillarColor{90, 100, 130, 255};
        const vec4 pillarSize{2.5f, 2.5f, 6.0f, 0.0f};
        IREntity::createEntity(
            C_LocalTransform{vec3{
                rowCenter.x - IRModifierDemo::kAnimRange - 4.0f,
                rowCenter.y + IRModifierDemo::kAnimRange + 4.0f,
                1.0f
            }},
            C_ShapeDescriptor{IRRender::ShapeType::BOX, pillarSize, pillarColor}
        );
        IREntity::createEntity(
            C_LocalTransform{vec3{
                rowCenter.x + IRModifierDemo::kAnimRange + 4.0f,
                rowCenter.y - IRModifierDemo::kAnimRange - 4.0f,
                1.0f
            }},
            C_ShapeDescriptor{IRRender::ShapeType::BOX, pillarSize, pillarColor}
        );
    }

    // Decorative scatter: a few off-track terrain bumps so the floor isn't
    // featureless. Positioned outside the animation envelope so they never
    // collide visually with the moving cubes.
    {
        struct Bump {
            vec3 pos_;
            vec4 size_;
            Color color_;
        };
        const Bump bumps[] = {
            {vec3{35.0f, -55.0f, 2.0f}, vec4{6.0f, 6.0f, 4.0f, 0.0f}, Color{80, 110, 90, 255}},
            {vec3{-65.0f, 40.0f, 2.0f}, vec4{8.0f, 4.0f, 4.0f, 0.0f}, Color{110, 90, 80, 255}},
            {vec3{50.0f, 45.0f, 3.0f}, vec4{4.0f, 4.0f, 2.0f, 0.0f}, Color{100, 100, 110, 255}},
            {vec3{-55.0f, -50.0f, 3.0f}, vec4{4.0f, 8.0f, 2.0f, 0.0f}, Color{120, 100, 90, 255}},
        };
        for (const auto &b : bumps) {
            IREntity::createEntity(
                C_LocalTransform{b.pos_},
                C_ShapeDescriptor{IRRender::ShapeType::BOX, b.size_, b.color_}
            );
        }
    }

    // HUD: combined per-cube status panel.
    // Smallest font (fontSize=1) + gui_scale=1 keeps the HUD compact so it
    // doesn't dominate the viewport. Press [H] to surface the extended help.
    IRModifierDemo::g_hudStatusEntity = IREntity::createEntity(
        C_TextSegment{""},
        C_GuiPosition{12, 12},
        C_GuiElement{},
        C_TextStyle{IRColors::kWhite, 0, TextAlignH::LEFT, TextAlignV::TOP, 0, 0, 1}
    );

    IRModifierDemo::g_hudHelpEntity = IREntity::createEntity(
        C_TextSegment{""},
        C_GuiPosition{260, 12},
        C_GuiElement{},
        C_TextStyle{IRColors::kWhite, 0, TextAlignH::LEFT, TextAlignV::TOP, 0, 0, 1}
    );
}

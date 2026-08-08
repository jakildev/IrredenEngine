// reposition_stress — headless pass/fail harness for #2565: repositioning MANY
// voxel-set entities every frame corrupted the entity store and SIGSEGV'd in
// `getComponent` on a still-live handle (`record.archetypeNode == nullptr`).
//
// The reporting creation (a game-side lattice demo) moved ~96 static voxel-set
// entities per frame through a Lua binding and died after ~2s, with the time to
// crash scaling INVERSELY with the number of sets moved. This harness rebuilds
// that shape engine-side.
//
// Arms (compose freely). They differ in HOW the per-tick write reaches the
// transform, because that is the axis the report's backtrace distinguishes:
//   --drive=none     control: full scene, no per-frame reposition (must run clean)
//   --drive=column   dense archetype-column write, zero per-entity lookups
//   --drive=lookup   per-id `getComponent<C_LocalTransform>` off a batched
//                    driver vector of ids snapshotted at spawn
//   --drive=node     dynamic system body over the matched `ArchetypeNode*`,
//                    resolving ids read out of the node's LIVE `entities_`
//                    array — structurally the reported stack (frames 0-3: Lua
//                    EVAL tick -> setPosition binding -> getComponent)
//   --churn          additionally create + destroy transient entities every tick,
//                    exercising swap-remove / flush bookkeeping alongside the moves
//
// Exit code is the whole result: 0 = the full tick horizon ran clean, 1 = an
// engine assert fired (IR_ASSERT throws) or an invariant check failed. So
// `fleet-run IRRepositionStress` is a headless regression check.
//
// The horizon is counted in UPDATE ticks (fixed timestep), not render frames —
// the reported crash horizon was ~2s of simulation, and the default 3600 ticks
// is roughly 3x that.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

// COMPONENTS
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

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
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_sprites_to_screen.hpp>
#include <irreden/render/camera_controls.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>

#include <cstdio>
#include <exception>
#include <list>
#include <string>
#include <vector>

namespace {

// Tag marking the per-tick transient entities the --churn arm spawns. It only
// has to give them their own archetype, so it carries no data.
struct C_RepositionChurn {};

enum class Drive { NONE, COLUMN, LOOKUP, NODE };

constexpr const char *kArgDrive = "--drive";
constexpr const char *kArgChurn = "--churn";
constexpr const char *kArgTicks = "--ticks";
constexpr const char *kArgSets = "--sets";
constexpr const char *kArgChurnPerTick = "--churn-per-tick";

// One-voxel sets: the defect is about the number of SETS moved per tick, not
// the voxel count, and a 1^3 set keeps the pool footprint trivial at 100+ sets.
constexpr IRMath::ivec3 kSetDims{1, 1, 1};
constexpr int kLatticeStride = 2;
constexpr int kLatticeWidth = 10;
constexpr float kDriftAmplitude = 3.0f;

Drive g_drive = Drive::COLUMN;
bool g_churn = false;
int g_tickHorizon = 3600;
int g_setCount = 100;
int g_churnPerTick = 8;

int g_tick = 0;
int g_failCount = 0;

// The batched driver vector: every set's EntityId, captured at spawn. The
// --drive=lookup arm walks THIS (a foreign-entity batch, the sanctioned shape
// for dynamically-determined ids) rather than getComponent-ing its own
// iterating entity.
std::vector<IREntity::EntityId> g_setEntities;
std::vector<IREntity::EntityId> g_churnEntities;

IRMath::vec3 driftFor(int index, int tick) {
    const float phase = static_cast<float>(index) * 0.37f + static_cast<float>(tick) * 0.05f;
    const IRMath::vec3 lattice{
        static_cast<float>((index % kLatticeWidth) * kLatticeStride),
        static_cast<float>((index / kLatticeWidth) * kLatticeStride),
        0.0f
    };
    return lattice + IRMath::vec3{
                         kDriftAmplitude * IRMath::sin(phase),
                         kDriftAmplitude * IRMath::cos(phase),
                         0.0f
                     };
}

} // namespace

void registerArgs();
void readArgs();
void initSystems();
void initEntities();
void checkStoreInvariants();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: reposition_stress");
    registerArgs();
    IREngine::init(argc, argv);
    readArgs();

    initSystems();
    initEntities();

    IR_LOG_INFO(
        "[reposition_stress] drive={} churn={} sets={} ticks={}",
        g_drive == Drive::NONE
            ? "none"
            : (g_drive == Drive::COLUMN ? "column"
                                        : (g_drive == Drive::LOOKUP ? "lookup" : "node")),
        g_churn,
        g_setCount,
        g_tickHorizon
    );

    // IR_ASSERT logs at critical and THROWS, so an engine-store assert reaches
    // here as an exception rather than a silent abort — that is what turns this
    // demo into a pass/fail check with a readable diagnostic.
    std::string failure;
    try {
        IREngine::gameLoop();
    } catch (const std::exception &e) {
        failure = e.what();
    }

    // The verdict goes to stderr, not IR_LOG_*: `gameLoop()` tears the world
    // (and the log sinks) down as it returns, so anything logged past this
    // point is swallowed — engine/CLAUDE.md §"Manager globals". main may read
    // only its own file-scope state here.
    if (!failure.empty()) {
        std::fprintf(
            stderr,
            "[reposition_stress] FAILED at tick %d/%d: %s\n",
            g_tick,
            g_tickHorizon,
            failure.c_str()
        );
        return 1;
    }
    if (g_failCount > 0) {
        std::fprintf(
            stderr,
            "[reposition_stress] FAILED: %d store-invariant check(s) at tick %d\n",
            g_failCount,
            g_tick
        );
        return 1;
    }
    if (g_tick < g_tickHorizon) {
        std::fprintf(
            stderr,
            "[reposition_stress] FAILED: loop exited at tick %d before the %d-tick horizon\n",
            g_tick,
            g_tickHorizon
        );
        return 1;
    }
    std::fprintf(stderr, "[reposition_stress] PASSED %d ticks clean\n", g_tick);
    return 0;
}

// Registered on IREngine::args() BEFORE init so the single engine parse covers
// them and --help aggregates everything (engine/CLAUDE.md §"CLI args go through
// IRArgs").
void registerArgs() {
    IRArgs::Parser &args = IREngine::args();
    args.enumValue(
        kArgDrive,
        "Per-tick reposition arm: none (control) | column (archetype-column write) | "
        "lookup (per-id getComponent off a batched driver vector) | node (dynamic "
        "system reading ids straight out of the matched archetype node)",
        {"none", "column", "lookup", "node"},
        "column"
    );
    args.flag(kArgChurn, "Also create + destroy transient entities every tick");
    args.integer(kArgTicks, "UPDATE ticks to run before exiting cleanly", 3600);
    args.integer(kArgSets, "Voxel-set entities spawned (and repositioned per tick)", 100);
    args.integer(kArgChurnPerTick, "Transient entities created per tick under --churn", 8);
}

void readArgs() {
    IRArgs::Parser &args = IREngine::args();
    const std::string drive = args.getEnum(kArgDrive);
    if (drive == "none") {
        g_drive = Drive::NONE;
    } else if (drive == "column") {
        g_drive = Drive::COLUMN;
    } else if (drive == "lookup") {
        g_drive = Drive::LOOKUP;
    } else {
        g_drive = Drive::NODE;
    }
    g_churn = args.getFlag(kArgChurn);
    g_tickHorizon = args.getInt(kArgTicks);
    g_setCount = args.getInt(kArgSets);
    g_churnPerTick = args.getInt(kArgChurnPerTick);
}

void initSystems() {
    std::list<IRSystem::SystemId> update = {
        IRSystem::createSystem<IRSystem::LOD_UPDATE>(),
        IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
    };

    if (g_drive == Drive::COLUMN) {
        // Dense-column write: the reposition every set gets, with zero
        // per-entity lookups. Isolates the pool/flush half of the pipeline
        // from the id-lookup half the LOOKUP arm adds.
        update.push_back(
            IRSystem::createSystem<C_LocalTransform, C_VoxelSetNew>(
                "reposition_stress_column",
                [](IREntity::EntityId entity, C_LocalTransform &xform, C_VoxelSetNew &) {
                    xform.translation_ = driftFor(static_cast<int>(entity % 1000), g_tick);
                }
            )
        );
    }

    // The canvas entity is unique (one C_VoxelPool per canvas), so a system
    // over C_VoxelPool ticks exactly once per frame — the home for the
    // once-per-tick driver walks and the horizon counter.
    if (g_drive == Drive::LOOKUP) {
        update.push_back(
            IRSystem::createSystem<C_VoxelPool>("reposition_stress_lookup", [](C_VoxelPool &) {
                // The reporting shape: resolve each set by stored EntityId and
                // write its transform through getComponent. One walk per tick
                // over a batched vector, not a per-iterating-entity lookup.
                for (std::size_t i = 0; i < g_setEntities.size(); ++i) {
                    IREntity::getComponent<C_LocalTransform>(g_setEntities[i]).translation_ =
                        driftFor(static_cast<int>(i), g_tick);
                }
            })
        );
    }

    if (g_drive == Drive::NODE) {
        // The reported crash's own shape (#2565 backtrace frames 0-3): a
        // DYNAMIC system body that receives the matched `ArchetypeNode*` and
        // resolves each entity by id through `getComponent`. Unlike the LOOKUP
        // arm, the ids come out of the node's live `entities_` array rather
        // than a stable snapshot — so they carry whatever flag bits `setFlags`
        // has OR'd in, and they shift under swap-remove while the walk runs.
        // That read path is the one thing the Lua EVAL tick in the report does
        // that a C++ column write does not.
        //
        // The per-row `getComponent` below IS the ECS footgun that
        // `.claude/rules/cpp-ecs.md` bans, and that is deliberate: this arm
        // exists to execute the banned shape so the store is exercised through
        // it. Rewriting it to dense-column iteration — the rule's mechanical
        // fix — would make it a duplicate of `--drive=column` and delete the
        // only arm that mirrors the report. Do not "fix" it; the arm is the
        // fixture. Every other reposition site in this demo follows the rule.
        update.push_back(
            IRSystem::createSystemDynamic(
                "reposition_stress_node",
                IREntity::getArchetype<C_LocalTransform, C_VoxelSetNew>(),
                IREntity::Archetype{},
                [](IREntity::ArchetypeNode *node) {
                    for (int row = 0; row < node->length_; ++row) {
                        const IREntity::EntityId entity = node->entities_[row];
                        IREntity::getComponent<C_LocalTransform>(entity).translation_ =
                            driftFor(row, g_tick);
                    }
                }
            )
        );
    }

    if (g_churn) {
        update.push_back(
            IRSystem::createSystem<C_VoxelPool>("reposition_stress_churn", [](C_VoxelPool &) {
                // Retire last tick's transients, then spawn this tick's. The
                // destroys drive swap-remove bookkeeping
                // (updateBackEntityPosition / moveEntityByArchetype) against
                // the same store the repositions are reading.
                for (IREntity::EntityId entity : g_churnEntities) {
                    if (IREntity::entityExists(entity)) {
                        IREntity::destroyEntity(entity);
                    }
                }
                g_churnEntities.clear();
                for (int i = 0; i < g_churnPerTick; ++i) {
                    g_churnEntities.push_back(
                        IREntity::createEntity(
                            C_LocalTransform{IRMath::vec3{static_cast<float>(i), 0.0f, -8.0f}},
                            C_RepositionChurn{}
                        )
                    );
                }
            })
        );
    }

    update.push_back(IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>());
    update.push_back(IRSystem::createSystem<IRSystem::REBUILD_GRID_VOXELS>());
    update.push_back(IRSystem::createSystem<IRSystem::REBUILD_GRID_VOXELS_IMPLICIT>());

    // Horizon counter runs LAST in UPDATE so a tick is only counted once every
    // system above has run against this tick's state.
    update.push_back(
        IRSystem::createSystem<C_VoxelPool>("reposition_stress_horizon", [](C_VoxelPool &) {
            ++g_tick;
            checkStoreInvariants();
            if (g_tick % 300 == 0) {
                IR_LOG_INFO(
                    "[reposition_stress] tick {}/{} liveEntities={}",
                    g_tick,
                    g_tickHorizon,
                    IREntity::getLiveEntityCount()
                );
            }
            if (g_tick >= g_tickHorizon) {
                IRWindow::closeWindow();
            }
        })
    );

    IRSystem::registerPipeline(IRTime::Events::UPDATE, update);
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
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
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
            IRSystem::createSystem<IRSystem::SPRITE_TO_SCREEN>(),
        }
    );
    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);

    IRPrefab::Camera::registerStandardKeyboardCommands();
}

// Spawn the lattice through the BATCH path (`createEntitiesBatch`), which is
// what the reporting creation used: it allocates every id first — all seeded
// `{nullptr, -1}` — and only then runs the per-entity `updateRecord` loop, so
// the "exists but not yet placed" window spans the whole batch rather than a
// single entity.
void initEntities() {
    std::vector<C_LocalTransform> transforms;
    std::vector<C_WorldTransform> worldTransforms;
    std::vector<C_VoxelSetNew> sets;
    transforms.reserve(g_setCount);
    worldTransforms.reserve(g_setCount);
    sets.reserve(g_setCount);
    for (int i = 0; i < g_setCount; ++i) {
        transforms.emplace_back(driftFor(i, 0));
        worldTransforms.emplace_back();
        sets.emplace_back(kSetDims, IRMath::Color{200, 120, 60, 255}, true);
    }

    g_setEntities =
        IREntity::getEntityManager().createEntitiesBatch(transforms, worldTransforms, sets);
    IR_LOG_INFO("[reposition_stress] spawned {} voxel-set entities", g_setEntities.size());
}

// Per-tick store audit. The defect's signature was a live handle whose record
// had gone missing (or gone unplaced) while the entity was never destroyed, so
// the honest check is: every id we spawned still resolves to a placed record.
// `findRecord` is the non-inserting probe — a bare `entityExists` here would
// answer from an index this very audit could have polluted pre-#2565.
void checkStoreInvariants() {
    for (std::size_t i = 0; i < g_setEntities.size(); ++i) {
        const IREntity::EntityRecord *record =
            IREntity::getEntityManager().findRecord(g_setEntities[i]);
        if (record == nullptr) {
            IR_LOG_ERROR(
                "[reposition_stress] tick {}: set {} (entity {}) lost its index record",
                g_tick,
                i,
                g_setEntities[i]
            );
            ++g_failCount;
            IRWindow::closeWindow();
            return;
        }
        if (record->archetypeNode == nullptr) {
            IR_LOG_ERROR(
                "[reposition_stress] tick {}: set {} (entity {}) has a null archetypeNode "
                "(row={}) — this is the #2565 fault state",
                g_tick,
                i,
                g_setEntities[i],
                record->row
            );
            ++g_failCount;
            IRWindow::closeWindow();
            return;
        }
    }
}

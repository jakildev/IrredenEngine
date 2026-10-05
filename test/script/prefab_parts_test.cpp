// Composite prefabs: the schema-v2 `parts` list, each part a CHILD_OF child of
// the root with its own transform and LOD band, and PREFAB_LOD_PARTS keeping
// the live parts equal to the parts whose band holds the root's settled tier.
// Every tier change runs the zoom -> tier write LOD_UPDATE performs.

#include <gtest/gtest.h>

#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/render/components/component_lod_tier_override.hpp>
#include <irreden/render/components/component_lod_tier_override_lua.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/components/component_zoom_level_lua.hpp>
#include <irreden/render/lod_tier_snapshot.hpp>
#include <irreden/render/lod_utils.hpp>
#include <irreden/render/systems/system_lod_update.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/script/prefab_api.hpp>
#include <irreden/script/prefab_component_factory.hpp>
#include <irreden/update/components/component_prefab_parts.hpp>
#include <irreden/update/systems/system_prefab_lod_parts.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using IRComponents::C_PrefabParts;
using IRComponents::C_ShapeDescriptor;
using IREntity::EntityId;
using IRMath::vec2;
using IRMath::vec3;
using IRMath::vec4;
using IRRender::LodLevel;

constexpr const char *kTmpDir = "/tmp";

// Six parts on bands chosen so the live set differs at every tier family:
//   coarse  [LOD_3 .. LOD_4]  zoom 1x / 2x
//   mid     [LOD_1 .. LOD_2]  zoom 4x / 8x
//   detail  [LOD_0 .. LOD_2]  zoom 4x and up
//   fine    [LOD_0 .. LOD_0]  zoom 16x and up
//   finer   [LOD_0 .. LOD_0]  zoom 16x and up
//   always  [LOD_0 .. LOD_4]  every zoom
// so 2 / 3 / 4 parts live at zoom 1x / 4x / 16x.
const char *kTieredParts = "  parts = {\n"
                           "    { id = 'coarse', shape = { type = IRShape.BOX },\n"
                           "      lod = { fine = IRRender.LodLevel.LOD_3,\n"
                           "              coarse = IRRender.LodLevel.LOD_4 } },\n"
                           "    { id = 'mid', shape = { type = IRShape.SPHERE },\n"
                           "      lod = { fine = IRRender.LodLevel.LOD_1,\n"
                           "              coarse = IRRender.LodLevel.LOD_2 } },\n"
                           "    { id = 'detail', shape = { type = IRShape.ELLIPSOID },\n"
                           "      lod = { fine = IRRender.LodLevel.LOD_0,\n"
                           "              coarse = IRRender.LodLevel.LOD_2 } },\n"
                           "    { id = 'fine', shape = { type = IRShape.CONE },\n"
                           "      lod = { fine = IRRender.LodLevel.LOD_0,\n"
                           "              coarse = IRRender.LodLevel.LOD_0 } },\n"
                           "    { id = 'finer', shape = { type = IRShape.TORUS },\n"
                           "      lod = { fine = IRRender.LodLevel.LOD_0,\n"
                           "              coarse = IRRender.LodLevel.LOD_0 } },\n"
                           "    { id = 'always', shape = { type = IRShape.CYLINDER } },\n"
                           "  },\n";
const std::vector<bool> kCoarseMask{true, false, false, false, false, true};
const std::vector<bool> kMidMask{false, true, true, false, false, true};
const std::vector<bool> kFineMask{false, false, true, true, true, true};

// LuaScript first so its sol::state outlives the managers, and with them the
// parts manifests' Lua tables.
class PrefabParts : public testing::Test {
  protected:
    PrefabParts()
        : m_lua{}
        , m_entityManager{}
        , m_systemManager{} {
        m_lua.bindLuaDrivenEcs();
        IRPrefab::Prefab::clearPrefabs();
        IRPrefab::Prefab::clearComponentFactories();
        m_systemManager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::PREFAB_LOD_PARTS>()}
        );
    }

    ~PrefabParts() override {
        IRPrefab::Prefab::clearPrefabs();
        IRPrefab::Prefab::clearComponentFactories();
    }

    static void setZoom(float zoom) {
        IRSystem::System<IRSystem::LOD_UPDATE>::writeActiveTier(vec2(zoom));
    }

    static std::string writePrefab(const std::string &tag, const std::string &body) {
        const std::string path = std::string{kTmpDir} + "/prefab_parts_" + tag + ".prefab.lua";
        std::ofstream out(path);
        out << body;
        return path;
    }

    IRPrefab::Prefab::SpawnResult spawnResult(const std::string &tag, const std::string &body) {
        IRPrefab::Prefab::registerPrefab(tag, writePrefab(tag, body));
        return IRPrefab::Prefab::spawnPrefab(m_lua, tag, vec3(0.0f));
    }

    EntityId spawn(const std::string &tag, const std::string &body) {
        const IRPrefab::Prefab::SpawnResult result = spawnResult(tag, body);
        EXPECT_NE(result.entity_, IREntity::kNullEntity) << result.error_;
        return result.entity_;
    }

    void tick() {
        m_systemManager.executePipeline(IRTime::Events::UPDATE);
        m_entityManager.destroyMarkedEntities();
    }

    // Enough ticks for a tier change to settle and its spawns to flush.
    void settle() {
        for (int i = 0; i <= IRConstants::kPrefabPartsTierSettleTicks; ++i) {
            tick();
        }
    }

    static C_PrefabParts &partsOf(EntityId root) {
        return IREntity::getComponent<C_PrefabParts>(root);
    }

    // The live part entity per manifest slot, kNullEntity where absent.
    static std::vector<EntityId> liveParts(EntityId root) {
        std::vector<EntityId> live;
        for (const auto &slot : partsOf(root).slots_) {
            live.push_back(
                slot.entity_ != IREntity::kNullEntity && IREntity::entityExists(slot.entity_)
                    ? slot.entity_
                    : IREntity::kNullEntity
            );
        }
        return live;
    }

    static int liveCount(EntityId root) {
        int count = 0;
        for (EntityId part : liveParts(root)) {
            count += part != IREntity::kNullEntity ? 1 : 0;
        }
        return count;
    }

    static std::vector<bool> liveMask(EntityId root) {
        std::vector<bool> mask;
        for (EntityId part : liveParts(root)) {
            mask.push_back(part != IREntity::kNullEntity);
        }
        return mask;
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
};

TEST_F(PrefabParts, SpawnsChildrenWithBands) {
    setZoom(4.0f); // LOD_2
    const EntityId root =
        spawn("bands", std::string{"return {\n  prefab_version = 2,\n"} + kTieredParts + "}\n");
    ASSERT_NE(root, IREntity::kNullEntity);

    // Only the parts whose band holds LOD_2 exist, without a tick.
    EXPECT_EQ(liveMask(root), kMidMask);
    const C_PrefabParts &parts = partsOf(root);
    ASSERT_EQ(parts.slots_.size(), 6u);
    EXPECT_EQ(parts.tier_, LodLevel::LOD_2);
    EXPECT_EQ(parts.slots_[0].lodMax_, LodLevel::LOD_3);
    EXPECT_EQ(parts.slots_[0].lodMin_, LodLevel::LOD_4);
    EXPECT_EQ(parts.slots_[2].lodMax_, LodLevel::LOD_0);
    EXPECT_EQ(parts.slots_[2].lodMin_, LodLevel::LOD_2);
    EXPECT_EQ(parts.slots_[3].lodMax_, LodLevel::LOD_0);
    EXPECT_EQ(parts.slots_[3].lodMin_, LodLevel::LOD_0);
    EXPECT_EQ(parts.slots_[5].lodMax_, LodLevel::LOD_0);
    EXPECT_EQ(parts.slots_[5].lodMin_, LodLevel::LOD_4);

    std::vector<EntityId> children = m_entityManager.getChildren(root);
    std::sort(children.begin(), children.end());
    std::vector<EntityId> expected{
        parts.slots_[1].entity_,
        parts.slots_[2].entity_,
        parts.slots_[5].entity_
    };
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(children, expected) << "every live part is a CHILD_OF child of the root";

    const C_ShapeDescriptor &mid =
        IREntity::getComponent<C_ShapeDescriptor>(parts.slots_[1].entity_);
    EXPECT_EQ(mid.shapeType_, IRMath::SDF::ShapeType::SPHERE);
}

TEST_F(PrefabParts, PartTransformAndComponentsApply) {
    IRScript::bindLuaType<IRComponents::C_ZoomLevel>(m_lua);
    const EntityId root = spawn(
        "transform",
        "return {\n"
        "  prefab_version = 2,\n"
        "  parts = {\n"
        "    { id = 'arm',\n"
        "      shape = { type = IRShape.BOX, params = { 2, 3, 4, 0 },\n"
        "                color = { r = 10, g = 20, b = 30 } },\n"
        "      transform = { translation = { 1, 2, 3 }, scale = { 2, 2, 2 } },\n"
        "      components = { C_ZoomLevel = { zoom = 3.0 } } },\n"
        "  },\n"
        "}\n"
    );
    ASSERT_NE(root, IREntity::kNullEntity);
    const EntityId arm = partsOf(root).slots_[0].entity_;
    ASSERT_NE(arm, IREntity::kNullEntity);

    const auto &local = IREntity::getComponent<IRComponents::C_LocalTransform>(arm);
    EXPECT_EQ(local.translation_, vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(local.scale_, vec3(2.0f));
    EXPECT_EQ(local.rotation_, vec4(0.0f, 0.0f, 0.0f, 1.0f));
    const auto &world = IREntity::getComponent<IRComponents::C_WorldTransform>(arm);
    EXPECT_EQ(world.translation_, vec3(1.0f, 2.0f, 3.0f)) << "seeded before the first propagation";

    const auto &shape = IREntity::getComponent<C_ShapeDescriptor>(arm);
    EXPECT_EQ(shape.params_, vec4(2.0f, 3.0f, 4.0f, 0.0f));
    EXPECT_EQ(shape.color_.red_, 10);
    EXPECT_EQ(shape.color_.alpha_, 255);
    EXPECT_FLOAT_EQ(IREntity::getComponent<IRComponents::C_ZoomLevel>(arm).zoom_.x, 3.0f);
}

TEST_F(PrefabParts, DestroyRootRemovesParts) {
    setZoom(1.0f);
    const EntityId bystander = IREntity::createEntity();
    const EntityId root =
        spawn("destroy", std::string{"return {\n  prefab_version = 2,\n"} + kTieredParts + "}\n");
    ASSERT_NE(root, IREntity::kNullEntity);
    const std::vector<EntityId> live = liveParts(root);
    ASSERT_EQ(liveCount(root), 2);

    IREntity::destroyTree(root);
    m_entityManager.destroyMarkedEntities();

    EXPECT_FALSE(IREntity::entityExists(root));
    for (EntityId part : live) {
        if (part != IREntity::kNullEntity) {
            EXPECT_FALSE(IREntity::entityExists(part));
        }
    }
    EXPECT_TRUE(IREntity::entityExists(bystander));
    EXPECT_EQ(IREntity::countComponents<C_ShapeDescriptor>(), 0);
}

TEST_F(PrefabParts, TierChangeSpawnsAndDestroysParts) {
    setZoom(1.0f);
    const EntityId root =
        spawn("tiers", std::string{"return {\n  prefab_version = 2,\n"} + kTieredParts + "}\n");
    ASSERT_NE(root, IREntity::kNullEntity);

    const auto expectTier = [&](LodLevel tier, const std::vector<bool> &mask, int count) {
        EXPECT_EQ(partsOf(root).tier_, tier);
        EXPECT_EQ(liveMask(root), mask);
        EXPECT_EQ(liveCount(root), count);
        EXPECT_EQ(IREntity::countComponents<C_ShapeDescriptor>(), count)
            << "a destroyed part leaves no entity behind";
        EXPECT_EQ(m_entityManager.getChildren(root).size(), static_cast<std::size_t>(count));
    };

    settle();
    expectTier(LodLevel::LOD_4, kCoarseMask, 2);

    setZoom(4.0f);
    settle();
    expectTier(LodLevel::LOD_2, kMidMask, 3);

    setZoom(16.0f);
    settle();
    expectTier(LodLevel::LOD_0, kFineMask, 4);

    // Back out to the coarse tier: the coarse part is rebuilt from the
    // manifest at its original slot.
    setZoom(1.0f);
    settle();
    expectTier(LodLevel::LOD_4, kCoarseMask, 2);
    EXPECT_EQ(
        IREntity::getComponent<C_ShapeDescriptor>(partsOf(root).slots_[0].entity_).shapeType_,
        IRMath::SDF::ShapeType::BOX
    );
}

TEST_F(PrefabParts, TierChangeWaitsToSettle) {
    setZoom(1.0f);
    const EntityId root =
        spawn("settle", std::string{"return {\n  prefab_version = 2,\n"} + kTieredParts + "}\n");
    ASSERT_NE(root, IREntity::kNullEntity);

    setZoom(16.0f);
    for (int i = 0; i < IRConstants::kPrefabPartsTierSettleTicks - 1; ++i) {
        tick();
        EXPECT_EQ(liveMask(root), kCoarseMask) << "tick " << i;
    }
    // A tier the camera only passes through never touches the parts.
    setZoom(4.0f);
    for (int i = 0; i < IRConstants::kPrefabPartsTierSettleTicks - 1; ++i) {
        tick();
        EXPECT_EQ(liveMask(root), kCoarseMask) << "tick " << i;
    }
    tick();
    EXPECT_EQ(liveMask(root), kMidMask);
}

TEST_F(PrefabParts, SpawnBudgetSpreadsSpawnsAcrossTicks) {
    // One more always-banded fine part than a tick may spawn.
    const int count = IRConstants::kPrefabPartSpawnBudgetPerTick + 1;
    std::string body = "return {\n  prefab_version = 2,\n  parts = {\n";
    for (int i = 0; i < count; ++i) {
        body +=
            "    { id = 'p" + std::to_string(i) +
            "', lod = { fine = IRRender.LodLevel.LOD_0, coarse = IRRender.LodLevel.LOD_0 } },\n";
    }
    body += "  },\n}\n";
    setZoom(1.0f);
    const EntityId root = spawn("budget", body);
    ASSERT_NE(root, IREntity::kNullEntity);
    EXPECT_EQ(liveCount(root), 0);

    setZoom(16.0f);
    for (int i = 0; i < IRConstants::kPrefabPartsTierSettleTicks; ++i) {
        tick();
    }
    EXPECT_EQ(liveCount(root), IRConstants::kPrefabPartSpawnBudgetPerTick);
    tick();
    EXPECT_EQ(liveCount(root), count);
}

TEST_F(PrefabParts, RootPinDrivesParts) {
    m_lua.registerTypeFromTraits<IRComponents::C_LodTierOverride>();
    setZoom(1.0f);
    const EntityId root = spawn(
        "pinned",
        std::string{
            "return {\n  prefab_version = 2,\n"
            "  components = { C_LodTierOverride = { tier = IRRender.LodLevel.LOD_0 } },\n"
        } + kTieredParts +
            "}\n"
    );
    ASSERT_NE(root, IREntity::kNullEntity);
    EXPECT_EQ(liveMask(root), kFineMask)
        << "the declared pin decides the spawn tier, not the camera";
    settle();
    EXPECT_EQ(liveMask(root), kFineMask);
}

TEST_F(PrefabParts, ResidentPartHidesInsteadOfDestroying) {
    setZoom(16.0f);
    const EntityId root = spawn(
        "resident",
        "return {\n"
        "  prefab_version = 2,\n"
        "  parts = {\n"
        "    { id = 'kept', resident = true, shape = { type = IRShape.SPHERE },\n"
        "      lod = { fine = IRRender.LodLevel.LOD_0, coarse = IRRender.LodLevel.LOD_1 } },\n"
        "    { id = 'dropped', shape = { type = IRShape.BOX },\n"
        "      lod = { fine = IRRender.LodLevel.LOD_0, coarse = IRRender.LodLevel.LOD_1 } },\n"
        "  },\n"
        "}\n"
    );
    ASSERT_NE(root, IREntity::kNullEntity);
    const EntityId kept = partsOf(root).slots_[0].entity_;
    ASSERT_NE(kept, IREntity::kNullEntity);
    ASSERT_NE(partsOf(root).slots_[1].entity_, IREntity::kNullEntity);

    const auto drawsAt = [&](EntityId part) {
        IRPrefab::Lod::TierSnapshot snapshot;
        snapshot.capture();
        const C_ShapeDescriptor &shape = IREntity::getComponent<C_ShapeDescriptor>(part);
        return !IRRender::shouldSkipAtLod(shape.lodMin_, shape.lodMax_, snapshot.resolve(part));
    };
    EXPECT_TRUE(drawsAt(kept));

    setZoom(1.0f);
    settle();
    EXPECT_EQ(partsOf(root).slots_[0].entity_, kept);
    EXPECT_TRUE(IREntity::entityExists(kept)) << "a resident part outlives its band";
    EXPECT_FALSE(drawsAt(kept)) << "but no longer draws";
    EXPECT_EQ(partsOf(root).slots_[1].entity_, IREntity::kNullEntity);
    EXPECT_EQ(IREntity::countComponents<C_ShapeDescriptor>(), 1);

    setZoom(16.0f);
    settle();
    EXPECT_EQ(partsOf(root).slots_[0].entity_, kept) << "re-entering the band reuses the part";
    EXPECT_TRUE(drawsAt(kept));
    EXPECT_NE(partsOf(root).slots_[1].entity_, IREntity::kNullEntity);
}

TEST_F(PrefabParts, VoxelRefPartLoadsOnFirstSpawn) {
    const std::string voxelPath = std::string{kTmpDir} + "/prefab_parts_petal.vxs";
    const auto writePetals = [&](int count) {
        std::vector<IRAsset::ShapeRecord> shapes(count);
        for (int i = 0; i < count; ++i) {
            shapes[i].shapeTypeId_ = static_cast<std::uint32_t>(IRMath::SDF::ShapeType::ELLIPSOID);
            shapes[i].offset_ = vec3(static_cast<float>(i), 0.0f, 0.0f);
        }
        IRAsset::saveShapeGroup(voxelPath, shapes);
    };
    writePetals(2);

    const std::string body = "return {\n"
                             "  prefab_version = 2,\n"
                             "  parts = {\n"
                             "    { id = 'petals', voxel_ref = '" +
                             voxelPath +
                             "',\n"
                             "      lod = { fine = IRRender.LodLevel.LOD_0,\n"
                             "              coarse = IRRender.LodLevel.LOD_2 } },\n"
                             "  },\n"
                             "}\n";
    setZoom(1.0f);
    const EntityId first = spawn("petal_a", body);
    const EntityId second = spawn("petal_b", body);
    ASSERT_NE(first, IREntity::kNullEntity);
    ASSERT_NE(second, IREntity::kNullEntity);
    EXPECT_EQ(IREntity::countComponents<C_ShapeDescriptor>(), 0);

    // The file the parts see is the one on disk when they first spawn.
    writePetals(3);
    setZoom(4.0f);
    settle();
    for (EntityId root : {first, second}) {
        const EntityId part = partsOf(root).slots_[0].entity_;
        ASSERT_NE(part, IREntity::kNullEntity);
        EXPECT_EQ(m_entityManager.getChildren(part).size(), 3u)
            << "each SHAPES record is a child of the part";
    }
    EXPECT_EQ(IREntity::countComponents<C_ShapeDescriptor>(), 6);

    // Out of band again: each part's tree, records included, goes.
    setZoom(1.0f);
    settle();
    EXPECT_EQ(IREntity::countComponents<C_ShapeDescriptor>(), 0);
}

// A tree mark lists the root's descendants when it is taken, so a part staged
// before it and built at the flush after it would outlive the root. The staged
// build sees the mark and gives up; a root nobody marked still gets its part.
TEST_F(PrefabParts, TreeMarkedRootCancelsStagedPart) {
    const std::string voxelPath = std::string{kTmpDir} + "/prefab_parts_staged.vxs";
    std::vector<IRAsset::ShapeRecord> shapes(2);
    for (IRAsset::ShapeRecord &shape : shapes) {
        shape.shapeTypeId_ = static_cast<std::uint32_t>(IRMath::SDF::ShapeType::ELLIPSOID);
    }
    IRAsset::saveShapeGroup(voxelPath, shapes);
    const std::string body = "return {\n"
                             "  prefab_version = 2,\n"
                             "  parts = {\n"
                             "    { id = 'petals', voxel_ref = '" +
                             voxelPath +
                             "',\n"
                             "      lod = { fine = IRRender.LodLevel.LOD_0,\n"
                             "              coarse = IRRender.LodLevel.LOD_2 } },\n"
                             "  },\n"
                             "}\n";
    setZoom(1.0f);
    const EntityId doomed = spawn("staged_doomed", body);
    const EntityId kept = spawn("staged_kept", body);
    ASSERT_NE(doomed, IREntity::kNullEntity);
    ASSERT_NE(kept, IREntity::kNullEntity);
    ASSERT_EQ(liveCount(doomed), 0);

    IRPrefab::Prefab::stagePartSpawn(doomed, partsOf(doomed), 0);
    IRPrefab::Prefab::stagePartSpawn(kept, partsOf(kept), 0);
    const EntityId doomedPart = partsOf(doomed).slots_[0].entity_;
    const EntityId keptPart = partsOf(kept).slots_[0].entity_;
    IREntity::destroyTree(doomed);
    m_entityManager.flushStructuralChanges();
    m_entityManager.destroyMarkedEntities();

    EXPECT_FALSE(IREntity::entityExists(doomed));
    EXPECT_FALSE(IREntity::entityExists(doomedPart));
    ASSERT_TRUE(IREntity::entityExists(keptPart));
    EXPECT_EQ(m_entityManager.getChildren(keptPart).size(), 2u);
    EXPECT_EQ(IREntity::countComponents<C_ShapeDescriptor>(), 2)
        << "only the unmarked root's part and its records remain";
}

TEST_F(PrefabParts, V1ManifestLoadsUnchanged) {
    const IRPrefab::Prefab::SpawnResult result = spawnResult(
        "v1",
        "return {\n"
        "  prefab_version = 1,\n"
        "  parts = { { id = 'ignored', shape = { type = IRShape.BOX } } },\n"
        "}\n"
    );
    ASSERT_NE(result.entity_, IREntity::kNullEntity) << result.error_;
    EXPECT_FALSE(IREntity::getComponentOptional<C_PrefabParts>(result.entity_).has_value());
    EXPECT_TRUE(m_entityManager.getChildren(result.entity_).empty());
    settle();
    EXPECT_EQ(IREntity::countComponents<C_ShapeDescriptor>(), 0);
}

TEST_F(PrefabParts, RejectsMalformedParts) {
    const auto rejects = [&](const std::string &tag, const std::string &part, const char *needle) {
        const IRPrefab::Prefab::SpawnResult result =
            spawnResult(tag, "return { prefab_version = 2, parts = { " + part + " } }\n");
        EXPECT_EQ(result.entity_, IREntity::kNullEntity) << tag;
        EXPECT_NE(result.error_.find(needle), std::string::npos) << tag << ": " << result.error_;
    };
    rejects("no_id", "{ shape = { type = IRShape.BOX } }", "non-empty string id");
    rejects("dup_id", "{ id = 'a' }, { id = 'a' }", "duplicate part id 'a'");
    rejects(
        "both",
        "{ id = 'a', voxel_ref = '/tmp/x.vxs', shape = { type = IRShape.BOX } }",
        "not both"
    );
    rejects(
        "missing_vxs",
        "{ id = 'a', voxel_ref = '/tmp/prefab_parts_missing.vxs' }",
        "not found"
    );
    rejects("bad_shape", "{ id = 'a', shape = { type = 99 } }", "IRShape");
    rejects("string_tier", "{ id = 'a', lod = { fine = 'LOD_0' } }", "lod.fine");
    rejects("tier_range", "{ id = 'a', lod = { coarse = 5 } }", "lod.coarse=5");
    rejects(
        "inverted",
        "{ id = 'a', lod = { fine = IRRender.LodLevel.LOD_3, coarse = IRRender.LodLevel.LOD_1 } }",
        "lod.fine must not be coarser"
    );
    rejects("factory", "{ id = 'a', components = { C_Nope = {} } }", "no factory registered");
    EXPECT_EQ(IREntity::countComponents<C_PrefabParts>(), 0) << "a rejected spawn leaves no root";
}

TEST_F(PrefabParts, RejectsRootLevelLod) {
    const auto rejects = [&](const std::string &tag, const std::string &body) {
        const int entitiesBefore = IREntity::countComponents<IRComponents::C_LocalTransform>();
        const IRPrefab::Prefab::SpawnResult result = spawnResult(tag, body);
        tick();
        EXPECT_EQ(result.entity_, IREntity::kNullEntity) << tag;
        EXPECT_NE(result.error_.find("root-level lod"), std::string::npos)
            << tag << ": " << result.error_;
        EXPECT_EQ(IREntity::countComponents<IRComponents::C_LocalTransform>(), entitiesBefore)
            << tag << ": a rejected spawn leaves no entity";
    };
    rejects(
        "root_lod_with_parts",
        "return { prefab_version = 2,\n"
        "  lod = { [IRRender.LodLevel.LOD_0] = 'fine' },\n" +
            std::string{kTieredParts} + "}\n"
    );
    rejects(
        "root_lod_band",
        "return { prefab_version = 2, lod = { fine = IRRender.LodLevel.LOD_0 } }\n"
    );
    EXPECT_EQ(IREntity::countComponents<C_PrefabParts>(), 0);
}

TEST_F(PrefabParts, CheckedInFlowerFixtureSpawns) {
    IRPrefab::Prefab::registerPrefab("flower", IR_FLOWER_PREFAB_PATH);
    // The fixture's voxel_ref paths are relative to the demo's exe dir.
    const std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::current_path(IR_FLOWER_ASSET_ROOT);
    setZoom(16.0f);
    const IRPrefab::Prefab::SpawnResult result =
        IRPrefab::Prefab::spawnPrefab(m_lua, "flower", vec3(0.0f));
    std::filesystem::current_path(previous);
    ASSERT_NE(result.entity_, IREntity::kNullEntity) << result.error_;
    const C_PrefabParts &parts = partsOf(result.entity_);
    EXPECT_GE(parts.slots_.size(), 3u);
    EXPECT_GE(liveCount(result.entity_), 2);
}

} // namespace

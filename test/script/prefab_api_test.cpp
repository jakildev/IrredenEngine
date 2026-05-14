#include <gtest/gtest.h>

#include <irreden/asset/rig_format.hpp>
#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/script/prefab_api.hpp>
#include <irreden/voxel/components/component_joint_hierarchy.hpp>

#include <fstream>
#include <string>

namespace {

using IRMath::vec3;
using IRMath::vec4;

constexpr const char *kTmpDir = "/tmp";

struct PrefabFiles {
    std::string voxel_path_;
    std::string rig_dir_;
    std::string rig_name_;
    std::string prefab_path_;
};

// Build a 3-shape `.vxs` + 2-joint `.rig` + matching prefab Lua on disk,
// pointing the prefab at the absolute paths. Returns the artifact set so
// individual tests can address subsets.
PrefabFiles writeFixtureSet(const std::string &tag, const std::string &prefabBody) {
    PrefabFiles f;
    f.voxel_path_ = std::string{kTmpDir} + "/prefab_test_" + tag + ".vxs";
    f.rig_dir_ = kTmpDir;
    f.rig_name_ = "prefab_test_" + tag;
    f.prefab_path_ = std::string{kTmpDir} + "/prefab_test_" + tag + ".prefab.lua";

    // Tiny shape group — 1 box primitive, default everything.
    std::vector<IRAsset::ShapeRecord> shapes(1);
    shapes[0].shapeTypeId_ = 0;
    shapes[0].params_ = vec4(1.0f, 1.0f, 1.0f, 0.0f);
    IRAsset::saveShapeGroup(f.voxel_path_, shapes);

    // 2-joint rig with deterministic translations.
    IRAsset::Rig rig;
    rig.joints_.resize(2);
    rig.joints_[0].translation_ = vec4(1.0f, 2.0f, 3.0f, 0.0f);
    rig.joints_[0].parentIndex_ = 0;
    rig.joints_[0].name_ = "root";
    rig.joints_[1].translation_ = vec4(4.0f, 5.0f, 6.0f, 0.0f);
    rig.joints_[1].parentIndex_ = 0;
    rig.joints_[1].name_ = "tip";
    IRAsset::saveRig(f.rig_name_, f.rig_dir_, rig);

    // Prefab Lua — write the supplied body verbatim, plus a return.
    std::ofstream out(f.prefab_path_);
    out << prefabBody;

    return f;
}

class PrefabApi : public testing::Test {
  protected:
    PrefabApi() {
        m_lua.bindLuaDrivenEcs();
        IRPrefab::Prefab::clearPrefabs();
    }

    ~PrefabApi() override {
        IRPrefab::Prefab::clearPrefabs();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
};

// ---- registry round-trip --------------------------------------------------

TEST_F(PrefabApi, RegisterAndLookup) {
    IRPrefab::Prefab::registerPrefab("ant", "/tmp/ant.prefab.lua");
    auto p = IRPrefab::Prefab::prefabPath("ant");
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(*p, "/tmp/ant.prefab.lua");

    // Unknown id surfaces as nullopt rather than crashing.
    EXPECT_FALSE(IRPrefab::Prefab::prefabPath("missing").has_value());
}

TEST_F(PrefabApi, RegisterOverwrite) {
    IRPrefab::Prefab::registerPrefab("ant", "/tmp/v1.lua");
    IRPrefab::Prefab::registerPrefab("ant", "/tmp/v2.lua");
    EXPECT_EQ(*IRPrefab::Prefab::prefabPath("ant"), "/tmp/v2.lua");
}

TEST_F(PrefabApi, LuaRegisterBinding) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(Prefab.register("via_lua", "/tmp/via_lua.lua"))",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    auto p = IRPrefab::Prefab::prefabPath("via_lua");
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(*p, "/tmp/via_lua.lua");
}

// ---- schema validation ----------------------------------------------------

TEST_F(PrefabApi, SpawnRejectsUnregisteredId) {
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "nope", vec3(0.0f));
    EXPECT_EQ(r.entity_, IREntity::kNullEntity);
    EXPECT_FALSE(r.error_.empty());
}

TEST_F(PrefabApi, SpawnRejectsMissingPrefabVersion) {
    PrefabFiles f = writeFixtureSet(
        "missing_version", "return { voxel_ref = '/tmp/whatever.vxs' }\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    EXPECT_EQ(r.entity_, IREntity::kNullEntity);
    EXPECT_NE(r.error_.find("prefab_version"), std::string::npos) << r.error_;
}

TEST_F(PrefabApi, SpawnRejectsFutureSchemaVersion) {
    PrefabFiles f =
        writeFixtureSet("future_version", "return { prefab_version = 99 }\n");
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    EXPECT_EQ(r.entity_, IREntity::kNullEntity);
    EXPECT_NE(r.error_.find("99"), std::string::npos) << r.error_;
}

TEST_F(PrefabApi, SpawnRejectsNonTableReturn) {
    PrefabFiles f = writeFixtureSet("non_table_return", "return 42\n");
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    EXPECT_EQ(r.entity_, IREntity::kNullEntity);
    EXPECT_NE(r.error_.find("did not return a table"), std::string::npos) << r.error_;
}

// ---- happy path: minimum-viable spawn -------------------------------------

TEST_F(PrefabApi, SpawnAttachesPosition) {
    PrefabFiles f = writeFixtureSet("minimal", "return { prefab_version = 1 }\n");
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(7.0f, 8.0f, 9.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;

    const auto &pos =
        IREntity::getComponent<IRComponents::C_Position3D>(r.entity_);
    EXPECT_FLOAT_EQ(pos.pos_.x, 7.0f);
    EXPECT_FLOAT_EQ(pos.pos_.y, 8.0f);
    EXPECT_FLOAT_EQ(pos.pos_.z, 9.0f);
}

// ---- voxel_ref load -------------------------------------------------------

TEST_F(PrefabApi, SpawnLoadsVoxelRef) {
    PrefabFiles f = writeFixtureSet(
        "voxel_ref",
        std::string{"return { prefab_version = 1, voxel_ref = '"} + std::string{kTmpDir} +
            "/prefab_test_voxel_ref.vxs' }\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;
}

TEST_F(PrefabApi, SpawnRejectsBadVoxelRef) {
    PrefabFiles f = writeFixtureSet(
        "bad_voxel_ref",
        "return { prefab_version = 1, voxel_ref = '/tmp/does_not_exist.vxs' }\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    EXPECT_EQ(r.entity_, IREntity::kNullEntity);
    EXPECT_NE(r.error_.find("voxel_ref load failed"), std::string::npos) << r.error_;
}

// ---- rig_ref load → C_JointHierarchy attached -----------------------------

TEST_F(PrefabApi, SpawnLoadsRigRefAndAttachesJointHierarchy) {
    PrefabFiles f = writeFixtureSet(
        "rig_ref",
        std::string{"return { prefab_version = 1, rig_ref = '"} + std::string{kTmpDir} +
            "/prefab_test_rig_ref.rig' }\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;

    const auto &hierarchy =
        IREntity::getComponent<IRComponents::C_JointHierarchy>(r.entity_);
    ASSERT_EQ(hierarchy.joints_.size(), 2u);
    EXPECT_FLOAT_EQ(hierarchy.joints_[0].translation_.x, 1.0f);
    EXPECT_FLOAT_EQ(hierarchy.joints_[1].translation_.x, 4.0f);
}

// ---- setup callback -------------------------------------------------------

TEST_F(PrefabApi, SetupCallbackReceivesEntity) {
    // Stash the entity id the setup function saw in a Lua global so the
    // test can read it back after spawn.
    PrefabFiles f = writeFixtureSet(
        "setup_callback",
        "g_setup_entity = nil\n"
        "return {\n"
        "  prefab_version = 1,\n"
        "  setup = function(entity)\n"
        "    g_setup_entity = entity\n"
        "  end,\n"
        "}\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;

    auto &lua = m_lua.lua();
    sol::object stored = lua["g_setup_entity"];
    ASSERT_TRUE(stored.is<IRScript::LuaEntity>())
        << "setup did not receive a LuaEntity userdata";
    EXPECT_EQ(stored.as<IRScript::LuaEntity>().entity, r.entity_);
}

TEST_F(PrefabApi, SetupCallbackErrorSurfaced) {
    PrefabFiles f = writeFixtureSet(
        "setup_error",
        "return {\n"
        "  prefab_version = 1,\n"
        "  setup = function(entity) error('boom') end,\n"
        "}\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    EXPECT_EQ(r.entity_, IREntity::kNullEntity);
    EXPECT_NE(r.error_.find("setup callback failed"), std::string::npos) << r.error_;
}

// ---- additivity: unknown top-level keys do not break the load -------------

TEST_F(PrefabApi, UnknownTopLevelFieldsIgnored) {
    PrefabFiles f = writeFixtureSet(
        "additive",
        "return { prefab_version = 1, future_feature = { a = 1 }, another = 'x' }\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    EXPECT_NE(r.entity_, IREntity::kNullEntity) << r.error_;
}

} // namespace

#include <gtest/gtest.h>

#include <irreden/asset/rig_format.hpp>
#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/components/component_zoom_level_lua.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/script/prefab_api.hpp>
#include <irreden/script/prefab_component_factory.hpp>
#include <irreden/voxel/components/component_bind_points.hpp>
#include <irreden/voxel/components/component_joint_hierarchy.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

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
        // vec3 / vec4 are bound per-creation (see creations/demos/default/
        // lua_bindings.cpp); bind a minimal version here so tests can
        // construct them in Lua bodies and read return-value fields.
        m_lua.lua().new_usertype<IRMath::vec3>(
            "vec3",
            sol::constructors<IRMath::vec3(float, float, float)>(),
            "x",
            &IRMath::vec3::x,
            "y",
            &IRMath::vec3::y,
            "z",
            &IRMath::vec3::z
        );
        m_lua.lua().new_usertype<IRMath::vec4>(
            "vec4",
            sol::constructors<IRMath::vec4(float, float, float, float)>(),
            "x",
            &IRMath::vec4::x,
            "y",
            &IRMath::vec4::y,
            "z",
            &IRMath::vec4::z,
            "w",
            &IRMath::vec4::w
        );
        IRPrefab::Prefab::clearPrefabs();
        IRPrefab::Prefab::clearComponentFactories();
    }

    ~PrefabApi() override {
        IRPrefab::Prefab::clearPrefabs();
        IRPrefab::Prefab::clearComponentFactories();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
};

// Build a rig fixture extended with two named bind points: "root" on joint 0
// with offset (10, 0, 0), and "tip" on joint 1 with offset (0, 0, 1). The
// joint chain remains the same shape as `writeFixtureSet` (joint 0 at
// translation (1,2,3); joint 1 child of 0 at translation (4,5,6); identity
// rotations).
PrefabFiles writeBindPointFixtureSet(const std::string &tag, const std::string &prefabBody) {
    PrefabFiles f;
    f.voxel_path_ = std::string{kTmpDir} + "/prefab_test_" + tag + ".vxs";
    f.rig_dir_ = kTmpDir;
    f.rig_name_ = "prefab_test_" + tag;
    f.prefab_path_ = std::string{kTmpDir} + "/prefab_test_" + tag + ".prefab.lua";

    std::vector<IRAsset::ShapeRecord> shapes(1);
    shapes[0].shapeTypeId_ = 0;
    shapes[0].params_ = vec4(1.0f, 1.0f, 1.0f, 0.0f);
    IRAsset::saveShapeGroup(f.voxel_path_, shapes);

    IRAsset::Rig rig;
    rig.joints_.resize(2);
    rig.joints_[0].translation_ = vec4(1.0f, 2.0f, 3.0f, 0.0f);
    rig.joints_[0].parentIndex_ = 0;
    rig.joints_[0].name_ = "root";
    rig.joints_[1].translation_ = vec4(4.0f, 5.0f, 6.0f, 0.0f);
    rig.joints_[1].parentIndex_ = 0;
    rig.joints_[1].name_ = "tip_joint";

    rig.bindPoints_.resize(2);
    rig.bindPoints_[0].boneId_ = 0;
    rig.bindPoints_[0].offset_ = vec3(10.0f, 0.0f, 0.0f);
    rig.bindPoints_[0].name_ = "root";
    rig.bindPoints_[1].boneId_ = 1;
    rig.bindPoints_[1].offset_ = vec3(0.0f, 0.0f, 1.0f);
    rig.bindPoints_[1].name_ = "tip";

    IRAsset::saveRig(f.rig_name_, f.rig_dir_, rig);

    std::ofstream out(f.prefab_path_);
    out << prefabBody;

    return f;
}

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
    PrefabFiles f =
        writeFixtureSet("missing_version", "return { voxel_ref = '/tmp/whatever.vxs' }\n");
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    EXPECT_EQ(r.entity_, IREntity::kNullEntity);
    EXPECT_NE(r.error_.find("prefab_version"), std::string::npos) << r.error_;
}

TEST_F(PrefabApi, SpawnRejectsFutureSchemaVersion) {
    PrefabFiles f = writeFixtureSet("future_version", "return { prefab_version = 99 }\n");
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

    const auto &pos = IREntity::getComponent<IRComponents::C_Position3D>(r.entity_);
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

    const auto &hierarchy = IREntity::getComponent<IRComponents::C_JointHierarchy>(r.entity_);
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
    ASSERT_TRUE(stored.is<IRScript::LuaEntity>()) << "setup did not receive a LuaEntity userdata";
    EXPECT_EQ(stored.as<IRScript::LuaEntity>().entity, r.entity_);
}

TEST_F(PrefabApi, SetupNonFunctionRejected) {
    PrefabFiles f = writeFixtureSet(
        "setup_not_function",
        "return {\n"
        "  prefab_version = 1,\n"
        "  setup = 42,\n"
        "}\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    EXPECT_EQ(r.entity_, IREntity::kNullEntity);
    EXPECT_NE(r.error_.find("setup must be a function"), std::string::npos) << r.error_;
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

// ---- voxel_ref SHAPES attachment ------------------------------------------

// Build a 3-record SHAPES `.vxs` with distinct offsets, shape types, and
// colors so the round-trip test can verify per-record attachment by
// inspecting each child entity.
PrefabFiles writeShapesFixture(const std::string &tag, const std::string &prefabBody) {
    PrefabFiles f;
    f.voxel_path_ = std::string{kTmpDir} + "/prefab_test_shapes_" + tag + ".vxs";
    f.rig_dir_ = kTmpDir;
    f.rig_name_ = "prefab_test_shapes_" + tag;
    f.prefab_path_ = std::string{kTmpDir} + "/prefab_test_shapes_" + tag + ".prefab.lua";

    std::vector<IRAsset::ShapeRecord> shapes(3);
    // BOX at (1, 0, 0), red
    shapes[0].shapeTypeId_ = static_cast<std::uint32_t>(IRRender::ShapeType::BOX);
    shapes[0].params_ = vec4(2.0f, 2.0f, 2.0f, 0.0f);
    shapes[0].color_ = IRMath::Color{255, 0, 0, 255};
    shapes[0].offset_ = IRMath::vec3(1.0f, 0.0f, 0.0f);
    // SPHERE at (0, 2, 0), green
    shapes[1].shapeTypeId_ = static_cast<std::uint32_t>(IRRender::ShapeType::SPHERE);
    shapes[1].params_ = vec4(1.5f, 0.0f, 0.0f, 0.0f);
    shapes[1].color_ = IRMath::Color{0, 255, 0, 255};
    shapes[1].offset_ = IRMath::vec3(0.0f, 2.0f, 0.0f);
    // CYLINDER at (0, 0, 3), blue
    shapes[2].shapeTypeId_ = static_cast<std::uint32_t>(IRRender::ShapeType::CYLINDER);
    shapes[2].params_ = vec4(0.5f, 4.0f, 0.0f, 0.0f);
    shapes[2].color_ = IRMath::Color{0, 0, 255, 255};
    shapes[2].offset_ = IRMath::vec3(0.0f, 0.0f, 3.0f);
    IRAsset::saveShapeGroup(f.voxel_path_, shapes);

    std::ofstream out(f.prefab_path_);
    out << prefabBody;
    return f;
}

TEST_F(PrefabApi, SpawnAttachesShapesAsChildren) {
    PrefabFiles f = writeShapesFixture(
        "attach",
        std::string{"return { prefab_version = 1, voxel_ref = '"} + std::string{kTmpDir} +
            "/prefab_test_shapes_attach.vxs' }\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(10.0f, 20.0f, 30.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;

    // Collect every C_ShapeDescriptor + C_Position3D entity in the world.
    // The fixture starts with an empty manager, so these are exactly the
    // SHAPES children attached by spawn (the root entity owns
    // C_Position3D but no C_ShapeDescriptor).
    struct ChildSnapshot {
        IRMath::vec3 offset_;
        IRRender::ShapeType shapeType_;
        IRMath::Color color_;
    };
    std::vector<ChildSnapshot> children;
    // forEachComponent only supports a single component type; getComponent<C_Position3D> per-entity
    // is the cleanest option available until the API gains multi-component iteration.
    IREntity::forEachComponent<IRComponents::C_ShapeDescriptor>(
        [&](IREntity::EntityId id, IRComponents::C_ShapeDescriptor &desc) {
            const auto &pos = IREntity::getComponent<IRComponents::C_Position3D>(id);
            children.push_back({pos.pos_, desc.shapeType_, desc.color_});
        }
    );

    ASSERT_EQ(children.size(), 3u);

    // The asset module's iteration order is insertion order; spawn preserves it.
    EXPECT_EQ(children[0].shapeType_, IRRender::ShapeType::BOX);
    EXPECT_FLOAT_EQ(children[0].offset_.x, 1.0f);
    EXPECT_EQ(children[0].color_.red_, 255);
    EXPECT_EQ(children[0].color_.green_, 0);

    EXPECT_EQ(children[1].shapeType_, IRRender::ShapeType::SPHERE);
    EXPECT_FLOAT_EQ(children[1].offset_.y, 2.0f);
    EXPECT_EQ(children[1].color_.green_, 255);

    EXPECT_EQ(children[2].shapeType_, IRRender::ShapeType::CYLINDER);
    EXPECT_FLOAT_EQ(children[2].offset_.z, 3.0f);
    EXPECT_EQ(children[2].color_.blue_, 255);
}

TEST_F(PrefabApi, SpawnSkipsShapesAttachmentWhenAbsent) {
    // A prefab without voxel_ref must still spawn cleanly with zero
    // shape children. Sanity check that the SHAPES path doesn't leak
    // into prefabs that opted out.
    PrefabFiles f = writeFixtureSet("no_voxel", "return { prefab_version = 1 }\n");
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;

    EXPECT_EQ(IREntity::countComponents<IRComponents::C_ShapeDescriptor>(), 0);
}

// ---- bind points: attach + IREntity.bindPoint() Lua API ---------------------

TEST_F(PrefabApi, SpawnAttachesBindPointsFromRig) {
    PrefabFiles f = writeBindPointFixtureSet(
        "bind_points",
        std::string{"return { prefab_version = 1, rig_ref = '"} + std::string{kTmpDir} +
            "/prefab_test_bind_points.rig' }\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;

    auto bpOpt = IREntity::getComponentOptional<IRComponents::C_BindPoints>(r.entity_);
    ASSERT_TRUE(bpOpt.has_value());
    const auto &bp = *bpOpt.value();
    ASSERT_EQ(bp.points_.size(), 2u);
    ASSERT_TRUE(bp.hasPoint("root"));
    ASSERT_TRUE(bp.hasPoint("tip"));
    EXPECT_EQ(bp.points_.at("tip").boneId_, 1u);
    EXPECT_FLOAT_EQ(bp.points_.at("tip").offset_.z, 1.0f);
}

TEST_F(PrefabApi, BindPointResolvesViaJointChain) {
    PrefabFiles f = writeBindPointFixtureSet(
        "bind_resolve",
        std::string{"return {\n"} +
            "  prefab_version = 1,\n"
            "  rig_ref = '" +
            std::string{kTmpDir} +
            "/prefab_test_bind_resolve.rig',\n"
            "  setup = function(entity)\n"
            "    local off, rot = IREntity.bindPoint(entity, 'tip')\n"
            "    g_off_x, g_off_y, g_off_z = off.x, off.y, off.z\n"
            "    g_rot_x, g_rot_y, g_rot_z, g_rot_w = rot.x, rot.y, rot.z, rot.w\n"
            "  end,\n"
            "}\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;

    auto &lua = m_lua.lua();
    // Joint 0 at (1,2,3) identity; joint 1 (child of 0) local (4,5,6)
    // identity; bp "tip" on joint 1 with offset (0,0,1). World offset
    // composes to (1+4+0, 2+5+0, 3+6+1) = (5, 7, 10).
    EXPECT_FLOAT_EQ(lua["g_off_x"].get<float>(), 5.0f);
    EXPECT_FLOAT_EQ(lua["g_off_y"].get<float>(), 7.0f);
    EXPECT_FLOAT_EQ(lua["g_off_z"].get<float>(), 10.0f);
    // Identity rotation propagates unchanged.
    EXPECT_FLOAT_EQ(lua["g_rot_w"].get<float>(), 1.0f);
    EXPECT_FLOAT_EQ(lua["g_rot_x"].get<float>(), 0.0f);
}

TEST_F(PrefabApi, BindPointResolvesWithNonIdentityParentRotation) {
    // Joint 0 (root): translation (0,0,0), 90° rotation around Y axis.
    // Joint 1 (child of 0): local translation (1,0,0), identity rotation.
    // Under 90°Y, (1,0,0) → (0,0,-1), so chain translation = (0,0,-1).
    // Bind point "end" on joint 1, offset (1,0,0) → (0,0,-1) under 90°Y.
    // Expected world offset = (0,0,-1) + (0,0,-1) = (0,0,-2).
    constexpr float kS =
        0.70710678118f; // sqrt(2)/2: sin/cos of 45°, the 90°Y quaternion components

    const std::string rigName = "prefab_test_rot_parent";
    const std::string prefabPath = std::string{kTmpDir} + "/prefab_test_rot_parent.prefab.lua";

    IRAsset::Rig rig;
    rig.joints_.resize(2);
    rig.joints_[0].translation_ = vec4(0.0f, 0.0f, 0.0f, 0.0f);
    rig.joints_[0].rotation_ = vec4(0.0f, kS, 0.0f, kS); // 90° around Y: (qx=0, qy=kS, qz=0, qw=kS)
    rig.joints_[0].parentIndex_ = 0;
    rig.joints_[0].name_ = "root";
    rig.joints_[1].translation_ = vec4(1.0f, 0.0f, 0.0f, 0.0f);
    rig.joints_[1].rotation_ = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    rig.joints_[1].parentIndex_ = 0;
    rig.joints_[1].name_ = "end_joint";

    rig.bindPoints_.resize(1);
    rig.bindPoints_[0].boneId_ = 1;
    rig.bindPoints_[0].offset_ = vec3(1.0f, 0.0f, 0.0f);
    rig.bindPoints_[0].name_ = "end";

    IRAsset::saveRig(rigName, kTmpDir, rig);

    {
        std::ofstream out(prefabPath);
        out << "return {\n"
               "  prefab_version = 1,\n"
               "  rig_ref = '"
            << std::string{kTmpDir} << "/" << rigName
            << ".rig',\n"
               "  setup = function(entity)\n"
               "    local off, rot = IREntity.bindPoint(entity, 'end')\n"
               "    g_off_x, g_off_y, g_off_z = off.x, off.y, off.z\n"
               "    g_rot_x, g_rot_y, g_rot_z, g_rot_w = rot.x, rot.y, rot.z, rot.w\n"
               "  end,\n"
               "}\n";
    }

    IRPrefab::Prefab::registerPrefab("p", prefabPath);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;

    auto &lua = m_lua.lua();
    constexpr float kEps = 1e-5f;
    EXPECT_NEAR(lua["g_off_x"].get<float>(), 0.0f, kEps);
    EXPECT_NEAR(lua["g_off_y"].get<float>(), 0.0f, kEps);
    EXPECT_NEAR(lua["g_off_z"].get<float>(), -2.0f, kEps);
    // World rotation = 90°Y: (qx=0, qy=kS, qz=0, qw=kS).
    EXPECT_NEAR(lua["g_rot_x"].get<float>(), 0.0f, kEps);
    EXPECT_NEAR(lua["g_rot_y"].get<float>(), kS, kEps);
    EXPECT_NEAR(lua["g_rot_z"].get<float>(), 0.0f, kEps);
    EXPECT_NEAR(lua["g_rot_w"].get<float>(), kS, kEps);
}

TEST_F(PrefabApi, BindPointOverridesApplied) {
    PrefabFiles f = writeBindPointFixtureSet(
        "bind_override",
        std::string{"return {\n"} +
            "  prefab_version = 1,\n"
            "  rig_ref = '" +
            std::string{kTmpDir} +
            "/prefab_test_bind_override.rig',\n"
            "  bind_point_overrides = {\n"
            "    tip = { offset = vec3.new(2, 2, 2) },\n"
            "  },\n"
            "  setup = function(entity)\n"
            "    local off, _ = IREntity.bindPoint(entity, 'tip')\n"
            "    g_off_x, g_off_y, g_off_z = off.x, off.y, off.z\n"
            "  end,\n"
            "}\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;

    auto &lua = m_lua.lua();
    // Override changes bp.offset from (0,0,1) to (2,2,2). World offset
    // = chain world (5,7,9) + override (2,2,2) = (7,9,11).
    EXPECT_FLOAT_EQ(lua["g_off_x"].get<float>(), 7.0f);
    EXPECT_FLOAT_EQ(lua["g_off_y"].get<float>(), 9.0f);
    EXPECT_FLOAT_EQ(lua["g_off_z"].get<float>(), 11.0f);
}

TEST_F(PrefabApi, BindPointMissingReturnsNil) {
    PrefabFiles f = writeBindPointFixtureSet(
        "bind_missing",
        std::string{"return {\n"} +
            "  prefab_version = 1,\n"
            "  rig_ref = '" +
            std::string{kTmpDir} +
            "/prefab_test_bind_missing.rig',\n"
            "  setup = function(entity)\n"
            "    local off, rot = IREntity.bindPoint(entity, 'nope')\n"
            "    g_off_is_nil = (off == nil)\n"
            "    g_rot_is_nil = (rot == nil)\n"
            "  end,\n"
            "}\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;

    auto &lua = m_lua.lua();
    EXPECT_TRUE(lua["g_off_is_nil"].get<bool>());
    EXPECT_TRUE(lua["g_rot_is_nil"].get<bool>());
}

// ---- voxel_ref DENSE / HYBRID attachment ----------------------------------

// Build a small DENSE `.vxs` (2×2×2 = 8 voxels) at a non-zero origin so
// the round-trip exercises both bounds and per-voxel record carry-over.
// Returns the fixture set so individual tests can address subsets.
PrefabFiles writeDenseFixture(const std::string &tag, const std::string &prefabBody) {
    PrefabFiles f;
    f.voxel_path_ = std::string{kTmpDir} + "/prefab_test_dense_" + tag + ".vxs";
    f.rig_dir_ = kTmpDir;
    f.rig_name_ = "prefab_test_dense_" + tag;
    f.prefab_path_ = std::string{kTmpDir} + "/prefab_test_dense_" + tag + ".prefab.lua";

    IRAsset::DenseVoxelSet dense;
    dense.boundsMin_ = IRMath::ivec3(1, 2, 3);
    dense.boundsMax_ = IRMath::ivec3(3, 4, 5); // 2x2x2 = 8 voxels
    dense.voxels_.resize(8);
    for (std::size_t i = 0; i < dense.voxels_.size(); ++i) {
        // Per-voxel signature so a round-trip mismatch surfaces in a test
        // assertion rather than a silent value drift.
        dense.voxels_[i].color_ = IRMath::Color{static_cast<std::uint8_t>(10 + i), 0, 0, 255};
        dense.voxels_[i].material_id_ = static_cast<std::uint8_t>(i);
        dense.voxels_[i].bone_id_ = static_cast<std::uint8_t>(i * 2);
    }
    IRAsset::saveDenseVoxelSet(f.voxel_path_, dense);

    std::ofstream out(f.prefab_path_);
    out << prefabBody;
    return f;
}

// Same shape as `writeDenseFixture` but emits a HYBRID `.vxs` — a small
// SHAPES half (one BOX) plus the DENSE half — to confirm both attach
// on the same root entity from a single voxel_ref.
PrefabFiles writeHybridFixture(const std::string &tag, const std::string &prefabBody) {
    PrefabFiles f;
    f.voxel_path_ = std::string{kTmpDir} + "/prefab_test_hybrid_" + tag + ".vxs";
    f.rig_dir_ = kTmpDir;
    f.rig_name_ = "prefab_test_hybrid_" + tag;
    f.prefab_path_ = std::string{kTmpDir} + "/prefab_test_hybrid_" + tag + ".prefab.lua";

    std::vector<IRAsset::ShapeRecord> shapes(1);
    shapes[0].shapeTypeId_ = static_cast<std::uint32_t>(IRRender::ShapeType::BOX);
    shapes[0].params_ = vec4(1.0f, 1.0f, 1.0f, 0.0f);
    shapes[0].color_ = IRMath::Color{200, 100, 50, 255};
    shapes[0].offset_ = IRMath::vec3(5.0f, 0.0f, 0.0f);

    IRAsset::DenseVoxelSet dense;
    dense.boundsMin_ = IRMath::ivec3(0);
    dense.boundsMax_ = IRMath::ivec3(2, 2, 1); // 2x2x1 = 4 voxels
    dense.voxels_.resize(4);
    for (std::size_t i = 0; i < dense.voxels_.size(); ++i) {
        dense.voxels_[i].color_ = IRMath::Color{0, static_cast<std::uint8_t>(30 + i), 0, 255};
    }
    IRAsset::saveVoxelSet(f.voxel_path_, shapes, dense);

    std::ofstream out(f.prefab_path_);
    out << prefabBody;
    return f;
}

TEST_F(PrefabApi, SpawnAttachesDenseVoxelSetHeadless) {
    PrefabFiles f = writeDenseFixture(
        "attach",
        std::string{"return { prefab_version = 1, voxel_ref = '"} + std::string{kTmpDir} +
            "/prefab_test_dense_attach.vxs' }\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;

    // Record count matches `dense.voxelCount()` per the T-189 acceptance
    // criterion. The test runs headless (no canvas), so the data lives in
    // `pendingVoxels_` and `numVoxels_` is 0 — `recordCount()` unifies
    // the two paths.
    const auto &voxelSet = IREntity::getComponent<IRComponents::C_VoxelSetNew>(r.entity_);
    EXPECT_EQ(voxelSet.recordCount(), 8u);
    EXPECT_EQ(voxelSet.numVoxels_, 0); // headless → staged, not pool-allocated
    EXPECT_EQ(voxelSet.canvasEntity_, IREntity::kNullEntity);
    EXPECT_EQ(voxelSet.pendingBoundsMin_.x, 1);
    EXPECT_EQ(voxelSet.pendingBoundsMin_.y, 2);
    EXPECT_EQ(voxelSet.pendingBoundsMin_.z, 3);

    // The bounds → size derivation must agree with `voxelCount()`.
    ASSERT_EQ(voxelSet.pendingVoxels_.size(), 8u);
    // Spot-check a couple of records — the per-voxel signature emitted by
    // the fixture carries through unmodified.
    EXPECT_EQ(voxelSet.pendingVoxels_[0].color_.red_, 10);
    EXPECT_EQ(voxelSet.pendingVoxels_[7].color_.red_, 17);
    EXPECT_EQ(voxelSet.pendingVoxels_[3].material_id_, 3);
    EXPECT_EQ(voxelSet.pendingVoxels_[3].bone_id_, 6);
}

TEST_F(PrefabApi, SpawnAttachesHybridShapesAndDenseOnSameEntity) {
    PrefabFiles f = writeHybridFixture(
        "attach",
        std::string{"return { prefab_version = 1, voxel_ref = '"} + std::string{kTmpDir} +
            "/prefab_test_hybrid_attach.vxs' }\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;

    // SHAPES half — one BOX child entity attached under the root.
    EXPECT_EQ(IREntity::countComponents<IRComponents::C_ShapeDescriptor>(), 1);

    // DENSE half — C_VoxelSetNew on the same root entity, record count
    // equals the dense voxel volume.
    const auto &voxelSet = IREntity::getComponent<IRComponents::C_VoxelSetNew>(r.entity_);
    EXPECT_EQ(voxelSet.recordCount(), 4u);
    EXPECT_EQ(voxelSet.pendingVoxels_.size(), 4u);
    EXPECT_EQ(voxelSet.pendingVoxels_[1].color_.green_, 31);
}

// ---- declarative components table (#698) ----------------------------------

TEST_F(PrefabApi, ComponentsTableAttachesAndAppliesOverride) {
    // Binding registers the factory as a side effect; mirrors the wiring
    // a creation does via `registerTypesFromTraits<C_ZoomLevel>()`.
    IRScript::bindLuaType<IRComponents::C_ZoomLevel>(m_lua);

    PrefabFiles f = writeFixtureSet(
        "components_zoom",
        "return {\n"
        "  prefab_version = 1,\n"
        "  components = { C_ZoomLevel = { zoom = 5.0 } },\n"
        "}\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;

    const auto &z = IREntity::getComponent<IRComponents::C_ZoomLevel>(r.entity_);
    EXPECT_FLOAT_EQ(z.zoom_.x, 5.0f);
    EXPECT_FLOAT_EQ(z.zoom_.y, 5.0f);
}

TEST_F(PrefabApi, ComponentsTableEmptyOverrideUsesDefaults) {
    // Empty override table → component constructed with its default ctor.
    IRScript::bindLuaType<IRComponents::C_ZoomLevel>(m_lua);

    PrefabFiles f = writeFixtureSet(
        "components_defaults",
        "return {\n"
        "  prefab_version = 1,\n"
        "  components = { C_ZoomLevel = {} },\n"
        "}\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;

    const IRComponents::C_ZoomLevel defaults{};
    const auto &z = IREntity::getComponent<IRComponents::C_ZoomLevel>(r.entity_);
    EXPECT_FLOAT_EQ(z.zoom_.x, defaults.zoom_.x);
    EXPECT_FLOAT_EQ(z.zoom_.y, defaults.zoom_.y);
}

TEST_F(PrefabApi, ComponentsTableUnknownComponentErrors) {
    // No factory registered for `C_DoesNotExist`. Spawn fails with a
    // message naming the missing factory and surfacing the binding-fix
    // hint; the entity is destroyed so the registry is not left dirty.
    PrefabFiles f = writeFixtureSet(
        "components_unknown",
        "return {\n"
        "  prefab_version = 1,\n"
        "  components = { C_DoesNotExist = {} },\n"
        "}\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    EXPECT_EQ(r.entity_, IREntity::kNullEntity);
    EXPECT_NE(r.error_.find("no factory registered"), std::string::npos) << r.error_;
    EXPECT_NE(r.error_.find("C_DoesNotExist"), std::string::npos) << r.error_;
}

TEST_F(PrefabApi, ComponentsTableNonTableEntryErrors) {
    // A non-table value (`components = { C_ZoomLevel = 42 }`) is a
    // schema error — catches the typo class instead of silently no-op'ing.
    IRScript::bindLuaType<IRComponents::C_ZoomLevel>(m_lua);

    PrefabFiles f = writeFixtureSet(
        "components_non_table",
        "return {\n"
        "  prefab_version = 1,\n"
        "  components = { C_ZoomLevel = 42 },\n"
        "}\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    EXPECT_EQ(r.entity_, IREntity::kNullEntity);
    EXPECT_NE(r.error_.find("must be a table"), std::string::npos) << r.error_;
}

TEST_F(PrefabApi, ComponentsRunBeforeSetupCallback) {
    // Verify components attach before setup runs: final zoom is 7.5f and
    // setup callback executed ('ran' flag set).
    IRScript::bindLuaType<IRComponents::C_ZoomLevel>(m_lua);

    PrefabFiles f = writeFixtureSet(
        "components_before_setup",
        "g_zoom = nil\n"
        "return {\n"
        "  prefab_version = 1,\n"
        "  components = { C_ZoomLevel = { zoom = 7.5 } },\n"
        "  setup = function(entity)\n"
        "    g_zoom = 'ran'\n"
        "  end,\n"
        "}\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;

    const auto &z = IREntity::getComponent<IRComponents::C_ZoomLevel>(r.entity_);
    EXPECT_FLOAT_EQ(z.zoom_.x, 7.5f);
    EXPECT_EQ(m_lua.lua()["g_zoom"].get<std::string>(), "ran");
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

// ---- rotation_mode + unbounded (Epic C C2) --------------------------------

TEST_F(PrefabApi, SpawnDefaultsToGridRotationMode) {
    PrefabFiles f = writeFixtureSet("rot_default", "return { prefab_version = 1 }\n");
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;

    auto modeOpt = IREntity::getComponentOptional<IRComponents::C_RotationMode>(r.entity_);
    ASSERT_TRUE(modeOpt.has_value());
    EXPECT_EQ(modeOpt.value()->mode_, IRComponents::RotationMode::GRID);
    // unbounded_ defaults to false on the auto-attached C_LocalTransform.
    const auto &lt = IREntity::getComponent<IRComponents::C_LocalTransform>(r.entity_);
    EXPECT_FALSE(lt.unbounded_);
    // GRID does not attach C_EntityCanvas.
    auto canvasOpt = IREntity::getComponentOptional<IRComponents::C_EntityCanvas>(r.entity_);
    EXPECT_FALSE(canvasOpt.has_value());
}

TEST_F(PrefabApi, SpawnRotationModeGridExplicit) {
    PrefabFiles f =
        writeFixtureSet("rot_grid", "return { prefab_version = 1, rotation_mode = 'GRID' }\n");
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;

    auto modeOpt = IREntity::getComponentOptional<IRComponents::C_RotationMode>(r.entity_);
    ASSERT_TRUE(modeOpt.has_value());
    EXPECT_EQ(modeOpt.value()->mode_, IRComponents::RotationMode::GRID);
}

TEST_F(PrefabApi, SpawnRotationModeDetachedAttachesComponent) {
    // No active RenderManager in the test fixture, so spawn skips the
    // GPU-backed canvas allocation (logged as a warning) but still tags
    // the entity DETACHED for archetype-filtered systems to see. The
    // runtime mode-change helper (`IRPrefab::RotationMode::setMode`)
    // picks the canvas up once a RenderManager exists.
    PrefabFiles f = writeFixtureSet(
        "rot_detached",
        "return { prefab_version = 1, rotation_mode = 'DETACHED', "
        "canvas_size = { x = 64, y = 64 } }\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;

    auto modeOpt = IREntity::getComponentOptional<IRComponents::C_RotationMode>(r.entity_);
    ASSERT_TRUE(modeOpt.has_value());
    EXPECT_EQ(modeOpt.value()->mode_, IRComponents::RotationMode::DETACHED);
}

TEST_F(PrefabApi, SpawnUnboundedSetsLocalTransformFlag) {
    PrefabFiles f = writeFixtureSet(
        "rot_unbounded",
        "return { prefab_version = 1, rotation_mode = 'DETACHED', unbounded = true, "
        "canvas_size = { x = 16, y = 16 } }\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    ASSERT_NE(r.entity_, IREntity::kNullEntity) << r.error_;

    const auto &lt = IREntity::getComponent<IRComponents::C_LocalTransform>(r.entity_);
    EXPECT_TRUE(lt.unbounded_);
}

TEST_F(PrefabApi, SpawnRejectsUnknownRotationMode) {
    PrefabFiles f =
        writeFixtureSet("rot_bogus", "return { prefab_version = 1, rotation_mode = 'WOBBLE' }\n");
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    EXPECT_EQ(r.entity_, IREntity::kNullEntity);
    EXPECT_NE(r.error_.find("WOBBLE"), std::string::npos) << r.error_;
}

TEST_F(PrefabApi, SpawnDetachedRequiresCanvasSize) {
    PrefabFiles f = writeFixtureSet(
        "rot_no_canvas_size",
        "return { prefab_version = 1, rotation_mode = 'DETACHED' }\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    EXPECT_EQ(r.entity_, IREntity::kNullEntity);
    EXPECT_NE(r.error_.find("canvas_size"), std::string::npos) << r.error_;
}

TEST_F(PrefabApi, SpawnRejectsNonPositiveCanvasSize) {
    PrefabFiles f = writeFixtureSet(
        "rot_zero_canvas_size",
        "return { prefab_version = 1, rotation_mode = 'DETACHED', "
        "canvas_size = { x = 0, y = 32 } }\n"
    );
    IRPrefab::Prefab::registerPrefab("p", f.prefab_path_);
    auto r = IRPrefab::Prefab::spawnPrefab(m_lua, "p", vec3(0.0f));
    EXPECT_EQ(r.entity_, IREntity::kNullEntity);
    EXPECT_NE(r.error_.find("positive"), std::string::npos) << r.error_;
}

} // namespace

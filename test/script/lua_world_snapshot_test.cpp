#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/script/lua_script.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_position_int_3d.hpp>
#include <irreden/common/components/component_size_int_3d.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <sol/sol.hpp>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// Persist P7 (#2218, epic #667): the `IRPersist` Lua binding (W-9) + the
// IR_PERSIST_DUMP `.json.txt` debug dump (W-11). Drives the real Lua surface
// (`m_lua.bindLuaDrivenEcs()`) against a live EntityManager built like
// world_snapshot_test, so a Lua `saveWorld`/`loadWorld` round-trips real engine
// components through the process-default registry (C_LocalTransform,
// C_PositionInt3D, C_SizeInt3D — the trivially-copyable POD members, plus
// C_VoxelSetNew, the one component with an explicit SaveSerialize<C>).

namespace {

using IRComponents::C_LocalTransform;
using IRComponents::C_PositionInt3D;
using IRComponents::C_SizeInt3D;
using IRComponents::C_Voxel;
using IRComponents::C_VoxelSetNew;
using IREntity::EntityId;

std::vector<std::uint8_t> readFileBytes(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()
    );
}

bool fileExists(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    return in.good();
}

// Portable IR_PERSIST_DUMP toggle so the flag-on / flag-off dump test runs on
// Linux/macOS (setenv/unsetenv) and native Windows (_putenv_s).
void setDumpFlag(bool on) {
#if defined(_WIN32)
    _putenv_s("IR_PERSIST_DUMP", on ? "1" : "");
#else
    if (on) {
        setenv("IR_PERSIST_DUMP", "1", 1);
    } else {
        unsetenv("IR_PERSIST_DUMP");
    }
#endif
}

class LuaWorldSnapshotTest : public testing::Test {
  protected:
    LuaWorldSnapshotTest()
        : m_lua{}
        , m_entity_manager{}
        , m_system_manager{} {
        m_lua.bindLuaDrivenEcs();
    }

    void TearDown() override {
        setDumpFlag(false); // never leak the flag into a sibling test
    }

    bool evalTrue(const std::string &expr) {
        auto result = m_lua.lua().safe_script(
            "return (" + expr + ") and true or false",
            sol::script_pass_on_error
        );
        return result.valid() && result.get<bool>();
    }

    bool runOk(const std::string &stmt) {
        return m_lua.lua().safe_script(stmt, sol::script_pass_on_error).valid();
    }

    bool raisesError(const std::string &stmt) {
        return !m_lua.lua().safe_script(stmt, sol::script_pass_on_error).valid();
    }

    // The CHILD_OF parent of @p child, or kNullEntity (matches the query
    // world_snapshot_relations_test uses).
    EntityId parentOf(EntityId child) {
        return m_entity_manager.getParentEntityFromArchetype(
            m_entity_manager.getEntityArchetype(child)
        );
    }

    std::string tempPath(const char *name) const {
        return testing::TempDir() + "/ir_ws_lua_" + name + ".irws";
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(LuaWorldSnapshotTest, SurfaceBound) {
    EXPECT_TRUE(evalTrue("type(IRPersist) == 'table'"));
    EXPECT_TRUE(evalTrue("type(IRPersist.saveWorld) == 'function'"));
    EXPECT_TRUE(evalTrue("type(IRPersist.loadWorld) == 'function'"));
}

// The core W-9 acceptance: a non-trivial world (multiple archetypes +
// CHILD_OF) built in C++, saved and reloaded entirely through the Lua binding,
// with exact component + relation parity on restore.
TEST_F(LuaWorldSnapshotTest, RoundTripThroughLua) {
    // Archetype {C_LocalTransform} — the relation parent + a bare peer.
    const EntityId parent =
        m_entity_manager.createEntity(C_LocalTransform{IRMath::vec3(1.0f, 2.0f, 3.0f)});
    const EntityId peer =
        m_entity_manager.createEntity(C_LocalTransform{IRMath::vec3(-4.0f, 5.0f, 6.0f)});
    // Archetype {C_PositionInt3D, C_SizeInt3D} — the relation child.
    const EntityId child =
        m_entity_manager.createEntity(C_PositionInt3D{4, 5, 6}, C_SizeInt3D{7, 8, 9});
    // Archetype {C_PositionInt3D} and {C_LocalTransform, C_PositionInt3D} so the
    // save spans 4 distinct projected archetypes.
    const EntityId gridOnly = m_entity_manager.createEntity(C_PositionInt3D{10, 11, 12});
    const EntityId both = m_entity_manager.createEntity(
        C_LocalTransform{IRMath::vec3(7.0f, 8.0f, 9.0f)},
        C_PositionInt3D{13, 14, 15}
    );
    IREntity::setParent(child, parent);
    ASSERT_EQ(parentOf(child), parent);

    const std::string path = tempPath("roundtrip");
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + path + "'))"))
        << "Lua IRPersist.saveWorld raised or returned false";

    // Clear the world (the reset the loadWorld contract expects), then reload
    // through the Lua binding.
    m_entity_manager.destroyAllEntities();
    ASSERT_EQ(m_entity_manager.getLiveEntityCount(), 0u);
    ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + path + "'))"))
        << "Lua IRPersist.loadWorld raised or returned false";

    // Exact-id restoration + component parity.
    ASSERT_TRUE(m_entity_manager.entityExists(parent));
    ASSERT_TRUE(m_entity_manager.entityExists(child));
    ASSERT_TRUE(m_entity_manager.entityExists(peer));
    ASSERT_TRUE(m_entity_manager.entityExists(gridOnly));
    ASSERT_TRUE(m_entity_manager.entityExists(both));

    const C_LocalTransform &parentXform = m_entity_manager.getComponent<C_LocalTransform>(parent);
    EXPECT_FLOAT_EQ(parentXform.translation_.x, 1.0f);
    EXPECT_FLOAT_EQ(parentXform.translation_.y, 2.0f);
    EXPECT_FLOAT_EQ(parentXform.translation_.z, 3.0f);

    const IRMath::ivec3 childPos = m_entity_manager.getComponent<C_PositionInt3D>(child).pos_;
    EXPECT_EQ(childPos.x, 4);
    EXPECT_EQ(childPos.y, 5);
    EXPECT_EQ(childPos.z, 6);
    const IRMath::ivec3 childSize = m_entity_manager.getComponent<C_SizeInt3D>(child).size_;
    EXPECT_EQ(childSize.x, 7);
    EXPECT_EQ(childSize.y, 8);
    EXPECT_EQ(childSize.z, 9);
    const IRMath::ivec3 gridPos = m_entity_manager.getComponent<C_PositionInt3D>(gridOnly).pos_;
    EXPECT_EQ(gridPos.x, 10);
    EXPECT_EQ(gridPos.z, 12);
    const IRMath::ivec3 bothPos = m_entity_manager.getComponent<C_PositionInt3D>(both).pos_;
    EXPECT_EQ(bothPos.x, 13);
    EXPECT_EQ(bothPos.z, 15);

    // CHILD_OF edge restored.
    EXPECT_EQ(parentOf(child), parent) << "CHILD_OF edge lost on reload";
}

// `makeDefaultSaveRegistry()` registers C_VoxelSetNew (world_default_registry.cpp)
// alongside the trivially-copyable PODs above — the one component with an
// explicit SaveSerialize<C> (persist P6, #2217) rather than the default
// byte-copy path. Round-trips a headless, pool-free set (StagedInit — no
// VoxelPool/canvas needed) through the Lua binding; reload always
// reconstructs in staged mode (numVoxels_ == 0, pendingVoxels_ populated) —
// attaching it to a live pool is a separate step the caller's UPDATE
// pipeline performs via SEED_STAGED_VOXELS (lua_world_snapshot_bindings.hpp).
TEST_F(LuaWorldSnapshotTest, RoundTripsVoxelSetNew) {
    const IRMath::ivec3 size{2, 1, 2};
    const IRMath::ivec3 boundsMin{3, -1, 5};
    const IREntity::EntityId canvas = 777;
    std::vector<C_Voxel> voxels(4);
    for (std::size_t i = 0; i < voxels.size(); ++i) {
        voxels[i].color_ = IRMath::Color{
            static_cast<std::uint8_t>(i * 10 + 1),
            static_cast<std::uint8_t>(i * 20 + 2),
            static_cast<std::uint8_t>(i * 30 + 3),
            255
        };
    }

    const EntityId entity = m_entity_manager.createEntity(
        C_VoxelSetNew{C_VoxelSetNew::StagedInit{}, size, boundsMin, voxels, canvas}
    );

    const std::string path = tempPath("voxelset");
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + path + "'))"));

    m_entity_manager.destroyAllEntities();
    ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + path + "'))"));

    ASSERT_TRUE(m_entity_manager.entityExists(entity));
    const C_VoxelSetNew &reloaded = m_entity_manager.getComponent<C_VoxelSetNew>(entity);
    EXPECT_EQ(reloaded.size_.x, size.x);
    EXPECT_EQ(reloaded.size_.y, size.y);
    EXPECT_EQ(reloaded.size_.z, size.z);
    EXPECT_EQ(reloaded.pendingBoundsMin_.x, boundsMin.x);
    EXPECT_EQ(reloaded.pendingBoundsMin_.y, boundsMin.y);
    EXPECT_EQ(reloaded.pendingBoundsMin_.z, boundsMin.z);
    EXPECT_EQ(reloaded.canvasEntity_, canvas);
    EXPECT_EQ(reloaded.numVoxels_, 0) << "reload must stay staged — never touches a pool";
    ASSERT_EQ(reloaded.pendingVoxels_.size(), voxels.size());
    for (std::size_t i = 0; i < voxels.size(); ++i) {
        EXPECT_EQ(0, std::memcmp(&reloaded.pendingVoxels_[i], &voxels[i], sizeof(C_Voxel)))
            << "voxel record " << i << " differs after round-trip";
    }
}

// Missing/corrupt path -> false with no Lua error (I/O failure is a bool, not
// an exception). evalTrue would report failure if the call raised, so this
// covers both halves of the contract. (Zero *gameplay* mutation on a failed
// load is the world layer's own guarantee, exercised by world_snapshot_test;
// here loadWorld still mints the default registry's component-backing entities,
// which is not saved-content mutation.)
TEST_F(LuaWorldSnapshotTest, MissingPathReturnsFalseNoThrow) {
    EXPECT_TRUE(evalTrue("IRPersist.loadWorld('" + tempPath("does_not_exist") + "') == false"));
}

// A non-string argument is a misuse -> Lua error (sol coercion), not a silent
// false — mirrors the IRSave binding's I/O-fails->bool / misuse->error split.
TEST_F(LuaWorldSnapshotTest, NonStringArgRaises) {
    EXPECT_TRUE(raisesError("IRPersist.saveWorld({})"));
    EXPECT_TRUE(raisesError("IRPersist.loadWorld({})"));
}

// W-11: with IR_PERSIST_DUMP set, saveWorld emits a `<path>.json.txt` detailed
// dump containing a known entity id + the CHILD_OF relation name; with the flag
// unset the dump is absent AND the binary is byte-identical (side-output only).
TEST_F(LuaWorldSnapshotTest, DumpModeAndByteParity) {
    const EntityId parent =
        m_entity_manager.createEntity(C_LocalTransform{IRMath::vec3(1.0f, 1.0f, 1.0f)});
    const EntityId child = m_entity_manager.createEntity(C_PositionInt3D{2, 3, 4});
    IREntity::setParent(child, parent);
    const EntityId maskedChild = child & IREntity::IR_ENTITY_ID_BITS;

    const std::string dumpPath = tempPath("dump_on");
    const std::string plainPath = tempPath("dump_off");

    setDumpFlag(true);
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + dumpPath + "'))"));
    setDumpFlag(false);
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + plainPath + "'))"));

    // Flag-off byte parity: the binary is identical regardless of the dump.
    EXPECT_EQ(readFileBytes(dumpPath), readFileBytes(plainPath));

    // Flag-on produced the dump; flag-off did not.
    ASSERT_TRUE(fileExists(dumpPath + ".json.txt"));
    EXPECT_FALSE(fileExists(plainPath + ".json.txt"));

    const std::vector<std::uint8_t> dumpBytes = readFileBytes(dumpPath + ".json.txt");
    const std::string dump(dumpBytes.begin(), dumpBytes.end());
    ASSERT_FALSE(dump.empty());
    EXPECT_EQ(dump.front(), '{'); // JSON object shape
    EXPECT_NE(dump.find("\"format\""), std::string::npos);
    EXPECT_NE(dump.find("CHILD_OF"), std::string::npos);                  // relation name
    EXPECT_NE(dump.find(std::to_string(maskedChild)), std::string::npos); // known id
}

} // namespace

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>

#include <irreden/script/lua_script.hpp>

#include <algorithm>
#include <string>
#include <vector>

// Seam coverage for the IREntity hierarchy bindings: every case drives the
// verb from Lua and reads the result back through the EntityManager.

namespace {

using IRComponents::C_LocalTransform;
using IRComponents::C_WorldTransform;
using IREntity::EntityId;

struct TestPartMarker {};

class LuaHierarchy : public testing::Test {
  protected:
    LuaHierarchy() {
        m_lua.bindLuaDrivenEcs();
    }

    void run(const std::string &src) {
        auto result = m_lua.lua().safe_script(src, sol::script_pass_on_error);
        ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    }

    std::string runExpectingError(const std::string &src) {
        auto result = m_lua.lua().safe_script(src, sol::script_pass_on_error);
        EXPECT_FALSE(result.valid());
        if (result.valid()) {
            return {};
        }
        return sol::error{result}.what();
    }

    void bind(const char *name, EntityId entity) {
        m_lua.lua()[name] = static_cast<lua_Integer>(entity);
    }

    std::vector<EntityId> sorted(std::vector<EntityId> ids) {
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    // m_lua first so the sol::state outlives the EntityManager's staged
    // callbacks.
    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(LuaHierarchy, SetGetClearParent) {
    const EntityId first = IREntity::createEntity();
    const EntityId second = IREntity::createEntity();
    const EntityId child = IREntity::createEntity();
    bind("first", first);
    bind("second", second);
    bind("child", child);
    m_lua.lua()["childHandle"] = IRScript::LuaEntity{child};

    run(R"lua(
        assert(IREntity.getParent(child) == nil, "fresh entity is a root")
        IREntity.setParent(childHandle, first)
        assert(IREntity.getParent(child) == first, "parented")
        IREntity.setParent(child, second)
        assert(IREntity.getParent(child) == second, "re-parented")
    )lua");
    EXPECT_TRUE(m_entity_manager.getChildren(first).empty());
    EXPECT_EQ(m_entity_manager.getChildren(second), std::vector<EntityId>{child});

    run(R"lua(
        IREntity.clearParent(child)
        assert(IREntity.getParent(child) == nil, "unparented")
        IREntity.clearParent(child)
    )lua");
    EXPECT_EQ(m_entity_manager.getParent(child), IREntity::kNullEntity);
    EXPECT_TRUE(m_entity_manager.getChildren(second).empty());
}

TEST_F(LuaHierarchy, SetParentRejectsCycles) {
    const EntityId root = IREntity::createEntity();
    const EntityId mid = IREntity::createEntity();
    const EntityId leaf = IREntity::createEntity();
    IREntity::setParent(mid, root);
    IREntity::setParent(leaf, mid);
    bind("root", root);
    bind("leaf", leaf);

    EXPECT_NE(runExpectingError("IREntity.setParent(root, leaf)").find("cycle"), std::string::npos);
    EXPECT_NE(runExpectingError("IREntity.setParent(root, root)").find("cycle"), std::string::npos);
    EXPECT_EQ(m_entity_manager.getParent(root), IREntity::kNullEntity);
    EXPECT_EQ(m_entity_manager.getParent(leaf), mid);
}

TEST_F(LuaHierarchy, RejectsDeadAndMalformedIds) {
    const EntityId dead = IREntity::createEntity();
    m_entity_manager.destroyEntity(dead);
    bind("dead", dead);

    EXPECT_NE(runExpectingError("IREntity.getParent(dead)").find("not a live"), std::string::npos);
    EXPECT_NE(
        runExpectingError("IREntity.getParent('root')").find("entity id or LuaEntity"),
        std::string::npos
    );
    EXPECT_NE(
        runExpectingError("IREntity.getParent(1.5)").find("integer entity id"),
        std::string::npos
    );
    // A deferred create is not placed until the flush.
    EXPECT_NE(
        runExpectingError("IREntity.getParent(IREntity.deferredCreate())").find("not a live"),
        std::string::npos
    );
}

// Children in different archetype nodes are all visited; grandchildren are
// not.
TEST_F(LuaHierarchy, ForEachChildSeesEveryChild) {
    const EntityId root = IREntity::createEntity();
    std::vector<EntityId> children;
    for (int i = 0; i < 4; ++i) {
        children.push_back(
            i % 2 == 0 ? IREntity::createEntity() : IREntity::createEntity(TestPartMarker{})
        );
        IREntity::setParent(children.back(), root);
    }
    const EntityId grandchild = IREntity::createEntity();
    IREntity::setParent(grandchild, children.front());
    bind("root", root);

    run(R"lua(
        seen = {}
        IREntity.forEachChild(root, function(id) seen[#seen + 1] = id end)
    )lua");
    sol::table seen = m_lua.lua()["seen"];
    std::vector<EntityId> visited;
    for (std::size_t i = 1; i <= seen.size(); ++i) {
        visited.push_back(seen.get<EntityId>(i));
    }
    EXPECT_EQ(sorted(visited), sorted(children));

    EXPECT_NE(
        runExpectingError("IREntity.forEachChild(root, function(id) error('boom') end)")
            .find("boom"),
        std::string::npos
    );
}

TEST_F(LuaHierarchy, DestroyRootCascades) {
    const EntityId root = IREntity::createEntity();
    std::vector<EntityId> tree;
    for (int i = 0; i < 3; ++i) {
        tree.push_back(IREntity::createEntity());
        IREntity::setParent(tree.back(), root);
    }
    for (int i = 0; i < 2; ++i) {
        tree.push_back(IREntity::createEntity());
        IREntity::setParent(tree.back(), tree.front());
    }
    const EntityId bystander = IREntity::createEntity();
    bind("root", root);
    ASSERT_EQ(m_entity_manager.getChildren(root).size(), 3u);

    run("IREntity.destroyTree(root)");

    EXPECT_EQ(m_entity_manager.getChildren(root).size(), 0u);
    EXPECT_FALSE(IREntity::entityExists(root));
    for (EntityId entity : tree) {
        EXPECT_FALSE(IREntity::entityExists(entity)) << entity;
    }
    EXPECT_TRUE(IREntity::entityExists(bystander));
}

TEST_F(LuaHierarchy, DeferredDestroyTreeCascadesOnDrain) {
    const EntityId root = IREntity::createEntity();
    const EntityId child = IREntity::createEntity();
    const EntityId grandchild = IREntity::createEntity();
    IREntity::setParent(child, root);
    IREntity::setParent(grandchild, child);
    bind("root", root);

    run("IREntity.deferredDestroyTree(root)");
    m_entity_manager.flushStructuralChanges();
    EXPECT_TRUE(IREntity::entityExists(grandchild));

    m_entity_manager.destroyMarkedEntities();
    EXPECT_FALSE(IREntity::entityExists(root));
    EXPECT_FALSE(IREntity::entityExists(child));
    EXPECT_FALSE(IREntity::entityExists(grandchild));
}

// The opt-out: detach first, then the tree verb destroys only the root.
TEST_F(LuaHierarchy, DestroyRootDetachesOnOptOut) {
    const EntityId root = IREntity::createEntity();
    std::vector<EntityId> children;
    for (int i = 0; i < 3; ++i) {
        children.push_back(IREntity::createEntity());
        IREntity::setParent(children.back(), root);
    }
    bind("root", root);

    run(R"lua(
        IREntity.detachChildren(root)
        IREntity.destroyTree(root)
    )lua");

    EXPECT_FALSE(IREntity::entityExists(root));
    for (EntityId child : children) {
        ASSERT_TRUE(IREntity::entityExists(child));
        EXPECT_EQ(m_entity_manager.getParent(child), IREntity::kNullEntity);
    }
}

TEST_F(LuaHierarchy, DetachChildrenLeavesRoots) {
    const EntityId root = IREntity::createEntity();
    std::vector<EntityId> children;
    for (int i = 0; i < 3; ++i) {
        children.push_back(IREntity::createEntity(TestPartMarker{}));
        IREntity::setParent(children.back(), root);
    }
    const EntityId grandchild = IREntity::createEntity();
    IREntity::setParent(grandchild, children.front());
    bind("root", root);

    run("IREntity.detachChildren(root)");

    EXPECT_TRUE(IREntity::entityExists(root));
    EXPECT_TRUE(m_entity_manager.getChildren(root).empty());
    for (EntityId child : children) {
        EXPECT_EQ(m_entity_manager.getParent(child), IREntity::kNullEntity);
    }
    EXPECT_EQ(m_entity_manager.getParent(grandchild), children.front());
}

// The pre-existing destroy verb keeps its non-cascading semantics.
TEST_F(LuaHierarchy, DestroyEntityDoesNotCascade) {
    const EntityId root = IREntity::createEntity();
    std::vector<EntityId> children;
    for (int i = 0; i < 3; ++i) {
        children.push_back(IREntity::createEntity());
        IREntity::setParent(children.back(), root);
    }
    bind("root", root);

    run("IREntity.deferredDestroy(root)");
    m_entity_manager.destroyMarkedEntities();

    EXPECT_FALSE(IREntity::entityExists(root));
    for (EntityId child : children) {
        EXPECT_TRUE(IREntity::entityExists(child));
    }
}

TEST_F(LuaHierarchy, ChildFollowsParentTransform) {
    const IRMath::vec4 yaw90 = IRMath::quatAxisAngle(IRMath::vec3(0, 0, 1), IRMath::kHalfPi);
    C_LocalTransform parentLocal{IRMath::vec3(10.0f, 0.0f, 0.0f)};
    parentLocal.rotation_ = yaw90;
    const EntityId parent = IREntity::createEntity(parentLocal);
    const EntityId child = IREntity::createEntity(C_LocalTransform{IRMath::vec3(1.0f, 0.0f, 0.0f)});
    bind("parent", parent);
    bind("child", child);
    m_system_manager.registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>()}
    );

    run("IREntity.setParent(child, parent)");
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    const auto &world = IREntity::getComponent<C_WorldTransform>(child);
    EXPECT_NEAR(world.translation_.x, 10.0f, 1e-4f);
    EXPECT_NEAR(world.translation_.y, 1.0f, 1e-4f);
    EXPECT_NEAR(world.translation_.z, 0.0f, 1e-4f);
    const IRMath::vec3 rotatedX =
        IRMath::rotateVectorByQuat(IRMath::vec3(1, 0, 0), world.rotation_);
    EXPECT_NEAR(rotatedX.x, 0.0f, 1e-4f);
    EXPECT_NEAR(rotatedX.y, 1.0f, 1e-4f);
}

} // namespace

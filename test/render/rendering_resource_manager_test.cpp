// The resource id pool: `destroy` hands the id back, `create` reuses it FIFO,
// and an exhausted pool fails loudly instead of reading an empty queue.
//
// GL-free by construction: `ShaderStage` only stores its path and type, so the
// create/destroy cycle runs without a context or backend on every host. Each
// manager is built with a small id capacity so the pool can be drained and its
// reuse order observed within a handful of creates.

#include <gtest/gtest.h>

#include <irreden/ir_render.hpp>

#include <irreden/render/shader.hpp>

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace {

IRRender::ResourceId createStage(IRRender::RenderingResourceManager &manager) {
    return manager.create<IRRender::ShaderStage>("unused.glsl", IRRender::ShaderType::COMPUTE)
        .first;
}

} // namespace

TEST(RenderingResourceManager, DestroyReturnsIdToPool) {
    constexpr IRRender::ResourceId kCapacity = 4;
    IRRender::RenderingResourceManager manager{kCapacity};
    ASSERT_EQ(manager.freeIdCount(), kCapacity);
    ASSERT_EQ(manager.liveResourceCount(), 0);

    std::vector<IRRender::ResourceId> firstBatch;
    for (IRRender::ResourceId i = 0; i < kCapacity; ++i) {
        firstBatch.push_back(createStage(manager));
    }
    EXPECT_EQ(manager.freeIdCount(), 0u);
    EXPECT_EQ(manager.liveResourceCount(), static_cast<int>(kCapacity));

    // Destroyed newest-first, so the expected reuse order is the reverse of
    // the creation order: the pool is a queue, not a stack.
    std::vector<IRRender::ResourceId> destroyOrder(firstBatch.rbegin(), firstBatch.rend());
    for (IRRender::ResourceId id : destroyOrder) {
        manager.destroy<IRRender::ShaderStage>(id);
    }
    EXPECT_EQ(manager.freeIdCount(), kCapacity);
    EXPECT_EQ(manager.liveResourceCount(), 0);

    std::vector<IRRender::ResourceId> secondBatch;
    for (IRRender::ResourceId i = 0; i < kCapacity; ++i) {
        secondBatch.push_back(createStage(manager));
    }
    EXPECT_EQ(secondBatch, destroyOrder);
    EXPECT_EQ(manager.freeIdCount(), 0u);
    EXPECT_EQ(manager.liveResourceCount(), static_cast<int>(kCapacity));
}

TEST(RenderingResourceManager, RepeatedCyclesKeepThePoolSizeConstant) {
    constexpr IRRender::ResourceId kCapacity = 3;
    IRRender::RenderingResourceManager manager{kCapacity};

    for (int cycle = 0; cycle < 10 * static_cast<int>(kCapacity); ++cycle) {
        const IRRender::ResourceId id = createStage(manager);
        manager.destroy<IRRender::ShaderStage>(id);
        ASSERT_EQ(manager.freeIdCount(), kCapacity) << "cycle " << cycle;
        ASSERT_EQ(manager.liveResourceCount(), 0) << "cycle " << cycle;
    }
}

// An id pushed twice would be handed to two live resources at once.
TEST(RenderingResourceManager, DoubleDestroyDoesNotDuplicateTheId) {
    constexpr IRRender::ResourceId kCapacity = 2;
    IRRender::RenderingResourceManager manager{kCapacity};

    const IRRender::ResourceId id = createStage(manager);
    manager.destroy<IRRender::ShaderStage>(id);
    manager.destroy<IRRender::ShaderStage>(id);

    EXPECT_EQ(manager.freeIdCount(), kCapacity);
    EXPECT_EQ(manager.liveResourceCount(), 0);
    EXPECT_NE(createStage(manager), createStage(manager));
}

// Ids are unique across types, so destroying one as a type it never was must
// leave the real resource and the pool untouched.
TEST(RenderingResourceManager, DestroyUnderTheWrongTypeIsRejected) {
    constexpr IRRender::ResourceId kCapacity = 2;
    IRRender::RenderingResourceManager manager{kCapacity};

    const auto [id, stage] =
        manager.create<IRRender::ShaderStage>("unused.glsl", IRRender::ShaderType::COMPUTE);
    manager.destroy<IRRender::Buffer>(id);

    EXPECT_EQ(manager.freeIdCount(), kCapacity - 1);
    EXPECT_EQ(manager.liveResourceCount(), 1);
    EXPECT_EQ(manager.get<IRRender::ShaderStage>(id), stage);
}

// With id reuse, a name surviving its resource would resolve to whichever
// resource next takes the id.
TEST(RenderingResourceManager, DestroyDropsTheResourceName) {
    constexpr const char *kName = "IR_TEST_PooledStage";
    IRRender::RenderingResourceManager manager{1};

    const IRRender::ResourceId named =
        manager
            .createNamed<IRRender::ShaderStage>(kName, "unused.glsl", IRRender::ShaderType::COMPUTE)
            .first;
    ASSERT_NE(manager.getNamedOrNull<IRRender::ShaderStage>(kName), nullptr);

    manager.destroy<IRRender::ShaderStage>(named);
    EXPECT_EQ(manager.getNamedOrNull<IRRender::ShaderStage>(kName), nullptr);

    // Capacity one forces the unnamed successor onto the same id.
    ASSERT_EQ(createStage(manager), named);
    EXPECT_EQ(manager.getNamedOrNull<IRRender::ShaderStage>(kName), nullptr);
}

#ifndef IR_RELEASE
TEST(RenderingResourceManager, CreateOnEmptyPoolIsLoud) {
    constexpr IRRender::ResourceId kCapacity = 2;
    IRRender::RenderingResourceManager manager{kCapacity};
    createStage(manager);
    createStage(manager);
    ASSERT_EQ(manager.freeIdCount(), 0u);

    EXPECT_THROW((void)createStage(manager), std::runtime_error);

    EXPECT_EQ(manager.freeIdCount(), 0u);
    EXPECT_EQ(manager.liveResourceCount(), static_cast<int>(kCapacity));
}
#endif

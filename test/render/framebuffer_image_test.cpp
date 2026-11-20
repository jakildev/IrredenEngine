#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>

#include <irreden/common/components/component_size_triangles.hpp>
#include <irreden/render/canvas_draw.hpp>
#include <irreden/render/components/component_canvas_draw_queue.hpp>
#include <irreden/render/components/component_framebuffer_image_queue.hpp>
#include <irreden/render/image_data.hpp>
#include <irreden/render/ir_render_types.hpp>

#include <cstddef>
#include <string>
#include <vector>

// `IRPrefab::CanvasDraw::queueFramebufferImage` with a loader that uploads
// nothing: every draw lands in the `C_FramebufferImageQueue` singleton exactly
// as `IMAGE_TO_FRAMEBUFFER` would flush it.

namespace {

namespace Draw = IRPrefab::CanvasDraw;

using IRComponents::C_FramebufferImageQueue;
using IRComponents::C_SizeTriangles;
using IRMath::Color;
using IRMath::ivec2;
using IRMath::vec4;

// A 300 x 200 source held at 150 x 100, no texture.
bool probeLoader(const std::string &, Draw::ImageSource &source, std::string &) {
    source.sourceSize_ = ivec2(300, 200);
    source.baseSize_ = ivec2(150, 100);
    source.levelCount_ = IRRender::mipLevelCount(source.baseSize_);
    return true;
}

class FramebufferImageTest : public testing::Test {
  protected:
    void TearDown() override {
        m_entityManager.destroyAllEntities();
    }

    Draw::ImageHandle load(const std::string &path) {
        std::string error;
        return Draw::loadImage(path, error, probeLoader);
    }

    const std::vector<C_FramebufferImageQueue::Draw> &draws() {
        return Draw::framebufferImageQueue().draws_;
    }

    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
};

TEST_F(FramebufferImageTest, QueuedImageCarriesItsBoxAtlasAndDepth) {
    const Draw::ImageHandle handle = load("a.png");
    Draw::queueFramebufferImage(handle, ivec2(-10, 20), ivec2(60, 40), {}, Draw::kDepthBehind);
    ASSERT_EQ(draws().size(), 1u);
    EXPECT_EQ(draws()[0].handle_, handle);
    const IRRender::CanvasImageCommand &command = draws()[0].command_;
    EXPECT_EQ(command.origin_, ivec2(-10, 20));
    EXPECT_EQ(command.size_, ivec2(60, 40));
    // The queued clip is the whole box; the flush cuts it to the framebuffer.
    EXPECT_EQ(command.clipLo_, ivec2(-10, 20));
    EXPECT_EQ(command.clipHi_, ivec2(50, 60));
    EXPECT_EQ(command.uvRect_, vec4(0.0f, 0.0f, 1.0f, 1.0f));
    EXPECT_EQ(command.baseSize_, ivec2(150, 100));
    EXPECT_EQ(command.levelCount_, 8);
    EXPECT_EQ(command.cell_, 1);
    EXPECT_EQ(command.tintPacked_, 0xFFFFFFFFu);
    EXPECT_EQ(command.posterizeLevels_, 0);
    EXPECT_EQ(command.distance_, Draw::kDepthBehind);
}

TEST_F(FramebufferImageTest, OptionsPackAsTheyDoForACanvasImage) {
    const Draw::ImageHandle handle = load("a.png");
    const IREntity::EntityId canvas = IREntity::createEntity(C_SizeTriangles{ivec2(400, 300)});
    const Draw::ImageOptions options{
        .uvRect_ = vec4(-1.0f, 0.25f, 0.5f, 0.75f),
        .cell_ = 1 << 20,
        .tint_ = Color{10, 20, 30, 255},
        .levels_ = 500,
    };
    Draw::queueImage(canvas, handle, ivec2(8, 6), ivec2(120, 90), options, Draw::kDepthWorldFar);
    Draw::queueFramebufferImage(handle, ivec2(8, 6), ivec2(120, 90), options, Draw::kDepthWorldFar);
    ASSERT_EQ(Draw::queue().batches_.size(), 1u);
    ASSERT_EQ(Draw::queue().batches_[0].images_.size(), 1u);
    ASSERT_EQ(draws().size(), 1u);
    const IRRender::CanvasImageCommand &onCanvas = Draw::queue().batches_[0].images_[0].command_;
    const IRRender::CanvasImageCommand &onFramebuffer = draws()[0].command_;
    EXPECT_EQ(onFramebuffer.origin_, onCanvas.origin_);
    EXPECT_EQ(onFramebuffer.size_, onCanvas.size_);
    EXPECT_EQ(onFramebuffer.uvRect_, onCanvas.uvRect_);
    EXPECT_EQ(onFramebuffer.uvRect_, vec4(0.0f, 0.25f, 0.5f, 0.75f));
    EXPECT_EQ(onFramebuffer.cell_, Draw::kMaxImageCell);
    EXPECT_EQ(onFramebuffer.tintPacked_, onCanvas.tintPacked_);
    EXPECT_EQ(onFramebuffer.posterizeLevels_, Draw::kMaxImagePosterizeLevels);
    EXPECT_EQ(onFramebuffer.distance_, onCanvas.distance_);
    EXPECT_EQ(onFramebuffer.unrotate_, onCanvas.unrotate_);
}

TEST_F(FramebufferImageTest, ARotationTurnsEachPathInItsOwnTexels) {
    const Draw::ImageHandle handle = load("a.png");
    const IREntity::EntityId canvas = IREntity::createEntity(C_SizeTriangles{ivec2(400, 300)});
    const Draw::ImageOptions options{.rotation_ = 0.3f};
    Draw::queueImage(canvas, handle, ivec2(80, 60), ivec2(120, 90), options, Draw::kDepthFront);
    Draw::queueFramebufferImage(handle, ivec2(80, 60), ivec2(120, 90), options, Draw::kDepthFront);
    const IRRender::CanvasImageCommand &onCanvas = Draw::queue().batches_[0].images_[0].command_;
    const IRRender::CanvasImageCommand &onFramebuffer = draws()[0].command_;
    EXPECT_EQ(onCanvas.unrotate_, Draw::imageUnrotation(0.3f, Draw::kCanvasTexelPixels));
    EXPECT_EQ(onFramebuffer.unrotate_, Draw::imageUnrotation(0.3f, Draw::kFramebufferTexelPixels));
    // One framebuffer pixel is square, so its matrix is the plain rotation.
    EXPECT_FLOAT_EQ(onFramebuffer.unrotate_.x, IRMath::cos(0.3f));
    EXPECT_FLOAT_EQ(onFramebuffer.unrotate_.y, IRMath::sin(0.3f));
    EXPECT_FLOAT_EQ(onFramebuffer.unrotate_.z, -IRMath::sin(0.3f));
    EXPECT_FLOAT_EQ(onFramebuffer.unrotate_.w, IRMath::cos(0.3f));
    // The turn writes the distance an unturned draw writes.
    EXPECT_EQ(onFramebuffer.distance_, Draw::kDepthFront);
    EXPECT_EQ(onCanvas.distance_, Draw::kDepthFront);
}

TEST_F(FramebufferImageTest, ATurnedImageQueuesItsBoundsAndTheFlushCutsThem) {
    const Draw::ImageHandle handle = load("a.png");
    const ivec2 target(644, 362);
    // 100 x 40 pixels about (60, 40); a quarter turn makes it 40 x 100.
    Draw::queueFramebufferImage(
        handle,
        ivec2(10, 20),
        ivec2(100, 40),
        {.rotation_ = IRMath::kHalfPi},
        Draw::kDepthFront
    );
    IRRender::CanvasImageCommand command = draws()[0].command_;
    EXPECT_NEAR(command.clipLo_.x, 40, 1);
    EXPECT_NEAR(command.clipLo_.y, -10, 1);
    EXPECT_NEAR(command.clipHi_.x, 80, 1);
    EXPECT_NEAR(command.clipHi_.y, 90, 1);
    ASSERT_TRUE(Draw::clipFramebufferImage(command, target));
    EXPECT_NEAR(command.clipLo_.x, 40, 1);
    EXPECT_EQ(command.clipLo_.y, 0);
    EXPECT_NEAR(command.clipHi_.x, 80, 1);
    EXPECT_NEAR(command.clipHi_.y, 90, 1);
    EXPECT_EQ(command.origin_, ivec2(10, 20));
    EXPECT_EQ(command.size_, ivec2(100, 40));

    // Every pixel the turned box covers on the target is inside the clip.
    for (int y = 0; y < target.y; ++y) {
        for (int x = 0; x < 200; ++x) {
            if (Draw::imageCovers(command, ivec2(x, y))) {
                ASSERT_TRUE(
                    x >= command.clipLo_.x && x < command.clipHi_.x && y >= command.clipLo_.y &&
                    y < command.clipHi_.y
                ) << x
                  << "," << y;
            }
        }
    }
}

TEST_F(FramebufferImageTest, ClipFollowsTheTurnAcrossTheTargetEdge) {
    const ivec2 target(644, 362);
    // A 300 x 20 strip whose left end is on the target. Turned 67.5 degrees
    // about its centre, (790, 180), it reaches 66.6 pixels to either side and
    // 142.4 up and down, so all of it is past the right edge.
    IRRender::CanvasImageCommand strip;
    strip.origin_ = ivec2(640, 170);
    strip.size_ = ivec2(300, 20);
    ASSERT_TRUE(Draw::clipFramebufferImage(strip, target));
    strip.unrotate_ = Draw::imageUnrotation(0.75f * IRMath::kHalfPi, Draw::kFramebufferTexelPixels);
    EXPECT_FALSE(Draw::clipFramebufferImage(strip, target));
    // 100 pixels further left the same turn leaves its near corner on.
    strip.origin_.x = 540;
    ASSERT_TRUE(Draw::clipFramebufferImage(strip, target));
    EXPECT_NEAR(strip.clipLo_.x, 623, 1);
    EXPECT_EQ(strip.clipHi_.x, target.x);
    EXPECT_NEAR(strip.clipLo_.y, 37, 1);
    EXPECT_NEAR(strip.clipHi_.y, 323, 1);

    // A strip along the top edge turned a quarter leaves the target sideways
    // but still crosses it top to bottom.
    IRRender::CanvasImageCommand tall;
    tall.origin_ = ivec2(100, 0);
    tall.size_ = ivec2(800, 10);
    tall.unrotate_ = Draw::imageUnrotation(IRMath::kHalfPi, Draw::kFramebufferTexelPixels);
    ASSERT_TRUE(Draw::clipFramebufferImage(tall, target));
    EXPECT_NEAR(tall.clipLo_.x, 495, 1);
    EXPECT_NEAR(tall.clipHi_.x, 505, 1);
    EXPECT_EQ(tall.clipLo_.y, 0);
    EXPECT_EQ(tall.clipHi_.y, target.y);
}

TEST_F(FramebufferImageTest, DrawsKeepTheirCallOrder) {
    const Draw::ImageHandle first = load("a.png");
    const Draw::ImageHandle second = load("b.png");
    Draw::queueFramebufferImage(second, ivec2(0), ivec2(10), {}, Draw::kDepthFront);
    Draw::queueFramebufferImage(first, ivec2(5), ivec2(10), {}, Draw::kDepthBehind);
    Draw::queueFramebufferImage(second, ivec2(9), ivec2(10), {}, Draw::kDepthFront);
    ASSERT_EQ(draws().size(), 3u);
    EXPECT_EQ(draws()[0].handle_, second);
    EXPECT_EQ(draws()[1].handle_, first);
    EXPECT_EQ(draws()[2].handle_, second);
    EXPECT_EQ(draws()[1].command_.distance_, Draw::kDepthBehind);
    EXPECT_EQ(draws()[2].command_.origin_, ivec2(9));
    // A canvas batch is a different queue.
    EXPECT_TRUE(Draw::queue().batches_.empty());
}

TEST_F(FramebufferImageTest, UnknownHandlesAndEmptyBoxesQueueNothing) {
    const Draw::ImageHandle handle = load("a.png");
    Draw::queueFramebufferImage(handle + 1, ivec2(0), ivec2(10), {}, Draw::kDepthFront);
    Draw::queueFramebufferImage(Draw::kInvalidImage, ivec2(0), ivec2(10), {}, Draw::kDepthFront);
    Draw::queueFramebufferImage(handle, ivec2(0), ivec2(10, 0), {}, Draw::kDepthFront);
    Draw::queueFramebufferImage(handle, ivec2(0), ivec2(-4, 10), {}, Draw::kDepthFront);
    Draw::releaseImage(handle);
    Draw::queueFramebufferImage(handle, ivec2(0), ivec2(10), {}, Draw::kDepthFront);
    EXPECT_TRUE(draws().empty());
}

TEST_F(FramebufferImageTest, TheQueueHoldsAtMostItsCapacity) {
    const Draw::ImageHandle handle = load("a.png");
    const int capacity = static_cast<int>(C_FramebufferImageQueue::kMaxDraws);
    for (int i = 0; i < capacity + 40; ++i) {
        Draw::queueFramebufferImage(handle, ivec2(i, 0), ivec2(10), {}, Draw::kDepthFront);
    }
    ASSERT_EQ(draws().size(), C_FramebufferImageQueue::kMaxDraws);
    // The draws past the capacity are the ones dropped.
    EXPECT_EQ(draws().front().command_.origin_, ivec2(0, 0));
    EXPECT_EQ(draws().back().command_.origin_, ivec2(capacity - 1, 0));
}

TEST_F(FramebufferImageTest, ClipCutsTheDrawToTheTargetButKeepsItsBox) {
    const Draw::ImageHandle handle = load("a.png");
    const ivec2 target(644, 362);
    Draw::queueFramebufferImage(handle, ivec2(-20, 300), ivec2(100, 100), {}, Draw::kDepthFront);
    IRRender::CanvasImageCommand command = draws()[0].command_;
    ASSERT_TRUE(Draw::clipFramebufferImage(command, target));
    EXPECT_EQ(command.clipLo_, ivec2(0, 300));
    EXPECT_EQ(command.clipHi_, ivec2(80, 362));
    EXPECT_EQ(command.origin_, ivec2(-20, 300));
    EXPECT_EQ(command.size_, ivec2(100, 100));

    // A box inside the target is its own clip.
    IRRender::CanvasImageCommand inside;
    inside.origin_ = ivec2(10, 12);
    inside.size_ = ivec2(30, 40);
    ASSERT_TRUE(Draw::clipFramebufferImage(inside, target));
    EXPECT_EQ(inside.clipLo_, ivec2(10, 12));
    EXPECT_EQ(inside.clipHi_, ivec2(40, 52));

    // A box covering the target clips to all of it.
    IRRender::CanvasImageCommand covering;
    covering.origin_ = ivec2(-100, -100);
    covering.size_ = ivec2(2000, 2000);
    ASSERT_TRUE(Draw::clipFramebufferImage(covering, target));
    EXPECT_EQ(covering.clipLo_, ivec2(0, 0));
    EXPECT_EQ(covering.clipHi_, target);
}

TEST_F(FramebufferImageTest, ClipRejectsABoxOffTheTarget) {
    const ivec2 target(644, 362);
    const ivec2 origins[] = {ivec2(644, 0), ivec2(0, 362), ivec2(-50, 0), ivec2(0, -50)};
    for (const ivec2 origin : origins) {
        IRRender::CanvasImageCommand command;
        command.origin_ = origin;
        command.size_ = ivec2(50, 50);
        EXPECT_FALSE(Draw::clipFramebufferImage(command, target)) << origin.x << "," << origin.y;
    }
}

} // namespace

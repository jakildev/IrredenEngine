#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_size_triangles.hpp>
#include <irreden/render/canvas_draw.hpp>
#include <irreden/render/components/component_framebuffer_image_queue.hpp>
#include <irreden/render/image_data.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/script/lua_canvas_draw_bindings.hpp>
#include <irreden/script/lua_script.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// `IRCanvas.framebufferImage` and `IRCanvas.framebufferSize` through the sol2
// seam. The injected loader decodes the file and builds its mip atlas but
// uploads nothing, and the framebuffer size is injected, so the suite runs
// without a RenderManager or a GPU; every draw lands in the
// `C_FramebufferImageQueue` singleton exactly as `IMAGE_TO_FRAMEBUFFER` would
// flush it.

namespace {

namespace fs = std::filesystem;
namespace Draw = IRPrefab::CanvasDraw;

using IRComponents::C_FramebufferImageQueue;
using IRComponents::C_SizeTriangles;
using IRMath::ivec2;
using IRMath::vec4;

constexpr ivec2 kFramebufferSize(644, 362);
constexpr ivec2 kProbeSize(6, 4);

// A fresh `<tmp>/<name>` image base holding `probe.png`.
struct FixtureTree {
    explicit FixtureTree(const char *name)
        : base_{fs::weakly_canonical(fs::path(testing::TempDir()) / name)} {
        fs::remove_all(base_);
        fs::create_directories(base_);
        const std::vector<std::uint8_t> rgba(
            static_cast<std::size_t>(kProbeSize.x) * kProbeSize.y * 4,
            200
        );
        IRRender::writePNG(
            (base_ / "probe.png").string().c_str(),
            kProbeSize.x,
            kProbeSize.y,
            4,
            rgba.data()
        );
    }
    ~FixtureTree() {
        std::error_code ignored;
        fs::remove_all(base_, ignored);
    }

    fs::path base_;
};

// m_lua is declared FIRST so it is destroyed LAST (test/CLAUDE.md).
class LuaFramebufferImageTest : public testing::Test {
  protected:
    LuaFramebufferImageTest()
        : m_tree{"lua_framebuffer_image"} {
        m_canvas = IREntity::createEntity(C_SizeTriangles{ivec2(322, 362)});
        IRScript::detail::bindCanvasDraw(
            m_lua,
            m_tree.base_,
            [this](const std::string &name) -> IREntity::EntityId {
                return name == "layer" ? m_canvas : IREntity::kNullEntity;
            },
            [](const std::string &) -> IREntity::EntityId { return IREntity::kNullEntity; },
            [](const std::string &path, std::string &error) {
                return Draw::loadImage(
                    path,
                    error,
                    [](const std::string &file, Draw::ImageSource &source, std::string &why) {
                        const IRRender::ImageData decoded{file.c_str(), why};
                        if (decoded.data_ == nullptr) {
                            return false;
                        }
                        source.sourceSize_ = ivec2(decoded.width_, decoded.height_);
                        const IRRender::MipAtlas atlas = IRRender::buildMipAtlas(
                            decoded.data_,
                            source.sourceSize_,
                            Draw::kMaxImageDimension
                        );
                        source.baseSize_ = atlas.baseSize_;
                        source.levelCount_ = atlas.levelCount_;
                        return true;
                    }
                );
            },
            []() { return kFramebufferSize; }
        );
    }

    bool scriptSucceeds(const std::string &source) {
        sol::protected_function_result result =
            m_lua.lua().safe_script(source, sol::script_pass_on_error);
        if (!result.valid()) {
            ADD_FAILURE() << sol::error(result).what();
        }
        return result.valid();
    }

    std::string scriptError(const std::string &source) {
        sol::protected_function_result result =
            m_lua.lua().safe_script(source, sol::script_pass_on_error);
        EXPECT_FALSE(result.valid());
        if (result.valid()) {
            return {};
        }
        return sol::error(result).what();
    }

    const std::vector<C_FramebufferImageQueue::Draw> &draws() {
        return Draw::framebufferImageQueue().draws_;
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
    FixtureTree m_tree;
    IREntity::EntityId m_canvas = IREntity::kNullEntity;
};

TEST_F(LuaFramebufferImageTest, ExposesTheFramebufferSurface) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        for _, name in ipairs({ 'framebufferImage', 'framebufferSize' }) do
            assert(type(IRCanvas[name]) == 'function', name)
        end
    )lua"));
}

TEST_F(LuaFramebufferImageTest, FramebufferSizeIsTheMainFramebuffersPixels) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        local w, h = IRCanvas.framebufferSize()
        assert(w == 644 and h == 362, tostring(w) .. 'x' .. tostring(h))
    )lua"));
}

TEST_F(LuaFramebufferImageTest, ImageQueuesOneDrawCarryingItsOptions) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        handle = IRCanvas.loadImage('probe.png')
        IRCanvas.framebufferImage(handle, 10, 20, 100, 60, {
            uv = { 0.25, 0.5, 0.75, 1 },
            cell = 4,
            tint = { 10, 20, 30 },
            levels = 6,
            distance = IRCanvas.Depth.BEHIND,
        })
    )lua"));
    ASSERT_EQ(draws().size(), 1u);
    EXPECT_EQ(draws()[0].handle_, static_cast<Draw::ImageHandle>(m_lua.lua()["handle"].get<int>()));
    const IRRender::CanvasImageCommand &command = draws()[0].command_;
    EXPECT_EQ(command.origin_, ivec2(10, 20));
    EXPECT_EQ(command.size_, ivec2(100, 60));
    EXPECT_EQ(command.clipLo_, ivec2(10, 20));
    EXPECT_EQ(command.clipHi_, ivec2(110, 80));
    EXPECT_EQ(command.uvRect_, vec4(0.25f, 0.5f, 0.75f, 1.0f));
    EXPECT_EQ(command.baseSize_, kProbeSize);
    EXPECT_EQ(command.levelCount_, IRRender::mipLevelCount(kProbeSize));
    EXPECT_EQ(command.cell_, 4);
    EXPECT_EQ(command.tintPacked_, IRMath::Color(10, 20, 30, 255).toPackedRGBA());
    EXPECT_EQ(command.posterizeLevels_, 6);
    EXPECT_EQ(command.distance_, Draw::kDepthBehind);
    // Nothing joins a canvas batch.
    EXPECT_TRUE(Draw::queue().batches_.empty());
}

TEST_F(LuaFramebufferImageTest, ImageDefaultsToTheWholeImageUntouchedAtTheFrontDepth) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        local w, h = IRCanvas.framebufferSize()
        IRCanvas.framebufferImage(IRCanvas.loadImage('probe.png'), 0, 0, w, h)
    )lua"));
    ASSERT_EQ(draws().size(), 1u);
    const IRRender::CanvasImageCommand &command = draws()[0].command_;
    EXPECT_EQ(command.origin_, ivec2(0));
    EXPECT_EQ(command.size_, kFramebufferSize);
    EXPECT_EQ(command.uvRect_, vec4(0.0f, 0.0f, 1.0f, 1.0f));
    EXPECT_EQ(command.cell_, 1);
    EXPECT_EQ(command.tintPacked_, 0xFFFFFFFFu);
    EXPECT_EQ(command.posterizeLevels_, 0);
    EXPECT_EQ(command.distance_, Draw::kDepthFront);
    EXPECT_EQ(command.unrotate_, vec4(1.0f, 0.0f, 0.0f, 1.0f));
}

TEST_F(LuaFramebufferImageTest, RotationTurnsTheBoxInFramebufferPixels) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        local handle = IRCanvas.loadImage('probe.png')
        IRCanvas.framebufferImage(handle, 10, 20, 100, 40, { rotation = math.pi / 2 })
        IRCanvas.framebufferImage(handle, 10, 20, 100, 40, { rotation = -0.25, cell = 4 })
        IRCanvas.framebufferImage(handle, 10, 20, 100, 40, { rotation = 0 })
    )lua"));
    ASSERT_EQ(draws().size(), 3u);
    const IRRender::CanvasImageCommand &quarter = draws()[0].command_;
    EXPECT_EQ(quarter.origin_, ivec2(10, 20));
    EXPECT_EQ(quarter.size_, ivec2(100, 40));
    EXPECT_EQ(
        quarter.unrotate_,
        Draw::imageUnrotation(IRMath::kHalfPi, Draw::kFramebufferTexelPixels)
    );
    // 100 x 40 about (60, 40) turned a quarter is 40 x 100.
    EXPECT_NEAR(quarter.clipLo_.x, 40, 1);
    EXPECT_NEAR(quarter.clipLo_.y, -10, 1);
    EXPECT_NEAR(quarter.clipHi_.x, 80, 1);
    EXPECT_NEAR(quarter.clipHi_.y, 90, 1);
    EXPECT_EQ(
        draws()[1].command_.unrotate_,
        Draw::imageUnrotation(-0.25f, Draw::kFramebufferTexelPixels)
    );
    EXPECT_EQ(draws()[1].command_.cell_, 4);
    EXPECT_EQ(draws()[2].command_.unrotate_, vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(draws()[2].command_.clipLo_, ivec2(10, 20));
    EXPECT_EQ(draws()[2].command_.clipHi_, ivec2(110, 60));
}

TEST_F(LuaFramebufferImageTest, DrawsKeepCallOrderBesideCanvasDraws) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        local handle = IRCanvas.loadImage('probe.png')
        IRCanvas.framebufferImage(handle, 0, 0, 100, 100, { distance = IRCanvas.Depth.BEHIND })
        IRCanvas.image('layer', handle, 0, 0, 50, 100)
        IRCanvas.framebufferImage(handle, 40, 40, 100, 100)
        IRCanvas.framebufferImage(handle, 700, 0, 50, 50)
        IRCanvas.framebufferImage(handle, 0, 0, 0, 10)
    )lua"));
    // A box off the framebuffer stays queued for the flush to cut; an empty
    // one is dropped at the call.
    ASSERT_EQ(draws().size(), 3u);
    EXPECT_EQ(draws()[0].command_.distance_, Draw::kDepthBehind);
    EXPECT_EQ(draws()[1].command_.origin_, ivec2(40, 40));
    EXPECT_EQ(draws()[2].command_.origin_, ivec2(700, 0));
    ASSERT_EQ(Draw::queue().batches_.size(), 1u);
    EXPECT_EQ(Draw::queue().batches_[0].images_.size(), 1u);
}

TEST_F(LuaFramebufferImageTest, AReleasedHandleIsRejected) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        handle = IRCanvas.loadImage('probe.png')
        IRCanvas.releaseImage(handle)
    )lua"));
    EXPECT_NE(
        scriptError("IRCanvas.framebufferImage(handle, 0, 0, 8, 8)").find("loaded image"),
        std::string::npos
    );
    EXPECT_TRUE(draws().empty());
}

TEST_F(LuaFramebufferImageTest, RejectsBadHandlesKeysAndRanges) {
    EXPECT_TRUE(scriptSucceeds("handle = IRCanvas.loadImage('probe.png')"));
    const auto rejects = [this](const std::string &call, const char *reason) {
        EXPECT_NE(scriptError(call).find(reason), std::string::npos) << call;
    };
    rejects("IRCanvas.framebufferImage(nil, 0, 0, 8, 8)", "loaded image");
    rejects("IRCanvas.framebufferImage(handle + 100, 0, 0, 8, 8)", "loaded image");
    rejects("IRCanvas.framebufferImage('probe.png', 0, 0, 8, 8)", "loaded image");
    rejects("IRCanvas.framebufferImage(handle, 0, 0, 8, 8, { crop = {} })", "unknown option");
    rejects("IRCanvas.framebufferImage(handle, 0, 0, 8, 8, 5)", "options must be a table");
    rejects("IRCanvas.framebufferImage(handle, 0, 0, 8, 8, { uv = 1 })", "uv must be");
    rejects("IRCanvas.framebufferImage(handle, 0, 0, 8, 8, { uv = { 0, 0, 1 } })", "uv must be");
    rejects(
        "IRCanvas.framebufferImage(handle, 0, 0, 8, 8, { uv = { 0, 0, 1, 1.5 } })",
        "uv must be"
    );
    rejects("IRCanvas.framebufferImage(handle, 0, 0, 8, 8, { cell = 0 })", "cell");
    rejects("IRCanvas.framebufferImage(handle, 0, 0, 8, 8, { cell = 1.5 })", "cell");
    rejects("IRCanvas.framebufferImage(handle, 0, 0, 8, 8, { levels = 1 })", "levels");
    rejects("IRCanvas.framebufferImage(handle, 0, 0, 8, 8, { levels = 65 })", "levels");
    rejects("IRCanvas.framebufferImage(handle, 0, 0, 8, 8, { distance = 70000 })", "distance");
    rejects("IRCanvas.framebufferImage(handle, 0, 0, 8, 8, { distance = 1.5 })", "distance");
    rejects("IRCanvas.framebufferImage(handle, 0, 0, 8, 8, { rotation = 'a' })", "rotation");
    rejects("IRCanvas.framebufferImage(handle, 0, 0, 8, 8, { rotation = { 1 } })", "rotation");
    rejects("IRCanvas.framebufferImage(handle, 0, 0, 8, 8, { rotation = 0 / 0 })", "rotation");
    rejects("IRCanvas.framebufferImage(handle, 0, 0, 8, 8, { rotation = math.huge })", "rotation");
    rejects("IRCanvas.framebufferImage(handle, 0, 0, 8, 8, { rotate = 1 })", "unknown option");
    EXPECT_TRUE(draws().empty());
}

} // namespace

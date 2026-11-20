#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_size_triangles.hpp>
#include <irreden/render/canvas_draw.hpp>
#include <irreden/render/components/component_canvas_draw_queue.hpp>
#include <irreden/render/image_data.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/script/lua_canvas_draw_bindings.hpp>
#include <irreden/script/lua_script.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

// The image half of `IRCanvas` through the sol2 seam. The injected loader
// decodes the file and builds its mip atlas but uploads nothing, so the suite
// runs without a RenderManager or a GPU; every draw lands in the
// `C_CanvasDrawQueue` singleton exactly as `CANVAS_DRAW` would flush it.

namespace {

namespace fs = std::filesystem;
namespace Draw = IRPrefab::CanvasDraw;

using IRComponents::C_CanvasDrawQueue;
using IRComponents::C_SizeTriangles;
using IRMath::ivec2;
using IRMath::vec4;

constexpr ivec2 kCanvasSize(272, 962);
constexpr ivec2 kProbeSize(6, 4);

void writeProbePng(const fs::path &path) {
    const std::vector<std::uint8_t> rgba(
        static_cast<std::size_t>(kProbeSize.x) * kProbeSize.y * 4,
        200
    );
    IRRender::writePNG(path.string().c_str(), kProbeSize.x, kProbeSize.y, 4, rgba.data());
}

// A fresh `<tmp>/<name>/base` image base holding `probe.png` and
// `other.png`, with `<tmp>/<name>/outside.png` beside it.
struct FixtureTree {
    explicit FixtureTree(const char *name)
        : root_{fs::weakly_canonical(fs::path(testing::TempDir()) / name)}
        , base_{root_ / "base"} {
        fs::remove_all(root_);
        fs::create_directories(base_);
        writeProbePng(base_ / "probe.png");
        writeProbePng(base_ / "other.png");
        writeProbePng(root_ / "outside.png");
    }
    ~FixtureTree() {
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }

    fs::path root_;
    fs::path base_;
};

// m_lua is declared FIRST so it is destroyed LAST (test/CLAUDE.md).
class LuaCanvasDrawImageTest : public testing::Test {
  protected:
    LuaCanvasDrawImageTest()
        : m_tree{"lua_canvas_draw_image"} {
        m_canvas = IREntity::createEntity(C_SizeTriangles{kCanvasSize});
        IRScript::detail::bindCanvasDraw(
            m_lua,
            m_tree.base_,
            [this](const std::string &name) -> IREntity::EntityId {
                return name == "layer" ? m_canvas : IREntity::kNullEntity;
            },
            [](const std::string &) -> IREntity::EntityId { return IREntity::kNullEntity; },
            [this](const std::string &path, std::string &error) {
                return Draw::loadImage(
                    path,
                    error,
                    [this](const std::string &file, Draw::ImageSource &source, std::string &why) {
                        m_decodedPaths.push_back(file);
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
            }
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

    const C_CanvasDrawQueue::Batch *batch() {
        for (const C_CanvasDrawQueue::Batch &queued : Draw::queue().batches_) {
            if (queued.canvas_ == m_canvas) {
                return &queued;
            }
        }
        return nullptr;
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
    FixtureTree m_tree;
    IREntity::EntityId m_canvas = IREntity::kNullEntity;
    std::vector<std::string> m_decodedPaths;
};

TEST_F(LuaCanvasDrawImageTest, ExposesTheImageSurface) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        for _, name in ipairs({ 'loadImage', 'imageSize', 'releaseImage', 'image' }) do
            assert(type(IRCanvas[name]) == 'function', name)
        end
    )lua"));
}

TEST_F(LuaCanvasDrawImageTest, LoadReturnsAHandleAndTheFileSize) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        local handle, err = IRCanvas.loadImage('probe.png')
        assert(type(handle) == 'number' and handle > 0, tostring(err))
        assert(err == nil)
        local w, h = IRCanvas.imageSize(handle)
        assert(w == 6 and h == 4)
    )lua"));
    ASSERT_EQ(m_decodedPaths.size(), 1u);
    EXPECT_EQ(fs::path(m_decodedPaths[0]), m_tree.base_ / "probe.png");
}

TEST_F(LuaCanvasDrawImageTest, TheSamePathLoadsOnceAndSharesItsHandle) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        first = IRCanvas.loadImage('probe.png')
        again = IRCanvas.loadImage('./probe.png')
        other = IRCanvas.loadImage('other.png')
        assert(first == again)
        assert(other ~= first)
    )lua"));
    EXPECT_EQ(m_decodedPaths.size(), 2u);

    // Two loads hold the image through one release.
    EXPECT_TRUE(scriptSucceeds(R"lua(
        IRCanvas.releaseImage(first)
        IRCanvas.image('layer', first, 0, 0, 10, 10)
        IRCanvas.releaseImage(first)
    )lua"));
    EXPECT_NE(
        scriptError("IRCanvas.image('layer', first, 0, 0, 10, 10)").find("loaded image"),
        std::string::npos
    );
    EXPECT_NE(scriptError("IRCanvas.imageSize(first)").find("loaded image"), std::string::npos);
    // A released handle is retired: a reload gets a new one, and releasing
    // the old one again is harmless.
    EXPECT_TRUE(scriptSucceeds(R"lua(
        IRCanvas.releaseImage(first)
        local reloaded = IRCanvas.loadImage('probe.png')
        assert(reloaded ~= first and reloaded ~= other)
    )lua"));
    EXPECT_EQ(m_decodedPaths.size(), 3u);
}

TEST_F(LuaCanvasDrawImageTest, AFailedLoadReturnsNilAndTheReason) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        local handle, err = IRCanvas.loadImage('missing.png')
        assert(handle == nil)
        assert(err:find('missing.png', 1, true), err)
    )lua"));
    EXPECT_EQ(m_decodedPaths.size(), 1u);
    EXPECT_TRUE(Draw::images().images_.empty());
}

TEST_F(LuaCanvasDrawImageTest, APathOutsideTheImageBaseNeverReachesTheDecoder) {
    const std::string absolute = (m_tree.root_ / "outside.png").generic_string();
    m_lua.lua()["absolutePath"] = absolute;
    EXPECT_TRUE(scriptSucceeds(R"lua(
        for _, path in ipairs({ '../outside.png', absolutePath, 'sub/../../outside.png' }) do
            local handle, err = IRCanvas.loadImage(path)
            assert(handle == nil, path)
            assert(err:find('image base', 1, true), err)
        end
    )lua"));
    EXPECT_TRUE(m_decodedPaths.empty());
}

TEST_F(LuaCanvasDrawImageTest, AGrantedAbsolutePathLoadsAndNothingBesideItDoes) {
    const fs::path outside = m_tree.root_ / "outside.png";
    writeProbePng(m_tree.root_ / "beside.png");
    m_lua.lua()["outside"] = outside.string();
    m_lua.lua()["beside"] = (m_tree.root_ / "beside.png").string();
    ASSERT_TRUE(m_lua.pathGrants().grantFile(outside));
    EXPECT_TRUE(scriptSucceeds(R"lua(
        local handle, err = IRCanvas.loadImage(outside)
        assert(type(handle) == 'number' and handle > 0, tostring(err))
        local refused, why = IRCanvas.loadImage(beside)
        assert(refused == nil)
        assert(why:find('image base', 1, true), why)
    )lua"));
    ASSERT_EQ(m_decodedPaths.size(), 1u);
    EXPECT_EQ(fs::path(m_decodedPaths[0]), outside);
}

TEST_F(LuaCanvasDrawImageTest, ImageQueuesOneCommandCarryingItsOptions) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        local handle = IRCanvas.loadImage('probe.png')
        IRCanvas.image('layer', handle, 10, 20, 100, 60, {
            uv = { 0.25, 0.5, 0.75, 1 },
            cell = 4,
            tint = { 10, 20, 30 },
            levels = 6,
            distance = IRCanvas.Depth.BEHIND,
        })
    )lua"));
    const C_CanvasDrawQueue::Batch *queued = batch();
    ASSERT_NE(queued, nullptr);
    EXPECT_TRUE(queued->commands_.empty());
    ASSERT_EQ(queued->images_.size(), 1u);
    ASSERT_EQ(queued->primitives_.size(), 1u);
    EXPECT_EQ(queued->primitives_[0].image_, 0);
    EXPECT_EQ(queued->primitives_[0].lo_, ivec2(10, 20));
    EXPECT_EQ(queued->primitives_[0].hi_, ivec2(110, 80));

    const IRRender::CanvasImageCommand &command = queued->images_[0].command_;
    EXPECT_EQ(command.clipLo_, ivec2(10, 20));
    EXPECT_EQ(command.clipHi_, ivec2(110, 80));
    EXPECT_EQ(command.origin_, ivec2(10, 20));
    EXPECT_EQ(command.size_, ivec2(100, 60));
    EXPECT_EQ(command.uvRect_, vec4(0.25f, 0.5f, 0.75f, 1.0f));
    EXPECT_EQ(command.baseSize_, kProbeSize);
    EXPECT_EQ(command.levelCount_, IRRender::mipLevelCount(kProbeSize));
    EXPECT_EQ(command.cell_, 4);
    EXPECT_EQ(command.tintPacked_, IRMath::Color(10, 20, 30, 255).toPackedRGBA());
    EXPECT_EQ(command.posterizeLevels_, 6);
    EXPECT_EQ(command.distance_, Draw::kDepthBehind);
}

TEST_F(LuaCanvasDrawImageTest, ImageDefaultsToTheWholeImageUntouchedAtTheFrontDepth) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        IRCanvas.image('layer', IRCanvas.loadImage('probe.png'), 0, 0, 8, 8)
    )lua"));
    const C_CanvasDrawQueue::Batch *queued = batch();
    ASSERT_NE(queued, nullptr);
    ASSERT_EQ(queued->images_.size(), 1u);
    const IRRender::CanvasImageCommand &command = queued->images_[0].command_;
    EXPECT_EQ(command.uvRect_, vec4(0.0f, 0.0f, 1.0f, 1.0f));
    EXPECT_EQ(command.cell_, 1);
    EXPECT_EQ(command.tintPacked_, 0xFFFFFFFFu);
    EXPECT_EQ(command.posterizeLevels_, 0);
    EXPECT_EQ(command.distance_, Draw::kDepthFront);
    EXPECT_EQ(command.unrotate_, vec4(1.0f, 0.0f, 0.0f, 1.0f));
}

TEST_F(LuaCanvasDrawImageTest, RotationTurnsTheBoxRigidlyOnScreen) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        local handle = IRCanvas.loadImage('probe.png')
        IRCanvas.image('layer', handle, 100, 400, 60, 40, { rotation = math.pi / 2 })
        IRCanvas.image('layer', handle, 100, 600, 60, 40, { rotation = 0.2, cell = 3 })
        IRCanvas.image('layer', handle, 100, 800, 60, 40, { rotation = 0 })
    )lua"));
    const C_CanvasDrawQueue::Batch *queued = batch();
    ASSERT_NE(queued, nullptr);
    ASSERT_EQ(queued->images_.size(), 3u);
    ASSERT_EQ(queued->primitives_.size(), 3u);
    const IRRender::CanvasImageCommand &quarter = queued->images_[0].command_;
    EXPECT_EQ(quarter.origin_, ivec2(100, 400));
    EXPECT_EQ(quarter.size_, ivec2(60, 40));
    EXPECT_EQ(quarter.unrotate_, Draw::imageUnrotation(IRMath::kHalfPi, Draw::kCanvasTexelPixels));
    // 60 x 40 texels are 120 x 40 pixels about texel (130, 420); turned a
    // quarter that is 40 x 120 pixels, 20 x 120 texels.
    EXPECT_NEAR(quarter.clipLo_.x, 120, 1);
    EXPECT_NEAR(quarter.clipLo_.y, 360, 1);
    EXPECT_NEAR(quarter.clipHi_.x, 140, 1);
    EXPECT_NEAR(quarter.clipHi_.y, 480, 1);
    EXPECT_EQ(queued->primitives_[0].lo_, quarter.clipLo_);
    EXPECT_EQ(queued->primitives_[0].hi_, quarter.clipHi_);
    EXPECT_EQ(
        queued->images_[1].command_.unrotate_,
        Draw::imageUnrotation(0.2f, Draw::kCanvasTexelPixels)
    );
    EXPECT_EQ(queued->images_[1].command_.cell_, 3);
    EXPECT_EQ(queued->images_[2].command_.unrotate_, vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(queued->images_[2].command_.clipLo_, ivec2(100, 800));
    EXPECT_EQ(queued->images_[2].command_.clipHi_, ivec2(160, 840));
}

TEST_F(LuaCanvasDrawImageTest, ImageIsClippedToTheCanvasButKeepsItsBox) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        local handle = IRCanvas.loadImage('probe.png')
        IRCanvas.image('layer', handle, -20, 950, 100, 60)
        IRCanvas.image('layer', handle, 300, 0, 50, 50)
        IRCanvas.image('layer', handle, 0, 0, 0, 10)
    )lua"));
    const C_CanvasDrawQueue::Batch *queued = batch();
    ASSERT_NE(queued, nullptr);
    // The draw past the right edge and the empty one queue nothing.
    ASSERT_EQ(queued->images_.size(), 1u);
    const IRRender::CanvasImageCommand &command = queued->images_[0].command_;
    EXPECT_EQ(command.clipLo_, ivec2(0, 950));
    EXPECT_EQ(command.clipHi_, ivec2(80, 962));
    EXPECT_EQ(command.origin_, ivec2(-20, 950));
    EXPECT_EQ(command.size_, ivec2(100, 60));
}

TEST_F(LuaCanvasDrawImageTest, ImagesJoinThePainterOrderOfTheOtherDraws) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        local handle = IRCanvas.loadImage('probe.png')
        IRCanvas.image('layer', handle, 0, 0, 100, 100)
        IRCanvas.fillRect('layer', 150, 0, 12, 11, { 1, 1, 1 })
        IRCanvas.image('layer', handle, 0, 200, 50, 50)
        IRCanvas.text('layer', 'HZ', 10, 10)
        IRCanvas.image('layer', handle, 5, 5, 40, 40)
    )lua"));
    const C_CanvasDrawQueue::Batch *queued = batch();
    ASSERT_NE(queued, nullptr);
    ASSERT_EQ(queued->primitives_.size(), 5u);
    ASSERT_EQ(queued->images_.size(), 3u);
    constexpr int kNoImage = C_CanvasDrawQueue::Batch::kNoImage;
    EXPECT_EQ(queued->primitives_[0].image_, 0);
    EXPECT_EQ(queued->primitives_[1].image_, kNoImage);
    EXPECT_EQ(queued->primitives_[2].image_, 1);
    EXPECT_EQ(queued->primitives_[3].image_, kNoImage);
    EXPECT_EQ(queued->primitives_[4].image_, 2);
    // An image holds no glyph commands, so the rectangle's and the text's
    // stay one contiguous range across it.
    EXPECT_EQ(queued->primitives_[0].end_, 0u);
    EXPECT_EQ(queued->primitives_[1].end_, 2u);
    EXPECT_EQ(queued->primitives_[2].end_, 2u);
    EXPECT_EQ(queued->primitives_[3].end_, 4u);
    EXPECT_EQ(queued->primitives_[4].end_, 4u);

    const std::span<const C_CanvasDrawQueue::Batch::Primitive> primitives(queued->primitives_);
    // The first image, the rectangle and the second image are clear of each
    // other; the text sits on the first image, and the last image on both.
    EXPECT_EQ(Draw::overlapGroupEnd(primitives, 0), 3u);
    EXPECT_EQ(Draw::overlapGroupEnd(primitives, 3), 4u);
    EXPECT_EQ(Draw::overlapGroupEnd(primitives, 4), 5u);
}

TEST_F(LuaCanvasDrawImageTest, RejectsBadHandlesCanvasesKeysAndRanges) {
    EXPECT_TRUE(scriptSucceeds("handle = IRCanvas.loadImage('probe.png')"));
    const auto rejects = [this](const std::string &call, const char *reason) {
        EXPECT_NE(scriptError(call).find(reason), std::string::npos) << call;
    };
    rejects("IRCanvas.image('nope', handle, 0, 0, 8, 8)", "no canvas named nope");
    rejects("IRCanvas.image('layer', nil, 0, 0, 8, 8)", "loaded image");
    rejects("IRCanvas.image('layer', handle + 100, 0, 0, 8, 8)", "loaded image");
    rejects("IRCanvas.image('layer', 'probe.png', 0, 0, 8, 8)", "loaded image");
    rejects("IRCanvas.imageSize(0)", "loaded image");
    rejects("IRCanvas.releaseImage(nil)", "handle must be");
    rejects("IRCanvas.image('layer', handle, 0, 0, 8, 8, { crop = {} })", "unknown option");
    rejects("IRCanvas.image('layer', handle, 0, 0, 8, 8, 5)", "options must be a table");
    rejects("IRCanvas.image('layer', handle, 0, 0, 8, 8, { uv = 1 })", "uv must be");
    rejects("IRCanvas.image('layer', handle, 0, 0, 8, 8, { uv = { 0, 0, 1 } })", "uv must be");
    rejects("IRCanvas.image('layer', handle, 0, 0, 8, 8, { uv = { 0, 0, 1, 1.5 } })", "uv must be");
    rejects("IRCanvas.image('layer', handle, 0, 0, 8, 8, { uv = { 0, 'a', 1, 1 } })", "uv must be");
    rejects("IRCanvas.image('layer', handle, 0, 0, 8, 8, { cell = 0 })", "cell");
    rejects("IRCanvas.image('layer', handle, 0, 0, 8, 8, { cell = 1.5 })", "cell");
    rejects("IRCanvas.image('layer', handle, 0, 0, 8, 8, { levels = 1 })", "levels");
    rejects("IRCanvas.image('layer', handle, 0, 0, 8, 8, { levels = 65 })", "levels");
    rejects("IRCanvas.image('layer', handle, 0, 0, 8, 8, { distance = 70000 })", "distance");
    rejects("IRCanvas.image('layer', handle, 0, 0, 8, 8, { rotation = 'a' })", "rotation");
    rejects("IRCanvas.image('layer', handle, 0, 0, 8, 8, { rotation = { 1 } })", "rotation");
    rejects("IRCanvas.image('layer', handle, 0, 0, 8, 8, { rotation = 0 / 0 })", "rotation");
    rejects("IRCanvas.image('layer', handle, 0, 0, 8, 8, { rotation = -math.huge })", "rotation");
    rejects("IRCanvas.image('layer', handle, 0, 0, 8, 8, { rotate = 1 })", "unknown option");
    EXPECT_EQ(batch(), nullptr);
}

} // namespace

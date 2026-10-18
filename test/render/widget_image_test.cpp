#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/image_data.hpp>
#include <irreden/render/widgets.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using IRComponents::C_Widget;
using IRComponents::C_WidgetImage;
using IRComponents::WidgetKind;
using IRMath::Color;
using IRMath::ivec2;

constexpr Color kBackground{40, 44, 56, 255};

std::vector<std::uint8_t> solidImage(ivec2 size, Color color) {
    std::vector<std::uint8_t> rgba;
    for (int i = 0; i < size.x * size.y; ++i) {
        rgba.insert(rgba.end(), {color.red_, color.green_, color.blue_, color.alpha_});
    }
    return rgba;
}

void setPixel(std::vector<std::uint8_t> &rgba, ivec2 size, ivec2 at, Color color) {
    const std::size_t o = (static_cast<std::size_t>(at.y) * size.x + at.x) * 4;
    rgba[o] = color.red_;
    rgba[o + 1] = color.green_;
    rgba[o + 2] = color.blue_;
    rgba[o + 3] = color.alpha_;
}

void expectColor(Color got, Color expected) {
    EXPECT_EQ(got.red_, expected.red_);
    EXPECT_EQ(got.green_, expected.green_);
    EXPECT_EQ(got.blue_, expected.blue_);
    EXPECT_EQ(got.alpha_, expected.alpha_);
}

// ---- IRRender::boxFilterToTrixels ------------------------------------------

TEST(BoxFilterToTrixels, AveragesEachTwoByOneBlock) {
    // 4x2 pixels into 2x2 texels: s = 1, one 2x1 block per texel.
    const ivec2 size(4, 2);
    std::vector<std::uint8_t> rgba = solidImage(size, Color{0, 0, 0, 255});
    setPixel(rgba, size, ivec2(0, 0), Color{255, 255, 255, 255});
    setPixel(rgba, size, ivec2(3, 1), Color{200, 100, 0, 255});
    const std::vector<Color> texels =
        IRRender::boxFilterToTrixels(rgba.data(), size, ivec2(2, 2), kBackground);
    ASSERT_EQ(texels.size(), 4u);
    expectColor(texels[0], Color{128, 128, 128, 255});
    expectColor(texels[1], Color{0, 0, 0, 255});
    expectColor(texels[2], Color{0, 0, 0, 255});
    expectColor(texels[3], Color{100, 50, 0, 255});
}

TEST(BoxFilterToTrixels, FitsAndLetterboxesOnTheBackground) {
    // 8x2 pixels into 4x4 texels: s = max(8/8, 2/4) = 1, so the image covers
    // two texel rows, centred: rows 1 and 2.
    const ivec2 size(8, 2);
    const Color orange{230, 140, 40, 255};
    const std::vector<std::uint8_t> rgba = solidImage(size, orange);
    const std::vector<Color> texels =
        IRRender::boxFilterToTrixels(rgba.data(), size, ivec2(4, 4), kBackground);
    for (int x = 0; x < 4; ++x) {
        expectColor(texels[0 * 4 + x], kBackground);
        expectColor(texels[1 * 4 + x], orange);
        expectColor(texels[2 * 4 + x], orange);
        expectColor(texels[3 * 4 + x], kBackground);
    }
}

TEST(BoxFilterToTrixels, WeightsFractionalBlockEdgesByCoverage) {
    // 3x1 pixels into 1x1 texel: s = max(3/2, 1) = 1.5, block 3 x 1.5 — the
    // image is 1 px tall, so the texel is two thirds image, one third
    // background (the letterbox rows split around it).
    const ivec2 size(3, 1);
    const std::vector<std::uint8_t> rgba = solidImage(size, Color{255, 255, 255, 255});
    const std::vector<Color> texels =
        IRRender::boxFilterToTrixels(rgba.data(), size, ivec2(1, 1), Color{0, 0, 0, 255});
    ASSERT_EQ(texels.size(), 1u);
    expectColor(texels[0], Color{170, 170, 170, 255});
}

TEST(BoxFilterToTrixels, CompositesAlphaOverTheBackground) {
    const ivec2 size(2, 1);
    const std::vector<std::uint8_t> rgba = solidImage(size, Color{255, 0, 0, 0});
    const std::vector<Color> clear =
        IRRender::boxFilterToTrixels(rgba.data(), size, ivec2(1, 1), kBackground);
    expectColor(clear[0], kBackground);

    const std::vector<std::uint8_t> half = solidImage(size, Color{240, 0, 0, 128});
    const std::vector<Color> blended =
        IRRender::boxFilterToTrixels(half.data(), size, ivec2(1, 1), Color{0, 0, 0, 255});
    // 240 * 128/255 = 120.47 -> 120; the composite is opaque.
    expectColor(blended[0], Color{120, 0, 0, 255});
}

TEST(BoxFilterToTrixels, NoImageIsAllBackground) {
    const std::vector<Color> texels =
        IRRender::boxFilterToTrixels(nullptr, ivec2(0, 0), ivec2(3, 2), kBackground);
    ASSERT_EQ(texels.size(), 6u);
    for (const Color &texel : texels) {
        expectColor(texel, kBackground);
    }
}

// ---- IRRender::ImageData non-asserting load --------------------------------

TEST(ImageDataLoad, MissingFileReportsThePath) {
    const std::string path = testing::TempDir() + "widget_image_test_missing.png";
    std::string error;
    const IRRender::ImageData image{path.c_str(), error};
    EXPECT_EQ(image.data_, nullptr);
    EXPECT_EQ(image.width_, 0);
    EXPECT_EQ(image.height_, 0);
    EXPECT_NE(error.find(path), std::string::npos) << error;
}

// ---- IRPrefab::Widget::makeImage -------------------------------------------

class WidgetImageTest : public testing::Test {
  protected:
    void SetUp() override {
        m_pngPath = testing::TempDir() + "widget_image_test_4x2.png";
        std::vector<std::uint8_t> rgba = solidImage(ivec2(4, 2), Color{10, 200, 30, 255});
        IRRender::writePNG(m_pngPath.c_str(), 4, 2, 4, rgba.data());
    }

    void TearDown() override {
        m_entityManager.destroyAllEntities();
    }

    std::string m_pngPath;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
};

TEST_F(WidgetImageTest, MakeImageResamplesAtBuild) {
    const IREntity::EntityId id = IRPrefab::Widget::makeImage(ivec2(4, 6), ivec2(2, 2), m_pngPath);
    const C_Widget &widget = IREntity::getComponent<C_Widget>(id);
    EXPECT_EQ(widget.kind_, WidgetKind::IMAGE);
    EXPECT_EQ(widget.size_, ivec2(2, 2));
    const C_WidgetImage &image = IREntity::getComponent<C_WidgetImage>(id);
    EXPECT_TRUE(image.loaded_);
    EXPECT_TRUE(IRPrefab::Widget::imageLoaded(id));
    EXPECT_TRUE(IRPrefab::Widget::imageError(id).empty());
    EXPECT_EQ(image.texelSize_, ivec2(2, 2));
    ASSERT_EQ(image.texels_.size(), 4u);
    for (const Color &texel : image.texels_) {
        expectColor(texel, Color{10, 200, 30, 255});
    }
}

TEST_F(WidgetImageTest, MissingFileDrawsTheHatchAndNamesThePath) {
    const std::string missing = testing::TempDir() + "widget_image_test_absent.png";
    const IREntity::EntityId id = IRPrefab::Widget::makeImage(ivec2(0, 0), ivec2(10, 3), missing);
    EXPECT_FALSE(IRPrefab::Widget::imageLoaded(id));
    EXPECT_NE(IRPrefab::Widget::imageError(id).find(missing), std::string::npos);

    const IRPrefab::Widget::WidgetTheme &theme = IRPrefab::Widget::defaultTheme();
    const C_WidgetImage &image = IREntity::getComponent<C_WidgetImage>(id);
    ASSERT_EQ(image.texels_.size(), 30u);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 10; ++x) {
            const bool line = (x + y) % IRPrefab::Widget::detail::kImageHatchPeriod <
                              IRPrefab::Widget::detail::kImageHatchWidth;
            expectColor(
                image.texels_[y * 10 + x],
                line ? theme.textDisabled_ : theme.backgroundDisabled_
            );
        }
    }
}

TEST_F(WidgetImageTest, SetImagePathReloadsAndClearsTheError) {
    const IREntity::EntityId id =
        IRPrefab::Widget::makeImage(ivec2(0, 0), ivec2(2, 2), m_pngPath + ".absent");
    ASSERT_FALSE(IRPrefab::Widget::imageLoaded(id));

    IRPrefab::Widget::setImagePath(id, m_pngPath);
    EXPECT_TRUE(IRPrefab::Widget::imageLoaded(id));
    EXPECT_TRUE(IRPrefab::Widget::imageError(id).empty());
    EXPECT_EQ(IREntity::getComponent<C_WidgetImage>(id).path_, m_pngPath);
    expectColor(IREntity::getComponent<C_WidgetImage>(id).texels_[0], Color{10, 200, 30, 255});
}

TEST_F(WidgetImageTest, SetImageSizeResamplesToTheNewRect) {
    const IREntity::EntityId id = IRPrefab::Widget::makeImage(ivec2(0, 0), ivec2(2, 2), m_pngPath);
    IRPrefab::Widget::setImageSize(id, ivec2(4, 4));
    EXPECT_EQ(IREntity::getComponent<C_Widget>(id).size_, ivec2(4, 4));
    const C_WidgetImage &image = IREntity::getComponent<C_WidgetImage>(id);
    EXPECT_EQ(image.texelSize_, ivec2(4, 4));
    ASSERT_EQ(image.texels_.size(), 16u);
    // 4x2 px into 4x4 texels: s = 0.5, so the image fills rows 0..3 and
    // columns 0..3 with no letterbox.
    expectColor(image.texels_[0], Color{10, 200, 30, 255});
    expectColor(image.texels_[15], Color{10, 200, 30, 255});
}

} // namespace

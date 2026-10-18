#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/voxel_font.hpp>
#include <irreden/render/voxel_text.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include "../../creations/demos/shape_debug/voxel_text_sample.hpp"

#include <bitset>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace {

namespace VoxelText = IRPrefab::VoxelText;
using IRMath::ivec3;
using IRMath::kPi;
using IRMath::kQuarterPi;
using IRMath::vec2;
using IRMath::vec3;

std::vector<ivec3> advances(const VoxelText::VoxelTextPath &path, float yaw, int scale) {
    const std::vector<ivec3> points =
        VoxelText::detail::letterPoints(4, path, ivec3(0), yaw, scale);
    std::vector<ivec3> steps;
    for (std::size_t i = 1; i < points.size(); ++i) {
        steps.push_back(points[i] - points[i - 1]);
    }
    return steps;
}

VoxelText::VoxelTextPath headingPath(float heading) {
    VoxelText::VoxelTextPath path;
    path.headings_ = {heading};
    return path;
}

int popcount(const IRRender::VoxelGlyph &glyph) {
    int lit = 0;
    for (const std::uint8_t row : glyph) {
        lit += static_cast<int>(std::bitset<8>(row).count());
    }
    return lit;
}

struct IsoBox {
    vec2 min_ = vec2(1e9f);
    vec2 max_ = vec2(-1e9f);
};

// The projected box of every cell a letter at `point` emits.
IsoBox letterIsoBox(const IRRender::VoxelGlyph &glyph, ivec3 point, float yaw, int depth) {
    IsoBox box;
    VoxelText::detail::forEachGlyphCell(
        glyph,
        VoxelText::detail::readingDirection(yaw),
        depth,
        1,
        [&](ivec3 offset, int) {
            const vec2 iso = IRMath::pos3DtoPos2DIsoYawed(
                vec3(point + offset),
                VoxelText::detail::snappedYaw(yaw)
            );
            box.min_ = IRMath::min(box.min_, iso);
            box.max_ = IRMath::max(box.max_, iso);
        }
    );
    return box;
}

TEST(VoxelTextPlacement, ReadingDirectionAtEachSnappedYaw) {
    const ivec3 expected[8] = {
        ivec3(-1, 1, 0),
        ivec3(-1, 0, 0),
        ivec3(-1, -1, 0),
        ivec3(0, -1, 0),
        ivec3(1, -1, 0),
        ivec3(1, 0, 0),
        ivec3(1, 1, 0),
        ivec3(0, 1, 0),
    };
    for (int octant = 0; octant < 8; ++octant) {
        const float yaw = static_cast<float>(octant) * kQuarterPi;
        EXPECT_EQ(VoxelText::detail::readingDirection(yaw), expected[octant]) << octant;
        EXPECT_EQ(VoxelText::detail::readingDirection(yaw - 2.0f * kPi), expected[octant])
            << octant;
        const ivec3 normal = VoxelText::detail::depthNormal(expected[octant]);
        EXPECT_GT(IRMath::pos3DtoDistanceYawed(vec3(normal), yaw), 0.0f)
            << "the depth normal points away from the camera at octant " << octant;
    }
}

TEST(VoxelTextPlacement, ReadingDirectionIsScreenRight) {
    for (int octant = 0; octant < 8; ++octant) {
        const float yaw = static_cast<float>(octant) * kQuarterPi;
        const vec2 step =
            IRMath::pos3DtoPos2DIsoYawed(vec3(VoxelText::detail::readingDirection(yaw)), yaw);
        EXPECT_GT(step.x, 1.0f) << octant;
        EXPECT_NEAR(step.y, 0.0f, 1e-5f) << octant;
    }
}

TEST(VoxelTextPlacement, YawSnapsToTheNearestEighthTurn) {
    EXPECT_EQ(VoxelText::detail::readingDirection(0.5f), ivec3(-1, 0, 0));
    EXPECT_EQ(VoxelText::detail::readingDirection(0.3f), ivec3(-1, 1, 0));
    EXPECT_EQ(VoxelText::detail::readingDirection(-0.3f), ivec3(-1, 1, 0));
    EXPECT_EQ(VoxelText::detail::readingDirection(-0.5f), ivec3(0, 1, 0));
}

TEST(VoxelTextPlacement, ExactAdvancesAtScalesOneAndTwo) {
    for (const int scale : {1, 2}) {
        const auto all = [](const std::vector<ivec3> &steps, ivec3 step) {
            for (const ivec3 each : steps) {
                if (each != step) {
                    return false;
                }
            }
            return steps.size() == 3;
        };
        EXPECT_TRUE(all(advances({}, 0.0f, scale), ivec3(0, 12 * scale, 0))) << scale;
        EXPECT_TRUE(
            all(advances(headingPath(3.0f * kQuarterPi), 0.0f, scale),
                ivec3(-6 * scale, 6 * scale, 0))
        ) << scale;
        EXPECT_TRUE(all(advances(headingPath(kPi), 0.0f, scale), ivec3(-12 * scale, 0, 0)))
            << scale;
        EXPECT_TRUE(all(advances({}, kQuarterPi, scale), ivec3(-6 * scale, 6 * scale, 0))) << scale;
        EXPECT_TRUE(all(advances({}, 2.0f * kQuarterPi, scale), ivec3(-12 * scale, 0, 0))) << scale;
    }
}

TEST(VoxelTextPlacement, AShortHeadingListRepeatsItsLastEntry) {
    VoxelText::VoxelTextPath path;
    path.headings_ = {0.5f * kPi, kPi};
    const std::vector<ivec3> steps = advances(path, 0.0f, 1);
    ASSERT_EQ(steps.size(), 3u);
    EXPECT_EQ(steps[0], ivec3(0, 12, 0));
    EXPECT_EQ(steps[1], ivec3(-12, 0, 0));
    EXPECT_EQ(steps[2], ivec3(-12, 0, 0));
}

TEST(VoxelTextPlacement, PolylineLettersContinuePastItsEnd) {
    VoxelText::VoxelTextPath path;
    path.points_ = {vec3(5.0f, 10.0f, 3.0f)};
    const std::vector<ivec3> points =
        VoxelText::detail::letterPoints(4, path, ivec3(5, -2, 3), 0.0f, 1);
    ASSERT_EQ(points.size(), 4u);
    EXPECT_EQ(points[0], ivec3(0));
    for (std::size_t i = 1; i < points.size(); ++i) {
        EXPECT_EQ(points[i], ivec3(0, 12 * static_cast<int>(i), 0)) << i;
    }
}

TEST(VoxelTextPlacement, FrontLayerCellsLieOnTheReadingLine) {
    for (int octant = 0; octant < 8; ++octant) {
        const float yaw = static_cast<float>(octant) * kQuarterPi;
        const ivec3 reading = VoxelText::detail::readingDirection(yaw);
        for (int ascii = 0; ascii < 128; ++ascii) {
            const IRRender::VoxelGlyph &glyph = *IRRender::getVoxelGlyph(static_cast<char>(ascii));
            for (const int scale : {1, 2}) {
                int front = 0;
                int total = 0;
                VoxelText::detail::forEachGlyphCell(
                    glyph,
                    reading,
                    3,
                    scale,
                    [&](ivec3 offset, int layer) {
                        ++total;
                        if (layer != 0) {
                            return;
                        }
                        ++front;
                        EXPECT_EQ(offset.x * reading.y - offset.y * reading.x, 0)
                            << "octant " << octant << " char " << ascii;
                    }
                );
                const int perLayer =
                    popcount(glyph) * scale * IRRender::kVoxelGlyphRowVoxels * scale;
                EXPECT_EQ(front, perLayer);
                EXPECT_EQ(total, perLayer * 3);
            }
        }
    }
}

TEST(VoxelTextPlacement, LetterBoxHoldsEveryEmittedCell) {
    for (int octant = 0; octant < 8; ++octant) {
        const float yaw = static_cast<float>(octant) * kQuarterPi;
        const ivec3 reading = VoxelText::detail::readingDirection(yaw);
        for (const int scale : {1, 3}) {
            const VoxelText::detail::CellBox box = VoxelText::detail::letterBox(reading, 2, scale);
            const ivec3 high = box.min_ + box.size_ - ivec3(1);
            VoxelText::detail::forEachGlyphCell(
                IRRender::kVoxelGlyph_Hash,
                reading,
                2,
                scale,
                [&](ivec3 offset, int) {
                    EXPECT_TRUE(
                        IRMath::all(IRMath::greaterThanEqual(offset, box.min_)) &&
                        IRMath::all(IRMath::greaterThanEqual(high, offset))
                    ) << octant;
                }
            );
        }
    }
}

TEST(VoxelTextPlacement, FixtureBentPathLettersDoNotOverlapOnScreen) {
    const auto titles = IRShapeDebug::VoxelTextSample::titles();
    const auto &bent = titles[2];
    ASSERT_FALSE(bent.options_.path_.points_.empty());
    for (const float yaw : {0.0f, kQuarterPi, 2.0f * kQuarterPi}) {
        const std::vector<ivec3> points = VoxelText::detail::letterPoints(
            bent.text_.size(),
            bent.options_.path_,
            IRMath::roundVec3HalfUp(bent.options_.origin_),
            yaw,
            bent.options_.scale_
        );
        for (std::size_t i = 1; i < points.size(); ++i) {
            const IsoBox a = letterIsoBox(
                *IRRender::getVoxelGlyph(bent.text_[i - 1]),
                points[i - 1],
                yaw,
                bent.options_.depth_
            );
            const IsoBox b = letterIsoBox(
                *IRRender::getVoxelGlyph(bent.text_[i]),
                points[i],
                yaw,
                bent.options_.depth_
            );
            const bool apart = a.max_.x < b.min_.x || b.max_.x < a.min_.x || a.max_.y < b.min_.y ||
                               b.max_.y < a.min_.y;
            EXPECT_TRUE(apart) << "letters " << i - 1 << " and " << i << " at yaw " << yaw;
            EXPECT_GT(IRMath::pos3DtoPos2DIsoYawed(vec3(points[i] - points[i - 1]), yaw).x, 0.0f)
                << "letter " << i << " reads left to right at yaw " << yaw;
        }
    }
}

// IR_ASSERT throws std::runtime_error in debug builds; each guard below
// sits on a path that, unguarded, would build garbage instead of raising.
TEST(VoxelTextPlacement, MalformedPathsTripTheirGuards) {
    VoxelText::VoxelTextPath both;
    both.headings_ = {0.0f};
    both.points_ = {vec3(4.0f, 0.0f, 0.0f)};
    EXPECT_THROW(VoxelText::detail::letterPoints(2, both, ivec3(0), 0.0f, 1), std::runtime_error);

    VoxelText::VoxelTextPath stayAtOrigin;
    stayAtOrigin.points_ = {vec3(0.0f), vec3(0.0f)};
    EXPECT_THROW(
        VoxelText::detail::letterPoints(2, stayAtOrigin, ivec3(0), 0.0f, 1),
        std::runtime_error
    );

    VoxelText::VoxelTextPath alongViewAxis;
    alongViewAxis.points_ = {vec3(2.0f, 2.0f, 2.0f)};
    EXPECT_FALSE(VoxelText::detail::movesOnScreen(vec3(1.0f, 1.0f, 1.0f), 0.0f));
    EXPECT_THROW(
        VoxelText::detail::letterPoints(2, alongViewAxis, ivec3(0), 0.0f, 1),
        std::runtime_error
    );
}

class VoxelTextBuild : public testing::Test {
  protected:
    IREntity::EntityManager m_entityManager;
};

TEST_F(VoxelTextBuild, OutOfRangeOptionsTripTheirGuardsBeforeAnyEntity) {
    const IREntity::EntityId canvas =
        IREntity::createEntity(IRComponents::C_VoxelPool{ivec3(64, 64, 64)});
    const auto entitiesBefore = IREntity::getEntityManager().getLiveEntityCount();
    VoxelText::VoxelTextOptions options;
    options.cameraYaw_ = 0.0f;
    options.canvas_ = canvas;

    options.depth_ = 0;
    EXPECT_THROW(VoxelText::create("A", options), std::runtime_error);
    options.depth_ = 2;
    options.scale_ = VoxelText::kMaxScale + 1;
    EXPECT_THROW(VoxelText::create("A", options), std::runtime_error);
    options.scale_ = 1;
    EXPECT_THROW(VoxelText::create("A\nB", options), std::runtime_error);
    EXPECT_EQ(IREntity::getEntityManager().getLiveEntityCount(), entitiesBefore);
}

TEST_F(VoxelTextBuild, LettersStandOnTheirPathPointsAndFaceTheGivenYaw) {
    const IREntity::EntityId canvas =
        IREntity::createEntity(IRComponents::C_VoxelPool{ivec3(64, 64, 64)});
    VoxelText::VoxelTextOptions options;
    options.origin_ = vec3(3.4f, -2.0f, 1.0f);
    options.cameraYaw_ = 2.0f * kQuarterPi;
    options.canvas_ = canvas;
    const IREntity::EntityId root = VoxelText::create("A B", options);

    EXPECT_EQ(
        IREntity::getComponent<IRComponents::C_LocalTransform>(root).translation_,
        vec3(3.0f, -2.0f, 1.0f)
    );
    const std::vector<IREntity::EntityId> children = IREntity::getEntityManager().getChildren(root);
    ASSERT_EQ(children.size(), 2u);
    const ivec3 reading = VoxelText::detail::readingDirection(*options.cameraYaw_);
    EXPECT_EQ(reading, ivec3(-1, -1, 0));
    const VoxelText::detail::CellBox box = VoxelText::detail::letterBox(reading, 2, 1);
    const ivec3 expectedPoints[2] = {ivec3(0), ivec3(-24, 0, 0)};
    const IRRender::VoxelGlyph *expectedGlyphs[2] = {
        &IRRender::kVoxelGlyph_A,
        &IRRender::kVoxelGlyph_B
    };
    for (std::size_t i = 0; i < children.size(); ++i) {
        const auto &set = IREntity::getComponent<IRComponents::C_VoxelSetNew>(children[i]);
        EXPECT_EQ(set.size_, box.size_);
        int active = 0;
        for (const auto &voxel : set.voxels_) {
            active += voxel.color_.alpha_ != 0 ? 1 : 0;
        }
        EXPECT_EQ(active, popcount(*expectedGlyphs[i]) * IRRender::kVoxelGlyphRowVoxels * 2);
        bool matched = false;
        for (std::size_t j = 0; j < 2; ++j) {
            matched =
                matched ||
                IREntity::getComponent<IRComponents::C_LocalTransform>(children[i]).translation_ ==
                    vec3(expectedPoints[j] + box.min_);
        }
        EXPECT_TRUE(matched) << i;
    }
}

} // namespace
